#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <new>
#include "xr_3da/KichR4RecordTiming.h"
static unsigned allocations=0;
void* operator new(size_t bytes){void* result=malloc(bytes?bytes:1);if(!result)throw std::bad_alloc();++allocations;return result;}
void operator delete(void* pointer)noexcept{free(pointer);}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete[](void* pointer)noexcept{free(pointer);}
using kich_r4_record::TimingQueue;
static KichR4TelemetryContext Context(uint64_t token,uint64_t engine=UINT64_C(9007199254740999),uint64_t render=UINT64_C(9007199254741999)){
    KichR4TelemetryContext c={};c.struct_size=sizeof(c);c.capture_id=token;c.engine_frame=engine;c.render_frame=render;return c;
}
static KichR4TelemetryTiming Result(const KichR4TelemetryContext& c){
    KichR4TelemetryTiming r={};r.struct_size=sizeof(r);r.status=KICH_R4_TIMING_READY;r.capture_id=c.capture_id;r.engine_frame=c.engine_frame;r.render_frame=c.render_frame;return r;
}
int main(){
    const unsigned before=allocations;
    {
        TimingQueue q;assert(q.count==0);
        const KichR4TelemetryContext snapshot=Context(7),record=Context(q.NextToken());
        assert(record.capture_id==UINT64_C(0x8000000000000001));
        assert(q.Add(snapshot,0) && q.Add(record,3));
        assert(q.Find(Result(snapshot))==0 && q.Find(Result(record))==1);
        KichR4TelemetryTiming wrong=Result(record);++wrong.engine_frame;assert(q.Find(wrong)==q.count);
        wrong=Result(record);++wrong.render_frame;assert(q.Find(wrong)==q.count);
        wrong=Result(record);++wrong.capture_id;assert(q.Find(wrong)==q.count);
        KichR4TelemetryContext duplicate=record;++duplicate.engine_frame;
        assert(!q.Add(duplicate,4) && q.count==2);
        assert(!q.Erase(2) && q.count==2);assert(q.Erase(0) && q.count==1 && q.Find(Result(record))==0);
        assert(q.DropRecord()==1 && !q.count && q.Find(Result(record))==q.count);
        const uint64_t next=q.NextToken();assert(next!=record.capture_id);
        assert(q.Add(Context(next),5));q.Clear();assert(!q.count && q.NextToken()!=next);
    }
    {
        TimingQueue q;
        for(unsigned i=0;i<TimingQueue::Capacity;++i)assert(q.Add(Context(i+1),i%2?9:0));
        assert(!q.Add(Context(31),0)); // full ring may never silently evict a snapshot
        assert(q.DropRecord()==8 && q.count==8);
        for(unsigned i=0;i<q.count;++i)assert(q.pending[i].record_epoch==0 && q.pending[i].context.capture_id==i*2+1);
        assert(q.DropRecord()==0 && q.count==8);
        const uint64_t token=q.NextToken();assert(q.Add(Context(token),11));
        assert(q.Find(Result(Context(token)))==8 && q.pending[8].record_epoch==11);
    }
    {
        TimingQueue q;
        for(unsigned i=1;i<=TimingQueue::Capacity;++i)assert(q.Add(Context(UINT64_C(0x8000000000000000)|i),0));
        assert(q.NextToken()==UINT64_C(0x8000000000000011));
        // High-bit snapshots remain snapshots; routing never guesses from token bits.
        assert(q.DropRecord()==0 && q.count==TimingQueue::Capacity);
    }
    {
        TimingQueue q(UINT64_C(0x7ffffffffffffffe));
        assert(q.NextToken()==UINT64_MAX);
        assert(q.Add(Context(UINT64_C(0x8000000000000001)),0));
        assert(q.NextToken()==UINT64_C(0x8000000000000002));
        KichR4TelemetryContext invalid=Context(0);assert(!q.Add(invalid,0));
        invalid=Context(4);invalid.struct_size=0;assert(!q.Add(invalid,0));
        invalid=Context(4);invalid.flags=4;assert(!q.Add(invalid,0));
        assert(q.count==1);
    }
    assert(allocations==before);
    puts("PASS: fixed timing owners/epochs, exact triple identity, OFF/resume, no snapshot eviction, token collision/wrap, zero allocations");
}
