#ifndef KICH_R4_FRAME_PHASES_H
#define KICH_R4_FRAME_PHASES_H
#include <stdint.h>
#if defined(_WIN32)
#include <windows.h>
#endif

// EXE-only CPU wall-clock attribution. Render contains NativePrelude/Submit,
// Present and PostPresent; these are nested spans, not additive GPU timings.
namespace kich_r4_phases {
enum Phase { FrameMove, Render, WorkerWait, WorkerFallback, NativePrelude,
    NativeSubmit, Present, PostPresent, PhaseCount };
struct Sample {
    uint32_t frame, measured;
    int64_t frequency, device_ticks, gap_ticks, ticks[PhaseCount];
    Sample():frame(0),measured(0),frequency(0),device_ticks(0),gap_ticks(0){
        for(unsigned i=0;i<PhaseCount;++i)ticks[i]=0;
    }
    int64_t CycleTicks()const{return device_ticks+gap_ticks;}
    double Milliseconds(int64_t value)const{return frequency>0?double(value)*1000.0/double(frequency):0;}
    double PhaseMS(Phase phase)const{return Milliseconds(ticks[phase]);}
};
struct Report {
    // Keep the four longest completed slow frames. Overflow is counted, never
    // allocated. A report is drained only at the present metrics' 2-second gate.
    enum { Capacity=4 };
    Sample slow[Capacity];
    uint32_t samples, slow_count, stored;
    Report():samples(0),slow_count(0),stored(0){}
};
class Tracker {
    Sample current;
    Report pending;
    int64_t started, previous_end, frequency;
    bool running, eligible;
public:
    Tracker():started(0),previous_end(0),frequency(0),running(false),eligible(false){}
    void Gap(){running=eligible=false;started=previous_end=0;}
    void Reset(){Gap();frequency=0;pending=Report();}
    void Begin(uint32_t frame,int64_t now,int64_t ticks_per_second){
        if(now<=0 || ticks_per_second<=0){Gap();return;}
        if(frequency && frequency!=ticks_per_second)Reset();
        if(running || now<previous_end)Gap();
        frequency=ticks_per_second;current=Sample();current.frame=frame;current.frequency=frequency;
        current.gap_ticks=previous_end?now-previous_end:0;
        started=now;running=true;eligible=false;
    }
    void MarkEligible(uint32_t frame,bool value){if(running && current.frame==frame)eligible=value;}
    void Span(uint32_t frame,Phase phase,int64_t ticks){
        if(!running || current.frame!=frame || unsigned(phase)>=PhaseCount || ticks<0)return;
        current.ticks[phase]+=ticks;current.measured|=uint32_t(1)<<phase;
    }
    void End(uint32_t frame,int64_t now){
        if(!running || current.frame!=frame || now<started){Gap();return;}
        running=false;
        if(!eligible){previous_end=0;return;}
        current.device_ticks=now-started;previous_end=now;++pending.samples;
        // ceil(frequency/20) is an exact 50-ms gate even with Win32 x87 PC_24.
        const int64_t threshold=frequency/20+(frequency%20!=0);
        if(current.CycleTicks()<threshold)return;
        ++pending.slow_count;
        unsigned slot=0;
        while(slot<pending.stored && pending.slow[slot].CycleTicks()>=current.CycleTicks())++slot;
        if(slot==Report::Capacity)return;
        if(pending.stored<Report::Capacity)++pending.stored;
        for(unsigned i=pending.stored-1;i>slot;--i)pending.slow[i]=pending.slow[i-1];
        pending.slow[slot]=current;
    }
    bool TakeReport(Report& report){
        report=pending;pending=Report();return report.slow_count!=0;
    }
};
// Both callers run on the main thread in XR_3DA.exe. This inline local static is
// one COMDAT-backed instance across its TUs; no renderer/game DLL ABI is changed.
inline Tracker& Global(){static Tracker tracker;return tracker;}
#if defined(_WIN32)
inline int64_t Counter(){LARGE_INTEGER value;return QueryPerformanceCounter(&value)?value.QuadPart:0;}
inline int64_t Frequency(){static const int64_t frequency=[]()->int64_t{
    LARGE_INTEGER value;return QueryPerformanceFrequency(&value)?value.QuadPart:0;
}();return frequency;}
#endif
}
#endif
