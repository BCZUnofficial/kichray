#include "../../xr_3da/KichR4FrameMetrics.h"
#include <cstdio>
#include <stdexcept>
#include <limits>
#if defined(KICH_TEST_X87)
#include <fpu_control.h>
#endif

namespace {
unsigned checks=0;
void Require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
bool Near(double a,double b,double tolerance=.00002){return std::fabs(a-b)<=tolerance;}

void LongUptime(){
    const int64_t frequency=10000000,step=156789;
    const int64_t starts[]={1,180000000000LL,1000000000000000000LL,
        (std::numeric_limits<int64_t>::max)()-frequency*10};
    for(unsigned sample=0;sample<sizeof(starts)/sizeof(starts[0]);++sample){
        kich_r4_metrics::Window window;window.Add(starts[sample],frequency,true);
        for(unsigned frame=1;frame<=160;++frame){
            window.Add(starts[sample]+step*frame,frequency,true);
            Require(Near(window.interval,15.6789),"frame interval must retain sub-ms precision at long QPC uptime");
            Require(Near(window.Current(),63.77998456524),"FPS cannot alternate with absolute-clock rounding");
            Require(Near(window.Recent(),63.77998456524),"recent FPS must use exact accumulated tick span");
            Require(window.ReportDue()==(step*frame>=frequency*2),"two-second report gate must use integral ticks");
        }
        Require(window.count==120 && Near(window.Low(),63.77998456524),"slow-frame ring remains bounded and accurate");
    }
}

void GateAndGaps(){
    kich_r4_metrics::Window window;
    const int64_t frequency=10000000,start=180000000000LL;
    window.Add(start,frequency,true);
    window.Add(start+frequency*2-1,frequency,true);
    Require(!window.ReportDue(),"rounded report_ms must not trigger one tick early");
    window.Add(start+frequency*2,frequency,true);
    Require(window.ReportDue() && window.report_count==2,"report becomes due on exact two-second tick");
    window.Reported();
    Require(window.report_count==0 && window.report_ms==0 && !window.ReportDue(),"reported window starts fresh without dropping current interval");
    window.Add(start+frequency*4,frequency,true);
    Require(window.ReportDue() && window.report_count==1 && Near(window.Current(),.5),"stalled frame is measured and reportable");
    const unsigned kept=window.count;
    window.Add(start+frequency*5,frequency,false);
    window.Add(start+frequency*6,frequency,true);
    Require(window.count==kept && window.Current()==0 && window.Recent()==0,"diagnostic and resume frames cannot contaminate gameplay intervals");
    window.Add(start+frequency*6+100000,frequency,true);
    Require(Near(window.Current(),100),"gameplay resumes with its first complete interval");
    window.Add(start+frequency*7,frequency*2,true);
    Require(window.Current()==0 && window.report_count==0,"clock frequency changes reset timestamp continuity");
    window.Add(start,frequency,true);
    Require(window.Current()==0 && window.report_count==0,"clock regression resets timestamp continuity");
    window.Add(0,frequency,true);
    Require(window.previous==0 && window.frequency==0,"zero timestamp rejected");
    window.Add(-1,frequency,true);
    Require(window.previous==0,"negative timestamp rejected");
    window.Add(start,0,true);
    Require(window.previous==0,"zero frequency rejected");
    window.Add(start,-frequency,true);
    Require(window.previous==0,"negative frequency rejected");
    // A large frequency must not overflow the integral two-second threshold.
    window.Add(1,(std::numeric_limits<int64_t>::max)(),true);
    window.Add((std::numeric_limits<int64_t>::max)(),(std::numeric_limits<int64_t>::max)(),true);
    Require(!window.ReportDue(),"large positive frequencies cannot wrap the report threshold");
}

#if defined(KICH_TEST_X87)
struct Precision24 {
    fpu_control_t saved;
    Precision24(){_FPU_GETCW(saved);fpu_control_t single=(saved&~_FPU_EXTENDED)|_FPU_SINGLE;_FPU_SETCW(single);}
    ~Precision24(){_FPU_SETCW(saved);}
};
__attribute__((noinline)) double OldAbsolute(int64_t ticks,int64_t frequency){return double(ticks)*1000.0/double(frequency);}
void ReproduceOldFailure(){
    const int64_t frequency=10000000,start=180000000000LL;
    volatile double first=OldAbsolute(start,frequency),second=OldAbsolute(start+156789,frequency);
    Require(std::fabs(second-first-15.6789)>.5,"x87 fixture must reproduce the old absolute-timestamp loss");
}
#endif
}

int main(){try{
#if defined(KICH_TEST_X87)
    Precision24 precision;ReproduceOldFailure();
#endif
    LongUptime();GateAndGaps();
    std::printf("E1.5 integer present clock: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
