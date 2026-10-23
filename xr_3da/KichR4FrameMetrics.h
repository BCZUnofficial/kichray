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
    double times[120],previous;unsigned count,next,frames;
    Window():previous(0),count(0),next(0),frames(0){}
    void Gap(){previous=0;}
    void Add(double end_ms,bool eligible){
        if(!eligible || !std::isfinite(end_ms)){Gap();return;}
        ++frames;
        if(previous>0 && end_ms>previous){times[next]=end_ms-previous;next=(next+1)%120;if(count<120)++count;}
        previous=end_ms;
    }
    double Low()const{if(!count)return 0;double sorted[120];std::copy(times,times+count,sorted);std::sort(sorted,sorted+count);
        const unsigned slow=(count+99)/100;double total=0;for(unsigned i=count-slow;i<count;++i)total+=sorted[i];return 1000.0*slow/total;}
};
}
#endif
