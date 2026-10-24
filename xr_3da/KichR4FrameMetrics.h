#ifndef KICH_R4_FRAME_METRICS_H
#define KICH_R4_FRAME_METRICS_H
#include <algorithm>
#include <chrono>
#include <cmath>
namespace kich_r4_metrics {
typedef std::chrono::steady_clock Clock;
inline double Elapsed(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
// Present-to-present wall time, including game work and vsync. No GPU/CPU-frame correlation implied.
struct Window {
    double times[120],previous,interval,report_started,report_ms;
    unsigned count,next,frames,report_count;
    Window():previous(0),interval(0),report_started(0),report_ms(0),count(0),next(0),frames(0),report_count(0){}
    void Gap(){previous=interval=report_started=report_ms=0;report_count=0;}
    void Add(double end_ms,bool eligible){
        if(!eligible || !std::isfinite(end_ms) || end_ms<=0){Gap();return;}
        if(previous>0 && end_ms<=previous)Gap();
        ++frames;
        if(!report_started)report_started=end_ms;
        if(previous>0){interval=end_ms-previous;times[next]=interval;next=(next+1)%120;if(count<120)++count;
            report_ms+=interval;++report_count;}
        previous=end_ms;
    }
    double Current()const{return interval>0?1000.0/interval:0;}
    double Recent()const{return report_ms>0?1000.0*report_count/report_ms:0;}
    // Reuse the present timestamp: no timer call, allocation or GPU readback.
    bool ReportDue()const{return report_count && previous-report_started>=2000.0;}
    void Reported(){report_started=previous;report_ms=0;report_count=0;}
    double Low()const{if(!count)return 0;double sorted[120];std::copy(times,times+count,sorted);std::sort(sorted,sorted+count);
        const unsigned slow=(count+99)/100;double total=0;for(unsigned i=count-slow;i<count;++i)total+=sorted[i];return 1000.0*slow/total;}
};
}
#endif
