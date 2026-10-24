#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <new>
#include <typeinfo>
#include <thread>
#include <vector>
#if defined(KICH_TEST_X87)
#include <fpu_control.h>
#endif
#include "KichR4UpdateProbe.h"

static unsigned checks=0,allocations=0;
#define CHECK(value) do {++checks;if(!(value)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#value);std::abort();}}while(0)
void* operator new(std::size_t size){++allocations;void* p=std::malloc(size?size:1);if(!p)throw std::bad_alloc();return p;}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}

struct FakeClock {
    static int64_t now,frequency;
    static unsigned reads,frequencies;
    static int64_t Now(){++reads;return now;}
    static int64_t Frequency(){++frequencies;return frequency;}
    static void Reset(){now=0;frequency=1000;reads=frequencies=0;}
};
int64_t FakeClock::now=0,FakeClock::frequency=1000;
unsigned FakeClock::reads=0,FakeClock::frequencies=0;
using namespace kich_r4_update;

#define ENGINE_API
#define __fastcall
#define __cdecl
typedef uint32_t u32;
template<class T>using xr_vector=std::vector<T>;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "pure_actual_win32.h"
#pragma GCC diagnostic pop
int __cdecl _REG_Compare(const void* a,const void* b){
    const _REG_INFO* lhs=static_cast<const _REG_INFO*>(a);const _REG_INFO* rhs=static_cast<const _REG_INFO*>(b);
    return lhs->Prio<rhs->Prio?1:lhs->Prio>rhs->Prio?-1:0;
}
static unsigned dispatches=0;
void __fastcall rp_Frame(void* p){++dispatches;static_cast<pureFrame*>(p)->OnFrame();}
#include "update_callback_actual.h"

struct Update : pureFrame {
    int64_t duration;
    bool subphase,throws;
    unsigned calls;
    Update(int64_t value=1):duration(value),subphase(false),throws(false),calls(0){}
    void OnFrame(){
        ++calls;
        if(subphase){Scope<FakeClock> scope(ObjectsUpdate);FakeClock::now+=duration;}
        else FakeClock::now+=duration;
        if(throws)throw 17;
    }
};
static void Run(Update& object,uint64_t epoch,uint32_t frame,bool enabled=true,uint32_t mode=0){
    FrameScope<FakeClock> scope(enabled,epoch,frame,mode);
    RP_FUNC* dispatch=enabled?KichR4_MeasuredFrame:rp_Frame;
    dispatch(&object);
}
static void Reset(){CHECK(Current()==0);Global().Reset(0);FakeClock::Reset();dispatches=0;}
static Report Drain(){Report report;Global().Take(report);return report;}

