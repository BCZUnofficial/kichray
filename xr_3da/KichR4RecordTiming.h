#ifndef KICH_R4_RECORD_TIMING_H
#define KICH_R4_RECORD_TIMING_H
#include "KichR4Telemetry.h"

// EXE-owned routing for optional asynchronous GPU results. Numeric tokens are
// not ownership flags: all completion fields must match a retained request.
namespace kich_r4_record {
struct PendingTiming {
    KichR4TelemetryContext context;
    uint64_t record_epoch; // zero belongs to an explicit snapshot
    PendingTiming():context(),record_epoch(0){}
};
struct TimingQueue {
    enum { Capacity=16 };
    PendingTiming pending[Capacity];
    unsigned count;
private:
    uint64_t token_counter;
public:
    explicit TimingQueue(uint64_t sequence=0):count(0),token_counter(sequence&UINT64_C(0x7fffffffffffffff)){}
    bool Contains(uint64_t token)const{
        for(unsigned i=0;i<count;++i)if(pending[i].context.capture_id==token)return true;
        return false;
    }
    bool Add(const KichR4TelemetryContext& context,uint64_t record_epoch){
        if(count==Capacity || context.struct_size!=sizeof(context) || !context.capture_id ||
            (context.flags&~(KICH_R4_TELEMETRY_DETAILED|KICH_R4_TELEMETRY_MENU)) || Contains(context.capture_id))return false;
        pending[count].context=context;pending[count].record_epoch=record_epoch;++count;return true;
    }
    unsigned Find(const KichR4TelemetryTiming& result)const{
        for(unsigned i=0;i<count;++i){const KichR4TelemetryContext& p=pending[i].context;
            if(p.capture_id==result.capture_id && p.engine_frame==result.engine_frame && p.render_frame==result.render_frame)return i;
        }
        return count;
    }
    bool Erase(unsigned index){
        if(index>=count)return false;
        for(unsigned i=index+1;i<count;++i)pending[i-1]=pending[i];
        pending[--count]=PendingTiming();return true;
    }
    unsigned DropRecord(){
        const unsigned before=count;unsigned kept=0;
        for(unsigned i=0;i<before;++i)if(!pending[i].record_epoch)pending[kept++]=pending[i];
        count=kept;for(unsigned i=kept;i<before;++i)pending[i]=PendingTiming();return before-kept;
    }
    // The caller reports cancellation to each owner before clearing. Neither
    // this operation nor dropping a record session touches GPU/resource state.
    void Clear(){for(unsigned i=0;i<count;++i)pending[i]=PendingTiming();count=0;}
    uint64_t NextToken(){
        // At most sixteen IDs are retained, so seventeen candidates suffice
        // even when a snapshot has legitimately reached the high-bit range.
        for(unsigned attempt=0;attempt<=Capacity;++attempt){
            token_counter=(token_counter+1)&UINT64_C(0x7fffffffffffffff);
            if(!token_counter)token_counter=1;
            const uint64_t token=UINT64_C(0x8000000000000000)|token_counter;
            if(!Contains(token))return token;
        }
        return 0; // defensive only; a well-formed fixed queue cannot exhaust IDs
    }
};
}
#endif
