#ifndef KICH_R4_TEXTURE_IO_H
#define KICH_R4_TEXTURE_IO_H
/* P0-S11 PERF01 background DDS reader. The main thread finds a file
 * (KichR4LevelFiles::locate: a lookup in the engine file table, no read) and
 * queues it; one worker reads the stored bytes through its own Win32 handle
 * and unpacks archive LZO (xrCore rtc_decompress, a pure function). The worker
 * never touches the engine filesystem (CLocatorAPI is not thread-safe), the
 * D3D device or the texture registry. Finished bytes wait, bounded by limit,
 * until the main thread takes them for parse and GPU creation; Clear (level
 * change) drops the queue, the results and any read in flight. */
#include "KichR4LevelFiles.h"
#ifndef NOMINMAX
#define NOMINMAX // (std::min) users elsewhere; the engine PCH defines it too
#endif
#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <condition_variable>
namespace kich_r4_texture_io {
typedef uint32_t (*Decompress)(void* dst,uint32_t dst_len,const void* src,uint32_t src_len);
enum Status {Absent=0,Queued,Ready,Failed};
static const uint64_t DefaultLimit=UINT64_C(128)*1024*1024;
// One location into out (exact size); 0 or a failure HRESULT.
inline int32_t Read(const KichR4FileLocation& where,Decompress decompress,std::vector<unsigned char>& out){
    if(!where.stored || !where.size || (!where.compressed && where.stored!=where.size))return static_cast<int32_t>(0x80070057u);
    if(where.compressed && !decompress)return static_cast<int32_t>(0x80004001u);
    const HANDLE file=CreateFileA(where.path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return static_cast<int32_t>(0x80070000u|(GetLastError()&0xffffu));
    std::vector<unsigned char> stored;int32_t hr=0;
    try{stored.resize(where.stored);}catch(...){CloseHandle(file);return static_cast<int32_t>(0x8007000Eu);}
    OVERLAPPED at={};at.Offset=static_cast<DWORD>(where.offset);at.OffsetHigh=static_cast<DWORD>(where.offset>>32);DWORD got=0;
    if(!ReadFile(file,stored.data(),where.stored,&got,&at) || got!=where.stored)hr=static_cast<int32_t>(0x8007001Eu); // read fault / short file
    CloseHandle(file);if(hr)return hr;
    if(!where.compressed){out.swap(stored);return 0;}
    try{out.resize(where.size);}catch(...){return static_cast<int32_t>(0x8007000Eu);}
    if(decompress(out.data(),where.size,stored.data(),where.stored)!=where.size){out.clear();return static_cast<int32_t>(0x8007000Du);}
    return 0;
}
struct Stats {uint64_t requests,completed,failed,bytes,expired,cleared;double read_ms;size_t queued,ready;uint64_t ready_bytes;};
class Loader {
    struct Job {std::string key;KichR4FileLocation where;uint32_t priority;uint64_t sequence,epoch;};
    struct Done {std::vector<unsigned char> bytes;int32_t status;uint64_t epoch;};
    Decompress decompress;uint64_t limit;
    std::mutex lock;std::condition_variable wake;std::thread worker;
    std::deque<Job> queue;std::map<std::string,Done> done;std::set<std::string> waiting;
    std::string running;uint64_t generation,sequence,ready_bytes;bool stop;
    Stats stats;
    // Lowest priority first, then request order; caller holds the lock.
    size_t Next()const{size_t best=0;for(size_t i=1;i<queue.size();++i)
        if(queue[i].priority<queue[best].priority || (queue[i].priority==queue[best].priority && queue[i].sequence<queue[best].sequence))best=i;return best;}
    bool Runnable()const{return !queue.empty() && (ready_bytes<limit || done.empty());}
    // Reads one job outside the lock; caller holds it on entry and exit.
    void Run(std::unique_lock<std::mutex>& held){
        const size_t at=Next();Job job=queue[at];queue.erase(queue.begin()+at);running=job.key;const uint64_t started_generation=generation;
        held.unlock();
        LARGE_INTEGER t0={},t1={},f={};QueryPerformanceCounter(&t0);
        Done result;result.epoch=job.epoch;result.status=Read(job.where,decompress,result.bytes);
        QueryPerformanceCounter(&t1);QueryPerformanceFrequency(&f);
        held.lock();running.clear();waiting.erase(job.key);
        stats.read_ms+=double(t1.QuadPart-t0.QuadPart)*1000.0/double(f.QuadPart?f.QuadPart:1);
        if(generation!=started_generation)return; // Clear() happened during the read
        if(result.status)++stats.failed;else{++stats.completed;stats.bytes+=result.bytes.size();}
        ready_bytes+=result.bytes.size();done[job.key].bytes.swap(result.bytes);done[job.key].status=result.status;done[job.key].epoch=result.epoch;
    }
    void Loop(){std::unique_lock<std::mutex> held(lock);
        for(;;){wake.wait(held,[&]{return stop || Runnable();});if(stop)return;Run(held);}}
public:
    // threaded false: no worker; RunOne drives the queue (tests).
    explicit Loader(Decompress unpack,uint64_t ready_limit=DefaultLimit,bool threaded=true):decompress(unpack),limit(ready_limit),generation(0),sequence(0),ready_bytes(0),stop(false),stats(){
        if(threaded)worker=std::thread([this]{Loop();});}
    ~Loader(){{std::lock_guard<std::mutex> held(lock);stop=true;}wake.notify_all();if(worker.joinable())worker.join();}
    Status Query(const std::string& key,uint64_t* bytes=NULL){std::lock_guard<std::mutex> held(lock);
        const auto found=done.find(key);
        if(found!=done.end()){if(bytes)*bytes=found->second.bytes.size();return found->second.status?Failed:Ready;}
        return waiting.count(key)?Queued:Absent;}
    // False when the key is already queued, reading or finished.
    bool Request(const std::string& key,const KichR4FileLocation& where,uint32_t priority,uint64_t epoch){
        {std::lock_guard<std::mutex> held(lock);if(waiting.count(key) || done.count(key))return false;
            Job job;job.key=key;job.where=where;job.priority=priority;job.sequence=++sequence;job.epoch=epoch;
            queue.push_back(job);waiting.insert(key);++stats.requests;}
        wake.notify_one();return true;}
    // Moves a finished read out (status 0: bytes valid).
    bool Take(const std::string& key,std::vector<unsigned char>& bytes,int32_t& status){
        {std::lock_guard<std::mutex> held(lock);const auto found=done.find(key);if(found==done.end())return false;
            bytes.swap(found->second.bytes);status=found->second.status;ready_bytes-=bytes.size();done.erase(found);}
        wake.notify_one();return true;}
    // Results nobody took within age epochs (the texture left the view).
    void Expire(uint64_t epoch,uint64_t age){bool freed=false;{std::lock_guard<std::mutex> held(lock);
        for(auto i=done.begin();i!=done.end();){if(i->second.epoch+age<epoch){ready_bytes-=i->second.bytes.size();i=done.erase(i);++stats.expired;freed=true;}else ++i;}}
        if(freed)wake.notify_one();}
    void Clear(){std::lock_guard<std::mutex> held(lock);++generation;stats.cleared+=queue.size()+done.size();
        queue.clear();done.clear();waiting.clear();ready_bytes=0;if(!running.empty())waiting.insert(running);}
    Stats Snapshot(){std::lock_guard<std::mutex> held(lock);Stats s=stats;s.queued=queue.size()+(running.empty()?0:1);s.ready=done.size();s.ready_bytes=ready_bytes;return s;}
    bool RunOne(){std::unique_lock<std::mutex> held(lock);if(worker.joinable() || !Runnable())return false;Run(held);return true;}
};
}
#endif