static void OffAndEpoch(){
    Reset();Update object(1500);object.subphase=true;
    const unsigned before=allocations;
    for(unsigned i=0;i<100;++i)Run(object,0,i,false);
    Report report=Drain();CHECK(report.frames==0 && report.callbacks==0);CHECK(object.calls==100 && dispatches==100);
    CHECK(FakeClock::reads==0 && FakeClock::frequencies==0);CHECK(allocations==before);
    Run(object,1,100,true,16);report=Drain();CHECK(report.frames==1 && report.callbacks==1 && report.stored==1);
    CHECK(report.slow[0].frame==100 && report.slow[0].mode==16 && report.slow[0].completed);
    CHECK(report.slow[0].phases[ObjectsUpdate]==1500 && report.phases[ObjectsUpdate].calls==1);
    CHECK(FakeClock::reads==4 && FakeClock::frequencies==1);
    CHECK(std::strstr(report.slow[0].type,"Update") || std::strstr(report.slow[0].type,"RTTI_unavailable"));
    Run(object,1,101);const unsigned reads=FakeClock::reads,frequencies=FakeClock::frequencies;
    Run(object,2,102,false);report=Drain();CHECK(!report.callbacks && !report.frames);
    CHECK(FakeClock::reads==reads && FakeClock::frequencies==frequencies);
    Run(object,3,103);report=Drain();CHECK(report.frames==1 && report.callbacks==1 && report.slow[0].frame==103);
    CHECK(allocations==before);
}
static void LimitsAndRollover(){
    Reset();const unsigned before=allocations;
    const int64_t durations[]={4,5,7,90,1500,13};
    for(unsigned i=0;i<6;++i){Update object(durations[i]);Run(object,1,UINT32_MAX-2+i);}
    Report report=Drain();CHECK(report.callbacks==6 && report.long_callbacks==5 && report.stored==4);
    CHECK(report.slow[0].ticks==1500 && report.slow[1].ticks==90 && report.slow[2].ticks==13 && report.slow[3].ticks==7);
    CHECK(report.slow[0].frame==1 && report.slow[1].frame==0);CHECK(report.total_ticks==1619 && report.max_ticks==1500);
    CHECK(Drain().frames==0);CHECK(allocations==before);
    // Every callback is counted; memory and retained output stay bounded.
    for(unsigned i=0;i<10000;++i){Update object(6);Run(object,1,i);}
    report=Drain();CHECK(report.frames==10000 && report.callbacks==10000 && report.stored==4 && report.long_callbacks==10000);
    CHECK(allocations==before);
    FakeClock::frequency=1001;FakeClock::now=0;Update edge(5);Run(edge,1,8);report=Drain();CHECK(report.callbacks==1 && !report.stored);
    edge.duration=6;Run(edge,1,9);report=Drain();CHECK(report.stored==1 && report.slow[0].ticks==6);
}
static void ReentrantAndExceptions(){
    Reset();Update child(10);child.subphase=true;
    {
        FrameScope<FakeClock> frame(true,1,1,0);
        CallbackScope<FakeClock> outer("Outer");
        {Scope<FakeClock> objects(ObjectsUpdate);
            {Scope<FakeClock> nested(ObjectsUpdate);FakeClock::now+=5;}
            {FrameScope<FakeClock> nested_frame(true,1,2,0);KichR4_MeasuredFrame(&child);}
        }
        outer.Complete();
    }
    Report report=Drain();CHECK(report.frames==1 && report.callbacks==1 && report.nested_callbacks==1 && report.nested_frames==1);
    CHECK(report.slow[0].ticks==15 && report.slow[0].phases[ObjectsUpdate]==15);
    CHECK(report.phases[ObjectsUpdate].calls==1 && FakeClock::reads==4 && FakeClock::frequencies==1);
    CHECK(!Current());Update throwing(1234);throwing.throws=true;throwing.subphase=true;
    bool caught=false;try{Run(throwing,1,3);}catch(int value){caught=value==17;}
    CHECK(caught && !Current());report=Drain();CHECK(report.callbacks==1 && report.interrupted==1);
    CHECK(report.slow[0].ticks==1234 && !report.slow[0].completed && report.slow[0].phases[ObjectsUpdate]==1234);
    Update next(8);Run(next,1,4);report=Drain();CHECK(report.callbacks==1 && report.interrupted==0 && report.slow[0].ordinal==1);
}
static void ClocksAndNames(){
    Reset();Update reverse(-10);FakeClock::now=100;Run(reverse,1,1);Report report=Drain();CHECK(!report.callbacks && report.clock_failures==1);
    Update fail(10);FakeClock::now=-1;Run(fail,1,2);report=Drain();CHECK(!report.callbacks && report.clock_failures==1);
    FakeClock::frequency=0;const unsigned reads=FakeClock::reads;Run(fail,1,3);report=Drain();CHECK(!report.frames && report.clock_failures==1 && FakeClock::reads==reads);
    FakeClock::frequency=1000;FakeClock::now=INT64_MAX-100;Update late(80);Run(late,1,4);report=Drain();CHECK(report.slow[0].ticks==80);
    Sample sample;sample.Name("test callback;\n[]");CHECK(!std::strcmp(sample.type,"test_callback____"));
    char huge[256];std::memset(huge,'x',sizeof(huge));huge[255]=0;sample.Name(huge);
    CHECK(sample.name_truncated && std::strlen(sample.type)==79);sample.Name(0);CHECK(!std::strcmp(sample.type,"unknown_frame_callback"));
    Report totals;totals.frequency=1000;sample.ticks=INT64_MAX;totals.Add(sample);totals.Add(sample);
    CHECK(totals.total_ticks==INT64_MAX && totals.saturated_totals==1);
    // An epoch reset invalidates outstanding scope tickets, including its final clock read.
    {FrameScope<FakeClock> frame(true,2,9,0);CallbackScope<FakeClock> callback("old");Scope<FakeClock> scope(HUDUpdate);
        Global().Reset(3);const unsigned count=FakeClock::reads;{Scope<FakeClock> no_frame(HUDUpdate);}CHECK(FakeClock::reads==count);}
    CHECK(!Current());report=Drain();CHECK(!report.frames && !report.callbacks && !report.phases[HUDUpdate].calls);
}
struct SelfDelete : pureFrame {
    bool* done;
    explicit SelfDelete(bool* flag):done(flag){}
    virtual ~SelfDelete(){}
    void OnFrame(){*done=true;FakeClock::now+=12;delete this;}
};
struct Mutate : pureFrame {
    CRegistrator<pureFrame>* registry;Update* added;unsigned calls;
    Mutate():registry(0),added(0),calls(0){}
    void OnFrame(){++calls;registry->Remove(this);registry->Add(added,2);FakeClock::now+=11;}
};
static void OriginalDispatchSemantics(){
    Reset();bool done=false;SelfDelete* object=new SelfDelete(&done);
    {FrameScope<FakeClock> frame(true,1,1,0);KichR4_MeasuredFrame(object);}
    Report report=Drain();CHECK(done && report.callbacks==1 && report.slow[0].completed);
    CHECK(std::strstr(report.slow[0].type,"SelfDelete") || std::strstr(report.slow[0].type,"RTTI_unavailable"));
    CRegistrator<pureFrame> registry;registry.R.reserve(8);Update first(6),added(7);Mutate mutation;
    mutation.registry=&registry;mutation.added=&added;registry.Add(&mutation,3);registry.Add(&first,1);
    const unsigned before=allocations;
    {FrameScope<FakeClock> frame(true,1,2,0);registry.Process(KichR4_MeasuredFrame);}
    report=Drain();CHECK(mutation.calls==1 && first.calls==1 && added.calls==1 && report.callbacks==3);
    CHECK(registry.R.size()==2 && !registry.in_process && !registry.changed && allocations==before);
    registry.R[0].Prio=REG_PRIORITY_CAPTURE;
    {FrameScope<FakeClock> frame(true,1,3,0);registry.Process(KichR4_MeasuredFrame);}
    report=Drain();CHECK(report.callbacks==1 && added.calls==2 && first.calls==1);
}
struct Sink {
    unsigned lines;char text[32768];std::size_t used;
    Sink():lines(0),used(0){text[0]=0;}
    void Emit(const char* format,...) __attribute__((format(gnu_printf,2,3))){
        ++lines;va_list args;va_start(args,format);int count=std::vsnprintf(text+used,sizeof(text)-used,format,args);va_end(args);
        CHECK(count>=0 && std::size_t(count)+used+1<sizeof(text));used+=std::size_t(count);text[used++]='\n';text[used]=0;
    }
};
static void ReportsAndThreads(){
    Reset();
    {FrameScope<FakeClock> frame(true,1,44,4);CallbackScope<FakeClock> callback("AllPhases");
        for(unsigned i=0;i<PhaseCount;++i){Scope<FakeClock> phase(static_cast<Phase>(i));FakeClock::now+=6;}
        callback.Complete();
        bool worker_active=true;
        std::thread worker([&worker_active](){worker_active=Current()!=0;Scope<FakeClock> phase(SchedulerUpdate);});worker.join();
        CHECK(!worker_active);
    }
    for(unsigned i=0;i<3;++i){Update object(10+i);Run(object,1,45+i);}
    Sink sink;const unsigned before=allocations;EmitReport(sink);CHECK(sink.lines==1+PhaseCount+4 && allocations==before);
    CHECK(std::strstr(sink.text,"frame=44") && std::strstr(sink.text,"mode=0x4") && std::strstr(sink.text,"type=AllPhases"));
    CHECK(std::strstr(sink.text,"objects_ms=6.000") && std::strstr(sink.text,"bounded_top4"));
    sink=Sink();EmitReport(sink);CHECK(sink.lines==1 && std::strstr(sink.text,"frames=0 callbacks=0"));
}
int main(){
#if defined(KICH_TEST_X87)
    fpu_control_t saved;_FPU_GETCW(saved);fpu_control_t single=(saved&~_FPU_EXTENDED)|_FPU_SINGLE;_FPU_SETCW(single);
#endif
    OffAndEpoch();LimitsAndRollover();ReentrantAndExceptions();ClocksAndNames();OriginalDispatchSemantics();ReportsAndThreads();
#if defined(KICH_TEST_X87)
    _FPU_SETCW(saved);
#endif
    std::printf("R4 bounded update probe: %u checks passed\n",checks);
}
