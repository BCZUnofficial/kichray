#include "../../xr_3da/KichR4LightAdmission.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
static unsigned allocations=0;
void* operator new(size_t size){++allocations;void* p=std::malloc(size?size:1);if(!p)throw std::bad_alloc();return p;}
void operator delete(void* p)noexcept{std::free(p);}
using kich_r4_light_admission::Policy;
KichR4LightIdentity Identity(uint64_t serial,uint64_t pointer=1){KichR4LightIdentity id={};id.serial=serial;id.process_handle=pointer;return id;}
bool Frame(Policy& p,uint32_t frame,uint32_t now,const KichR4LightIdentity& id,bool raw,unsigned faces=6){
    p.Begin(frame,now);const bool result=p.Observe(id,raw,faces);p.Commit();return result;
}
int main(){
    Policy p;const KichR4LightIdentity id=Identity(1);
    const unsigned before=allocations;
    assert(Frame(p,1,1000,id,true));
    assert(Frame(p,2,1016,id,false));
    assert(p.last.raw_rejected==1 && p.last.retained==1 && p.last.retained_shadow_faces==6 && p.last.effective_rejected==0);
    assert(Frame(p,3,1099,id,false));
    assert(!Frame(p,4,1116,id,false));
    assert(p.last.raw_rejected==1 && p.last.retained==0 && p.last.effective_rejected==1);
    assert(!Frame(p,5,1132,id,false));
    assert(Frame(p,6,1148,id,true)); // Immediate newly visible source.
    assert(Frame(p,7,1164,id,false));
    assert(Frame(p,8,1180,id,true));
    for(unsigned i=0;i<200;++i)assert(Frame(p,9+i,1196+i*16,id,i%2==0));
    assert(allocations==before);

    // A missing candidate clears history; it is not retained as an old light.
    p.Begin(209,4400);p.Commit();assert(p.Tracked()==0);
    assert(!Frame(p,210,4416,id,false));
    assert(!Frame(p,211,4516,id,false));
    assert(!Frame(p,212,4532,Identity(2),false)); // Address reused, new serial.
    p.Forget(Identity(2));assert(p.Tracked()==0);
    assert(!Frame(p,213,4548,Identity(2),false));
    assert(!Frame(p,214,4648,Identity(2),false));
    assert(!Frame(p,215,5000,Identity(2),false)); // Observation gap.
    assert(!Frame(p,217,5016,Identity(2),false)); // Missing engine frame.
    assert(!Frame(p,218,4000,Identity(2),false)); // Clock moved backwards.

    p.Reset();assert(p.Tracked()==0 && p.window.observations==0);
    assert(Frame(p,0,0,id,true));
    assert(Frame(p,1,1,id,false));
    assert(Frame(p,1,101,id,false)); // Repeated frame cannot supply two samples.
    assert(!Frame(p,2,102,id,false));
    p.Reported();assert(p.window.observations==0 && p.last.effective_rejected==1);
    assert(Frame(p,3,102,id,true));

    p.Reset();assert(Frame(p,UINT32_MAX-1,UINT32_MAX-65,id,true));
    assert(Frame(p,UINT32_MAX,UINT32_MAX-49,id,false));
    assert(!Frame(p,0,50,id,false)); // Unsigned frame/time wrap.

    // A failed frame never commits pending admission/history/diagnostics.
    p.Reset();assert(Frame(p,0,0,id,true));assert(Frame(p,1,100,id,false));
    p.Begin(2,200);assert(!p.Observe(id,false));
    assert(p.last.retained==1 && p.window.observations==2);
    p.Begin(3,216);assert(!p.Observe(id,false));p.Commit(); // Gap discards old history.
    assert(p.window.observations==3);

    // Full registry, reverse input order, and subsequent sweeps remain bounded
    // and allocation-free; malformed identities cannot force a false hide.
    p.Reset();const unsigned full_before=allocations;
    for(unsigned frame=1;frame<=4;++frame){p.Begin(frame,frame*100);
        for(unsigned i=0;i<KICH_R4_LIGHT_REGISTRY_CAPACITY;++i){const auto key=Identity(KICH_R4_LIGHT_REGISTRY_CAPACITY-i,100+i);
            assert(p.Observe(key,frame==1,1)==(frame<=2));}
        p.Commit();assert(p.Tracked()==KICH_R4_LIGHT_REGISTRY_CAPACITY);}
    assert(allocations==full_before);
    Policy small(1);small.Begin(1,10);assert(!small.Observe(id,false));assert(small.Observe(Identity(2),false));small.Commit();
    assert(small.Tracked()==1 && small.last.overflow==1 && small.last.retained==1);
    p.Reset();p.Begin(1,0);assert(p.Observe(Identity(0,0),false));p.Commit();assert(p.Tracked()==0 && p.last.invalid==1);
    p.Begin(2,100);assert(!p.Observe(id,false));assert(p.Observe(id,true));p.Commit();assert(p.Tracked()==0 && p.last.invalid==2);
    std::puts("PASS: native HOM admission, continuous rejection, identity/lifecycle/gaps, atomic publication and allocation-free full registry");
}
