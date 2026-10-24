#include "../../xr_3da/KichR4FramePhases.h"
// Separate translation unit represents the no-PCH presenter. Its eligibility
// mark must reach the same EXE-local tracker as the engine device caller.
kich_r4_phases::Tracker* PhasePeer(){return &kich_r4_phases::Global();}
void PhasePeerMark(uint32_t frame){kich_r4_phases::Global().MarkEligible(frame,true);}
