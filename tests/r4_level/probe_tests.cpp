#include "../../xr_3da/KichR4FrameMetrics.h"
#include <cstdio>
#include <stdexcept>

namespace {
unsigned checks=0;
void Require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
struct Clock {
    typedef std::chrono::nanoseconds duration;
    typedef std::chrono::time_point<Clock> time_point;
    static unsigned calls;
    static time_point now(){return time_point(duration(++calls*1000000));}
};
unsigned Clock::calls=0;
void GatedClock(){
    using namespace kich_r4_metrics;
    kich_r4_record::Request(false);kich_r4_record::BeginFrame(1);
    Require(!Start().measured,"ordinary probes start disabled");
    Measurement<Clock> off(kich_r4_record::Measure());
    Require(Clock::calls==0 && Elapsed(off)==0 && Clock::calls==0,"OFF has no clock reads at either endpoint");
    kich_r4_record::Request(true);
    Require(!Start().measured,"console request does not enable half a frame");
    kich_r4_record::BeginFrame(2);const uint64_t epoch=kich_r4_record::Epoch();
    Measurement<Clock> on(kich_r4_record::Measure());
    Require(Clock::calls==1 && Elapsed(on)==1 && Clock::calls==2,"ON measures only its own elapsed scope");
    kich_r4_record::Request(false);kich_r4_record::BeginFrame(3);
    Require(kich_r4_record::Epoch()!=epoch && !Start().measured,"new OFF epoch immediately stops new clocks");
    Require(Elapsed(off)==0 && Clock::calls==2,"late evaluation cannot activate an originally disabled stamp");
    Measurement<Clock> capture(kich_r4_record::Measure(true));
    Require(Clock::calls==3 && Elapsed(capture)==1 && Clock::calls==4 && Start(true).measured,"explicit snapshots remain measurable when ordinary recording is OFF");
}
}
int main(){try{GatedClock();printf("E1.7 gated CPU probes: %u checks passed\n",checks);return 0;}
    catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
