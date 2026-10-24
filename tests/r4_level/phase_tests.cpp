#include "../../xr_3da/KichR4FramePhases.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <limits>
#if defined(KICH_TEST_X87)
#include <fpu_control.h>
#endif
kich_r4_phases::Tracker* PhasePeer();
void PhasePeerMark(uint32_t frame);
namespace {
using namespace kich_r4_phases;
unsigned checks=0;
void Require(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
bool Near(double a,double b){return std::fabs(a-b)<.0001;}
const int64_t Hz=10000000;
void Frame(Tracker& tracker,uint32_t frame,int64_t start,int64_t duration,bool eligible=true){
    tracker.Begin(frame,start,Hz);tracker.MarkEligible(frame,eligible);tracker.End(frame,start+duration);
}
void LongClockAndPhases(){
    const int64_t starts[]={1,180000000000LL,1000000000000000000LL,
        (std::numeric_limits<int64_t>::max)()-Hz*10};
    for(unsigned n=0;n<sizeof(starts)/sizeof(starts[0]);++n){
        Tracker tracker;const int64_t start=starts[n];
        tracker.Begin(7,start,Hz);
        tracker.Span(7,FrameMove,156789);
        tracker.Span(7,Render,456789);
        tracker.Span(7,NativePrelude,12345);
        tracker.Span(7,NativeSubmit,100000);
        tracker.Span(7,Present,200000);
        tracker.Span(7,PostPresent,50000);
        tracker.Span(7,WorkerWait,12000);
        tracker.Span(7,WorkerFallback,0);
        tracker.MarkEligible(7,true);
        Report unfinished;
        Require(!tracker.TakeReport(unfinished),"unfinished current frame must not be reported");
        tracker.End(7,start+700000);
        Report report;Require(tracker.TakeReport(report),"70-ms completed frame must be captured");
        Require(report.samples==1 && report.slow_count==1 && report.stored==1,"bounded aggregate records complete frame");
        const Sample& sample=report.slow[0];
        Require(sample.frame==7 && sample.measured==255,"frame identity and measured phases preserved");
        Require(Near(sample.Milliseconds(sample.CycleTicks()),70),"integer total retains precision at long uptime");
        Require(Near(sample.PhaseMS(FrameMove),15.6789),"FrameMove keeps sub-ms precision with PC_24");
        Require(Near(sample.PhaseMS(Render),45.6789) && Near(sample.PhaseMS(NativeSubmit),10),"nested render/submission spans kept separate");
        Require(Near(sample.PhaseMS(NativePrelude),1.2345) && Near(sample.PhaseMS(PostPresent),5),"previously unmeasured presenter work attributed");
        Require(sample.gap_ticks==0,"first frame has no invented pre-frame interval");
        Require(!tracker.TakeReport(report) && report.samples==0,"report drains window once");
    }
}
void InterFrameAndEligibility(){
    Tracker tracker;const int64_t start=180000000000LL;
    Frame(tracker,1,start,100000);
    Frame(tracker,2,start+100000+Hz,100000);
    Report report;Require(tracker.TakeReport(report),"long message or scheduling gap retained");
    Require(report.samples==2 && report.slow_count==1 && report.slow[0].frame==2,"inter-frame pause attributed to resumed frame");
    Require(Near(report.slow[0].Milliseconds(report.slow[0].gap_ticks),1000) && Near(report.slow[0].Milliseconds(report.slow[0].device_ticks),10),"gap remains distinguishable from engine work");
    Frame(tracker,3,start+Hz*2,Hz,false);
    Frame(tracker,4,start+Hz*10,100000);
    Require(!tracker.TakeReport(report) && report.samples==1,"diagnostic/menu frame breaks continuity and does not add false hitch");
    tracker.Gap();Frame(tracker,5,start+Hz*100,100000);
    Require(!tracker.TakeReport(report),"loading/inactive gap does not contaminate resumed frame");
}
void ExactThresholdAndBound(){
    Tracker tracker;int64_t start=1;
    Frame(tracker,1,start,Hz/20-1);start+=Hz;
    tracker.Gap();Frame(tracker,2,start,Hz/20);
    Report report;Require(tracker.TakeReport(report) && report.slow_count==1 && report.slow[0].frame==2,"50-ms threshold is exact to the tick");
    for(uint32_t i=0;i<1000;++i){tracker.Gap();start+=Hz;Frame(tracker,10+i,start,Hz/20+i*10);}
    Require(tracker.TakeReport(report) && report.samples==1000 && report.slow_count==1000 && report.stored==Report::Capacity,"many slow frames have fixed bounded storage and exact counts");
    for(unsigned i=0;i<Report::Capacity;++i)Require(report.slow[i].frame==1009-i,"four worst frames retained in descending order");
    // Equal-duration frames and an extra fast frame cannot overrun or displace
    // the bounded history. Frame frequency is constant throughout a window.
    for(uint32_t i=0;i<10;++i){tracker.Gap();start+=Hz;Frame(tracker,i,start,Hz/20);}
    tracker.Gap();start+=Hz;Frame(tracker,11,start,Hz/100);
    Require(tracker.TakeReport(report) && report.stored==4 && report.slow_count==10 && report.samples==11,"ties and ordinary frames preserve history/counts");
}
void InvalidLifetimes(){
    Tracker tracker;Report report;
    tracker.Begin(1,1,Hz);tracker.MarkEligible(2,true);tracker.End(1,Hz);
    Require(!tracker.TakeReport(report),"unmatched presenter frame cannot mark current sample eligible");
    tracker.Begin(2,Hz,Hz);tracker.MarkEligible(2,true);
    tracker.Span(1,FrameMove,Hz);tracker.Span(2,FrameMove,-1);tracker.End(2,Hz+Hz/10);
    Require(tracker.TakeReport(report) && report.slow[0].measured==0,"stale/negative spans rejected");
    tracker.Begin(3,Hz*2,Hz);tracker.MarkEligible(3,true);tracker.End(4,Hz*3);
    Require(!tracker.TakeReport(report),"mismatched end invalidates current frame");
    tracker.Begin(4,Hz*4,Hz);tracker.MarkEligible(4,true);tracker.End(4,Hz*3);
    Require(!tracker.TakeReport(report),"clock regression never becomes a huge unsigned hitch");
    Frame(tracker,5,Hz*5,Hz/10);tracker.Reset();
    Require(!tracker.TakeReport(report),"level/device reset clears old pending frame data");
    tracker.Begin(6,Hz*6,Hz);tracker.MarkEligible(6,true);
    tracker.Begin(7,Hz*7,Hz);tracker.MarkEligible(7,true);tracker.End(7,Hz*7+100000);
    Require(!tracker.TakeReport(report),"unfinished interrupted frame does not pollute next frame");
    Frame(tracker,8,Hz*8,Hz/10);
    tracker.Begin(9,Hz*9,Hz*2);tracker.MarkEligible(9,true);tracker.End(9,Hz*9+100000);
    Require(!tracker.TakeReport(report) && report.samples==1,"frequency change resets earlier timebase and continuity");
    tracker.Begin(10,0,Hz);tracker.MarkEligible(10,true);tracker.End(10,Hz*10);
    Require(!tracker.TakeReport(report),"invalid clock cannot produce an event");
}
void OrdinaryFrameAggregates(){
    Tracker tracker;const int64_t start=180000000000LL;
    const int64_t durations[]={100000,166666,166667,333333,333334,499999,500000};
    int64_t total=0;
    for(unsigned i=0;i<sizeof(durations)/sizeof(durations[0]);++i){
        tracker.Gap();tracker.Begin(i+1,start+i*Hz,Hz);
        tracker.Span(i+1,FrameMove,10000);tracker.Span(i+1,Render,durations[i]-10000);
        tracker.MarkEligible(i+1,true);tracker.End(i+1,start+i*Hz+durations[i]);total+=durations[i];
    }
    Report report;Require(tracker.TakeReport(report),"aggregate retains a threshold slow frame");
    Require(report.samples==7 && report.frequency==Hz,"aggregate includes fast and slow completed frames");
    Require(report.total_cycle_ticks==total && report.total_device_ticks==total && !report.total_gap_ticks,"aggregate sums exact integer durations without nested spans");
    Require(report.cycle_min_ticks==100000 && report.cycle_max_ticks==500000,"aggregate preserves minimum and maximum frame durations");
    Require(report.at_least_60hz==5 && report.at_least_30hz==3 && report.at_least_20hz==1,"integer threshold bins handle both sides of 60/30/20Hz exactly");
    Require(report.phase_ticks[FrameMove]==70000 && report.phase_ticks[Render]==total-70000,"phase aggregate includes ordinary frames below hitch threshold");
    Require(report.phase_max_ticks[FrameMove]==10000 && report.phase_max_ticks[Render]==490000,"phase maxima retained independently");
    Require(report.phase_samples[FrameMove]==7 && !report.phase_samples[Present],"unmeasured phase stays distinct from zero-cost phase");
    tracker.Gap();Frame(tracker,20,start+20*Hz,Hz/40);tracker.Gap();Frame(tracker,21,start+21*Hz,Hz/40);
    Require(!tracker.TakeReport(report) && report.samples==2 && report.total_cycle_ticks==Hz/20,"TakeReport yields steady 40FPS aggregate even with no >50ms hitch");
    Require(!tracker.TakeReport(report) && !report.samples && !report.total_cycle_ticks,"draining clears aggregate counters");
}
void TranslationUnits(){
    Require(PhasePeer()==&Global(),"device/presenter translation units share one EXE tracker");
    Global().Reset();Global().Begin(19,1,Hz);PhasePeerMark(19);Global().End(19,Hz);
    Report report;Require(Global().TakeReport(report) && report.slow[0].frame==19,"presenter eligibility mark completes device-owned frame");
    Global().Reset();
}
}
int main(){try{
#if defined(KICH_TEST_X87)
    fpu_control_t saved;_FPU_GETCW(saved);fpu_control_t single=(saved&~_FPU_EXTENDED)|_FPU_SINGLE;_FPU_SETCW(single);
#endif
    LongClockAndPhases();InterFrameAndEligibility();ExactThresholdAndBound();InvalidLifetimes();OrdinaryFrameAggregates();TranslationUnits();
#if defined(KICH_TEST_X87)
    _FPU_SETCW(saved);
#endif
    std::printf("E1.6 CPU phase attribution: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
