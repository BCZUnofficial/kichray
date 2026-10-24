#ifndef KICH_R4_FRAME_METRICS_H
#define KICH_R4_FRAME_METRICS_H
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdint.h>
namespace kich_r4_metrics {
typedef std::chrono::steady_clock Clock;
inline double Elapsed(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
// Present-to-present wall time, including game work and vsync. No GPU/CPU-frame correlation implied.
struct Window {
    double times[120],interval,report_ms;
    int64_t previous,report_started,frequency;
    unsigned count,next,frames,report_count;
    Window():interval(0),report_ms(0),previous(0),report_started(0),frequency(0),count(0),next(0),frames(0),report_count(0){}
    void Gap(){previous=report_started=frequency=0;interval=report_ms=0;report_count=0;}
    double Milliseconds(int64_t ticks)const{return double(ticks)*1000.0/double(frequency);}
    void Add(int64_t end_ticks,int64_t ticks_per_second,bool eligible){
        if(!eligible || end_ticks<=0 || ticks_per_second<=0){Gap();return;}
        if(previous>0 && (end_ticks<=previous || ticks_per_second!=frequency))Gap();
        frequency=ticks_per_second;
        ++frames;
        if(!report_started)report_started=end_ticks;
        // Win32 engine code uses x87 PC_24. Converting absolute uptime before
        // subtracting quantizes ordinary frame intervals to milliseconds.
        // Keep both elapsed clocks integral; convert only their small deltas.
        if(previous>0){interval=Milliseconds(end_ticks-previous);times[next]=interval;next=(next+1)%120;if(count<120)++count;
            report_ms=Milliseconds(end_ticks-report_started);++report_count;}
        previous=end_ticks;
    }
    double Current()const{return interval>0?1000.0/interval:0;}
    double Recent()const{return report_ms>0?1000.0*report_count/report_ms:0;}
    // Reuse the present timestamp: no timer call, allocation or GPU readback.
    bool ReportDue()const{return report_count && uint64_t(previous-report_started)>=uint64_t(frequency)*2u;}
    void Reported(){report_started=previous;report_ms=0;report_count=0;}
    double Low()const{if(!count)return 0;double sorted[120];std::copy(times,times+count,sorted);std::sort(sorted,sorted+count);
        const unsigned slow=(count+99)/100;double total=0;for(unsigned i=count-slow;i<count;++i)total+=sorted[i];return 1000.0*slow/total;}
};
}
#endif
