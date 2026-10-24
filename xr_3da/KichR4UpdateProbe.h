#ifndef KICH_R4_UPDATE_PROBE_H
#define KICH_R4_UPDATE_PROBE_H
#include <stdint.h>
#include <limits.h>
#include "KichR4FramePhases.h"

// Main-thread EXE probes, separate from the legacy CStatTimer collection switch.
// No allocation, RTTI lookup, timer read or output is needed with log_record=0.
namespace kich_r4_update {
enum Phase { ApplicationEvents, SpatialUpdate, SoundDispatch, ObjectsUpdate,
    HUDUpdate, SchedulerUpdate, EnvironmentUpdate, ParticleLifecycle, PhaseCount };
inline const char* PhaseName(unsigned phase){
    const char* names[]={"application_events","spatial_update","sound_dispatch","objects_update",
        "hud_update","scheduler_update","environment_update","particle_lifecycle"};
    return phase<PhaseCount?names[phase]:"unknown";
}
struct Sample {
    uint32_t frame,ordinal,mode;
    bool completed,name_truncated;
    int64_t ticks,phases[PhaseCount];
    char type[80];
    Sample():frame(0),ordinal(0),mode(0),completed(false),name_truncated(false),ticks(0){
        type[0]=0;for(unsigned i=0;i<PhaseCount;++i)phases[i]=0;
    }
    void Name(const char* name){
        if(!name || !*name)name="unknown_frame_callback";
        unsigned i=0;
        for(;name[i] && i+1<sizeof(type);++i){
            const unsigned char c=static_cast<unsigned char>(name[i]);
            type[i]=(c>32 && c<127 && c!=';' && c!='[' && c!=']')?char(c):'_';
        }
        type[i]=0;name_truncated=name[i]!=0;
    }
};
struct PhaseReport {
    uint64_t calls;
    int64_t ticks,max_ticks;
    uint32_t max_frame;
    PhaseReport():calls(0),ticks(0),max_ticks(0),max_frame(0){}
};
struct Report {
    enum { Capacity=4 };
    uint64_t frames,callbacks,long_callbacks,interrupted,nested_callbacks,nested_frames,clock_failures,saturated_totals;
    int64_t frequency,total_ticks,max_ticks;
    unsigned stored;
    Sample slow[Capacity];
    PhaseReport phases[PhaseCount];
    Report():frames(0),callbacks(0),long_callbacks(0),interrupted(0),nested_callbacks(0),nested_frames(0),
        clock_failures(0),saturated_totals(0),frequency(0),total_ticks(0),max_ticks(0),stored(0){}
    double MS(int64_t ticks)const{return frequency>0?double(ticks)*1000.0/double(frequency):0;}
    void AddTicks(int64_t& total,int64_t value){
        if(value>INT64_MAX-total){total=INT64_MAX;++saturated_totals;}
        else total+=value;
    }
    void Add(const Sample& sample){
        ++callbacks;if(!sample.completed)++interrupted;
        AddTicks(total_ticks,sample.ticks);if(sample.ticks>max_ticks)max_ticks=sample.ticks;
        const int64_t threshold=frequency/200+(frequency%200!=0); // exact ceil(5ms), including x87 PC_24
        if(sample.ticks<threshold)return;
        ++long_callbacks;unsigned slot=0;
        while(slot<stored && slow[slot].ticks>=sample.ticks)++slot;
        if(slot==Capacity)return;
        if(stored<Capacity)++stored;
        for(unsigned i=stored-1;i>slot;--i)slow[i]=slow[i-1];
        slow[slot]=sample;
    }
};
class Tracker {
    Report pending;
    uint64_t epoch,token;
    uint32_t frame,mode,ordinal,callback_depth,phase_depth[PhaseCount];
    bool running;
    int64_t frequency,callback_phases[PhaseCount];
public:
    Tracker():epoch(0),token(0),frame(0),mode(0),ordinal(0),callback_depth(0),running(false),frequency(0){ClearPhases();}
    void ClearPhases(){for(unsigned i=0;i<PhaseCount;++i){phase_depth[i]=0;callback_phases[i]=0;}}
    void Reset(uint64_t next_epoch){
        epoch=next_epoch;++token;pending=Report();running=false;callback_depth=ordinal=0;frequency=0;ClearPhases();
    }
    void Disabled(uint64_t next_epoch){if(epoch!=next_epoch)Reset(next_epoch);}
    bool Begin(uint64_t next_epoch,uint32_t next_frame,uint32_t next_mode,int64_t ticks_per_second){
        if(running){++pending.nested_frames;return false;}
        if(epoch!=next_epoch || (frequency && frequency!=ticks_per_second))Reset(next_epoch);
        if(ticks_per_second<=0){++pending.clock_failures;return false;}
        frame=next_frame;mode=next_mode;frequency=ticks_per_second;pending.frequency=frequency;
        ordinal=callback_depth=0;ClearPhases();running=true;++token;++pending.frames;return true;
    }
    void End(){running=false;callback_depth=0;++token;ClearPhases();}
    bool Active()const{return running;}
    bool TopCallback()const{return running && callback_depth==0;}
    uint64_t Token()const{return token;}
    uint32_t EnterCallback(){
        if(!running)return 0;
        if(callback_depth++){++pending.nested_callbacks;return 0;}
        for(unsigned i=0;i<PhaseCount;++i)callback_phases[i]=0;
        return ++ordinal;
    }
    void LeaveCallback(uint64_t ticket,Sample* sample,int64_t started,int64_t ended){
        if(ticket!=token || !running || !callback_depth)return;
        --callback_depth;if(!sample)return;
        if(started<0 || ended<started){++pending.clock_failures;return;}
        sample->frame=frame;sample->mode=mode;sample->ticks=ended-started;
        for(unsigned i=0;i<PhaseCount;++i)sample->phases[i]=callback_phases[i];
        pending.Add(*sample);
    }
    bool EnterPhase(unsigned phase){return running && callback_depth && phase<PhaseCount && phase_depth[phase]++==0;}
    void LeavePhase(uint64_t ticket,unsigned phase,bool measured,int64_t started,int64_t ended){
        if(ticket!=token || !running || phase>=PhaseCount || !phase_depth[phase])return;
        --phase_depth[phase];if(!measured)return;
        if(started<0 || ended<started){++pending.clock_failures;return;}
        const int64_t ticks=ended-started;PhaseReport& p=pending.phases[phase];++p.calls;
        pending.AddTicks(p.ticks,ticks);pending.AddTicks(callback_phases[phase],ticks);
        if(ticks>p.max_ticks){p.max_ticks=ticks;p.max_frame=frame;}
    }
    void Take(Report& report){report=pending;pending=Report();pending.frequency=frequency;}
};
inline Tracker& Global(){static Tracker tracker;return tracker;}
// Scope hooks in engine functions may also be reached by a worker. Only the
// thread currently inside measured FrameMove owns a context; no shared writes.
inline Tracker*& Current(){static thread_local Tracker* current=0;return current;}
struct Clock {
    static int64_t Now(){
#if defined(_WIN32)
        LARGE_INTEGER value;return QueryPerformanceCounter(&value)?value.QuadPart:-1;
#else
        return -1;
#endif
    }
    static int64_t Frequency(){
#if defined(_WIN32)
        return kich_r4_phases::Frequency();
#else
        return 0;
#endif
    }
};
template<class Time=Clock> class FrameScope {
    Tracker* owner;
public:
    FrameScope(bool enabled,uint64_t epoch,uint32_t frame,uint32_t mode):owner(0){
        if(!enabled){if(!Current())Global().Disabled(epoch);return;}
        if(Current()){Current()->Begin(epoch,frame,mode,1);return;} // count reentry, no timer
        Tracker& tracker=Global();
        if(tracker.Begin(epoch,frame,mode,Time::Frequency())){owner=&tracker;Current()=owner;}
    }
    ~FrameScope(){if(owner){Current()=0;owner->End();}}
    bool Active()const{return owner!=0;}
private:FrameScope(const FrameScope&);FrameScope& operator=(const FrameScope&);
};
template<class Time=Clock> class CallbackScope {
    Tracker* tracker;
    uint64_t token;
    int64_t started;
    bool measured,complete;
    Sample sample;
public:
    explicit CallbackScope(const char* type):tracker(Current()),token(0),started(0),measured(false),complete(false){
        if(!tracker)return;
        token=tracker->Token();sample.ordinal=tracker->EnterCallback();measured=sample.ordinal!=0;
        if(measured){sample.Name(type);started=Time::Now();}
    }
    void Complete(){complete=true;}
    ~CallbackScope(){
        if(!tracker || tracker->Token()!=token || !tracker->Active())return;
        sample.completed=complete;
        tracker->LeaveCallback(token,measured?&sample:0,started,measured?Time::Now():0);
    }
private:CallbackScope(const CallbackScope&);CallbackScope& operator=(const CallbackScope&);
};
template<class Time=Clock> class Scope {
    Tracker* tracker;
    uint64_t token;
    unsigned phase;
    int64_t started;
    bool measured;
public:
    explicit Scope(Phase value):tracker(Current()),token(0),phase(value),started(0),measured(false){
        if(tracker){token=tracker->Token();measured=tracker->EnterPhase(phase);if(measured)started=Time::Now();}
    }
    ~Scope(){if(tracker && tracker->Token()==token && tracker->Active())
        tracker->LeavePhase(token,phase,measured,started,measured?Time::Now():0);}
private:Scope(const Scope&);Scope& operator=(const Scope&);
};
template<class Sink> void EmitReport(Sink& sink){
    Report report;Global().Take(report);
    sink.Emit("[R4 E1.8 updates] frames=%llu callbacks=%llu callback_total_ms=%.3f callback_max_ms=%.3f callbacks_ge_5ms=%llu retained=%u interrupted=%llu nested_callbacks=%llu nested_frames=%llu clock_failures=%llu saturated_totals=%llu; main_seqFrame_all_modes; mode_at_FrameMove_start; mode_bits_match_shadow_trace; bounded_top4; no_AI_policy_changes; nested_spans_not_additive",
        static_cast<unsigned long long>(report.frames),static_cast<unsigned long long>(report.callbacks),report.MS(report.total_ticks),report.MS(report.max_ticks),
        static_cast<unsigned long long>(report.long_callbacks),report.stored,static_cast<unsigned long long>(report.interrupted),
        static_cast<unsigned long long>(report.nested_callbacks),static_cast<unsigned long long>(report.nested_frames),
        static_cast<unsigned long long>(report.clock_failures),static_cast<unsigned long long>(report.saturated_totals));
    for(unsigned i=0;i<PhaseCount;++i){const PhaseReport& p=report.phases[i];if(p.calls)
        sink.Emit("[R4 E1.8 update_phase] name=%s calls=%llu total_ms=%.3f max_ms=%.3f max_frame=%u; main_seqFrame_nested_span",
            PhaseName(i),static_cast<unsigned long long>(p.calls),report.MS(p.ticks),report.MS(p.max_ticks),p.max_frame);}
    for(unsigned i=0;i<report.stored;++i){const Sample& p=report.slow[i];
        sink.Emit("[R4 E1.8 update_callback] frame=%u ordinal=%u mode=0x%X type=%s name_truncated=%u completed=%u wall_ms=%.3f application_events_ms=%.3f spatial_ms=%.3f sound_dispatch_ms=%.3f objects_ms=%.3f hud_ms=%.3f scheduler_ms=%.3f environment_ms=%.3f particles_ms=%.3f; compiler_RTTI_name_or_static_fallback; ordinal_is_not_persistent_identity; nested_spans_not_additive",
            p.frame,p.ordinal,p.mode,p.type,unsigned(p.name_truncated),unsigned(p.completed),report.MS(p.ticks),report.MS(p.phases[ApplicationEvents]),
            report.MS(p.phases[SpatialUpdate]),report.MS(p.phases[SoundDispatch]),report.MS(p.phases[ObjectsUpdate]),report.MS(p.phases[HUDUpdate]),
            report.MS(p.phases[SchedulerUpdate]),report.MS(p.phases[EnvironmentUpdate]),report.MS(p.phases[ParticleLifecycle]));}
}
}
#endif
