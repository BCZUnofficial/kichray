#ifndef KICH_R4_RECORD_CONTROL_H
#define KICH_R4_RECORD_CONTROL_H
#include <stdint.h>

// EXE-only, main render thread. Console requests take effect at a frame
// boundary; changing diagnostics must never change render selection policy.
namespace kich_r4_record {
struct Control {
    bool requested,enabled;
    uint64_t epoch;
    uint32_t frame;
    Control():requested(false),enabled(false),epoch(0),frame(0){}
};
inline Control& Switch(){static Control control;return control;}
inline bool Enabled(){return Switch().enabled;}
inline bool Requested(){return Switch().requested;}
inline uint64_t Epoch(){return Switch().epoch;}
inline void Request(bool value){Switch().requested=value;}
inline bool BeginFrame(uint32_t frame){
    Control& c=Switch();c.frame=frame;
    if(c.enabled==c.requested)return false;
    c.enabled=c.requested;++c.epoch;return true;
}
inline bool Measure(bool snapshot=false){return Enabled() || snapshot;}
}

// Implemented in the native EXE presenter. Commands do no GPU work.
void KichR4_RecordSet(bool enabled);
void KichR4_RecordMark(const char* label);
void KichR4_RecordWatch(bool enabled);
void KichR4_RecordView(unsigned view);
unsigned KichR4_RecordView();
#endif
