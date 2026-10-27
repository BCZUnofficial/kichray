#ifndef KICH_R4_PARALLEL_FOR_H
#define KICH_R4_PARALLEL_FOR_H
/* P0-S11 PERF02: R4's own helper threads for short frame-bound CPU loops (the
 * detail instance assembly, r4_detail_threads). For(count,body) runs body(i)
 * for every i in [0,count) on the calling thread and the helpers and returns
 * when all of them finished; the caller always takes part, so no helpers is
 * the plain serial loop. Helpers sleep between calls and touch only body.
 * One For runs at a time; an exception from body is rethrown on the caller
 * after every index ended. */
#include <stdint.h>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
namespace kich_r4_parallel {
class Pool {
    std::mutex serial; // one For at a time
    std::mutex lock;std::condition_variable wake,idle;
    std::vector<std::thread> threads;
    // Written under lock only while no helper is inside a call (running==0).
    const std::function<void(size_t)>* body;size_t count;
    std::atomic<size_t> next;
    unsigned running;uint64_t call;bool stop;
    std::exception_ptr failure;
    void Drain(){
        for(;;){const size_t i=next.fetch_add(1);if(i>=count)return;
            try{(*body)(i);}catch(...){std::lock_guard<std::mutex> held(lock);if(!failure)failure=std::current_exception();}}}
    void Loop(){
        uint64_t seen=0;std::unique_lock<std::mutex> held(lock);
        for(;;){wake.wait(held,[&]{return stop || call!=seen;});if(stop)return;
            seen=call;++running;held.unlock();Drain();held.lock();if(!--running)idle.notify_all();}}
    void Stop(){
        {std::lock_guard<std::mutex> held(lock);stop=true;}wake.notify_all();
        for(size_t i=0;i<threads.size();++i)threads[i].join();
        threads.clear();stop=false;}
public:
    Pool():body(NULL),count(0),next(0),running(0),call(0),stop(false){}
    ~Pool(){Stop();}
    size_t Helpers()const{return threads.size();}
    void For(size_t n,unsigned helpers,const std::function<void(size_t)>& f){
        std::lock_guard<std::mutex> one(serial);
        if(helpers!=threads.size()){Stop();
            try{for(unsigned i=0;i<helpers;++i)threads.push_back(std::thread([this]{Loop();}));}
            catch(...){Stop();} // no threads: serial below
        }
        if(threads.empty() || n<2){for(size_t i=0;i<n;++i)f(i);return;}
        {std::unique_lock<std::mutex> held(lock);
            // A helper woken by the previous call may still be leaving it.
            idle.wait(held,[&]{return !running;});
            body=&f;count=n;next.store(0);failure=nullptr;++call;}
        wake.notify_all();
        Drain();
        std::exception_ptr error;
        {std::unique_lock<std::mutex> held(lock);idle.wait(held,[&]{return !running;});
            body=NULL;count=0;error=failure;failure=nullptr;}
        if(error)std::rethrow_exception(error);
    }
};
// The one pool R4 shares: the detail instance assembly (engine parallel
// thread) and the detail shadow selection (render thread). For never nests.
inline Pool& Shared(){static Pool* pool=new Pool();return *pool;} // outlives static teardown
// Its helper count, r4_detail_threads as last published by the assembly.
inline std::atomic<unsigned>& SharedHelpers(){static std::atomic<unsigned> helpers(0);return helpers;}
// Chunks per thread of one For: balance when a thread starts late.
const size_t kChunksPerThread=4;
inline size_t Parts(unsigned helpers){return size_t(helpers+1)*kChunksPerThread;}
// Cuts entries (entry i starts at item first(i) of total) into at most parts
// contiguous chunks of about equal items: chunk c is [cuts[c],cuts[c+1]).
template<class First> void Cuts(size_t entries,size_t total,size_t parts,First first,std::vector<size_t>& cuts){
    cuts.clear();cuts.push_back(0);
    for(size_t i=1,c=1;i<entries && c<parts;++i)if(first(i)>=c*total/parts){cuts.push_back(i);while(c<parts && first(i)>=c*total/parts)++c;}
    cuts.push_back(entries);}
}
#endif
