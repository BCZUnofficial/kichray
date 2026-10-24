#ifndef KICH_R4_SHADOW_POOL_POLICY_H
#define KICH_R4_SHADOW_POOL_POLICY_H
#include <stdint.h>

namespace kich_r4_shadow_pool {
// D32F payload only. Every selected point light retains all six faces.
static const uint64_t BudgetBytes=64ull*1024*1024;
static const uint32_t PreferredSize=512,MinimumSize=16;
static const uint32_t RecoveryDelayMs=2000,ObservationGapMs=1000;

inline bool ValidResolution(uint32_t size){
    return size>=MinimumSize && size<=PreferredSize && (size&(size-1))==0;
}
inline uint64_t Payload(uint32_t faces,uint32_t size){return uint64_t(faces)*size*size*4;}
inline bool Fits(uint32_t faces,uint32_t size){return ValidResolution(size) && Payload(faces,size)<=BudgetBytes;}
inline uint32_t BestFit(uint32_t faces,uint64_t budget=BudgetBytes){
    uint32_t size=PreferredSize;
    while(size>MinimumSize && Payload(faces,size)>budget)size/=2;
    return Payload(faces,size)<=budget?size:0;
}

// Allocation-free policy shared by the frontend and backend through the frozen
// lighting frame. Pressure acts immediately; recovery needs sustained headroom
// so crossing the 64-face boundary cannot recreate every map on alternate frames.
class ResolutionPolicy {
    uint32_t size,pending_size,pending_since,last_observation;
    bool pending,observed;
    uint32_t downgrades,recoveries;
    void CancelRecovery(){pending=false;pending_size=pending_since=0;}
public:
    explicit ResolutionPolicy(uint32_t initial=PreferredSize){Reset(initial);}
    void Reset(uint32_t initial=PreferredSize){
        size=ValidResolution(initial)?initial:PreferredSize;
        pending_size=pending_since=last_observation=0;pending=observed=false;
        downgrades=recoveries=0;
    }
    uint32_t Size()const{return size;}
    uint32_t Downgrades()const{return downgrades;}
    uint32_t Recoveries()const{return recoveries;}
    uint32_t PendingSize()const{return pending?pending_size:0;}
    uint32_t Select(uint32_t faces,uint32_t now_ms){
        // Unsigned subtraction supports Device.dwTimeGlobal wrapping. A pause,
        // clock discontinuity or missed observations cannot count as headroom.
        if(observed && uint32_t(now_ms-last_observation)>ObservationGapMs)CancelRecovery();
        last_observation=now_ms;observed=true;
        const uint32_t required=BestFit(faces);
        if(!required){CancelRecovery();return 0;}
        if(required<size){size=required;++downgrades;CancelRecovery();return size;}
        if(!faces){CancelRecovery();return size;}
        const uint32_t candidate=BestFit(faces,BudgetBytes*3/4);
        if(candidate<=size){CancelRecovery();return size;}
        if(!pending || pending_size!=candidate){pending=true;pending_size=candidate;pending_since=now_ms;}
        if(uint32_t(now_ms-pending_since)>=RecoveryDelayMs){
            size=candidate;++recoveries;CancelRecovery();
        }
        return size;
    }
};
}
#endif
