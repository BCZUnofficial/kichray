#ifndef KICH_R4_QUEUE_PROFILE_H
#define KICH_R4_QUEUE_PROFILE_H
/* P0-S11 PERF02: the CPU queue (KichR4_NativeQueueTime, from capture begin to
 * the native submit) split into its large stages and the repeated operations
 * inside them. Stages are wall times of whole scopes; Model/Stream are nested
 * in Submit/Environment and must not be added to them. Only measured frames
 * (log_record or a snapshot) read the clock; the window reports mean and max. */
#include "KichR4FrameMetrics.h"
#include <stdint.h>
#include <string.h>
namespace kich_r4_queue {
// DetailSync (the render thread inside MT_SYNC: waiting for the parallel
// detail calculation, or doing it) and DetailPrepare (publishing the parallel
// instance assembly, or assembling here) are nested in Details. DetailWorker
// is the assembly's own time on the parallel thread: not render-thread time.
enum Stage {Lights,Renderables,HudFirst,Submit,Details,Environment,Wallmarks,Glows,ModelCapture,StreamCapture,DetailSync,DetailPrepare,DetailWorker,StageCount};
inline const char* StageName(unsigned s){static const char* names[StageCount]={"lights","renderables","hud_first","submit","details","environment","wallmarks","glows","model_capture","stream_capture","detail_sync","detail_prepare","detail_worker"};return s<StageCount?names[s]:"?";}
enum Counter {Visuals,ModelCalls,ModelDuplicates,ModelBones,StreamDraws,StreamIndices,CounterCount};
inline const char* CounterName(unsigned c){static const char* names[CounterCount]={"visuals","model_calls","model_duplicates","model_bones","stream_draws","stream_indices"};return c<CounterCount?names[c]:"?";}
struct Profile {
    double frame[StageCount],sum[StageCount],peak[StageCount];uint64_t count[CounterCount],count_sum[CounterCount];
    unsigned frames;bool measuring;
    Profile(){Reset();measuring=false;memset(frame,0,sizeof(frame));memset(count,0,sizeof(count));}
    void Reset(){memset(sum,0,sizeof(sum));memset(peak,0,sizeof(peak));memset(count_sum,0,sizeof(count_sum));frames=0;}
    // Capture begin: this frame measures iff the recorder does.
    void Begin(bool measured){measuring=measured;finished=false;memset(frame,0,sizeof(frame));memset(peak_late,0,sizeof(peak_late));memset(count,0,sizeof(count));}
    void Add(Stage s,double ms){if(measuring)frame[s]+=ms;}
    void Count(Counter c,uint64_t n=1){if(measuring)count[c]+=n;}
    // Native submit: fold the finished queue into the report window.
    void Finish(){finished=measuring;if(!measuring)return;++frames;measuring=false;
        for(unsigned s=0;s<StageCount;++s){sum[s]+=frame[s];if(frame[s]>peak[s])peak[s]=frame[s];}
        for(unsigned c=0;c<CounterCount;++c)count_sum[c]+=count[c];}
    // Work of this frame done after the queue ended (inside the native submit),
    // added to the frame that was just folded. P0-S11: the deferred detail publish.
    void AddFinished(Stage s,double ms){if(!finished || !frames)return;sum[s]+=ms;peak_late[s]+=ms;if(peak_late[s]>peak[s])peak[s]=peak_late[s];}
    bool finished=false;double peak_late[StageCount]={};
};
inline Profile& Global(){static Profile profile;return profile;}
// One stage scope; nothing is read when the frame is not measured.
struct Scope {Stage stage;kich_r4_metrics::Stamp stamp;
    explicit Scope(Stage s):stage(s),stamp(Global().measuring){}
    ~Scope(){if(stamp.measured)Global().Add(stage,kich_r4_metrics::Elapsed(stamp));}};
}
#endif
