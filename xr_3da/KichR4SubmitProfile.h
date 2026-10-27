#ifndef KICH_R4_SUBMIT_PROFILE_H
#define KICH_R4_SUBMIT_PROFILE_H
/* P0-S11 PERF02: the native submit (KichR4_NativePresent from its start to
 * present) split into consecutive CPU stages. Plan and preflight come from the
 * presenter; Draw's stages are laps between its phase boundaries (the GPU
 * Mark points), so they are disjoint and add up with "other" to the submit.
 * Only measured frames (log_record or a snapshot) read the clock. */
#include "KichR4FrameMetrics.h"
#include <stdint.h>
#include <string.h>
namespace kich_r4_submit {
enum Stage {Plan,Preflight,Setup,SunNear,SunFar,SunContact,LocalShadows,GBuffer,Lighting,Combine,Forward,Post,Other,StageCount};
inline const char* StageName(unsigned s){static const char* names[StageCount]={"plan","preflight","setup","sun_128m","sun_512m","sun_contact","local_shadows",
    "gbuffer","lighting","combine","forward","post_hud","other"};return s<StageCount?names[s]:"?";}
enum Counter {SunFarRendered,SunFarReused,LocalFaces,CounterCount};
inline const char* CounterName(unsigned c){static const char* names[CounterCount]={"sun_512m_rendered","sun_512m_reused","local_faces"};return c<CounterCount?names[c]:"?";}
struct Profile {
    double frame[StageCount],sum[StageCount],peak[StageCount];uint64_t count[CounterCount],count_sum[CounterCount];
    unsigned frames;bool measuring;
    Profile(){Reset();measuring=false;memset(frame,0,sizeof(frame));memset(count,0,sizeof(count));}
    void Reset(){memset(sum,0,sizeof(sum));memset(peak,0,sizeof(peak));memset(count_sum,0,sizeof(count_sum));frames=0;}
    void Begin(bool measured){measuring=measured;memset(frame,0,sizeof(frame));memset(count,0,sizeof(count));}
    void Add(Stage s,double ms){if(measuring)frame[s]+=ms;}
    void Count(Counter c,uint64_t n=1){if(measuring)count[c]+=n;}
    // total_ms: the whole native submit; its unattributed rest becomes Other.
    void Finish(double total_ms){if(!measuring)return;measuring=false;
        double known=0;for(unsigned s=0;s<Other;++s)known+=frame[s];frame[Other]=total_ms>known?total_ms-known:0;
        ++frames;for(unsigned s=0;s<StageCount;++s){sum[s]+=frame[s];if(frame[s]>peak[s])peak[s]=frame[s];}
        for(unsigned c=0;c<CounterCount;++c)count_sum[c]+=count[c];}
    // A frame that did not reach present is dropped from the window.
    void Cancel(){measuring=false;}
};
inline Profile& Global(){static Profile profile;return profile;}
// Consecutive laps: each Lap gives the time since the previous one to a stage.
struct Laps {
    kich_r4_metrics::Stamp stamp;
    Laps():stamp(Global().measuring){}
    void Lap(Stage s){if(!stamp.measured)return;const kich_r4_metrics::Clock::time_point now=kich_r4_metrics::Clock::now();
        Global().Add(s,std::chrono::duration<double,std::milli>(now-stamp.started).count());stamp.started=now;}
};
}
#endif
