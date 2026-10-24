#include "../../xr_3da/KichR4ShadowPoolPolicy.h"
#include <assert.h>
#include <stdio.h>
using namespace kich_r4_shadow_pool;

static void PressureAndRecovery(){
    ResolutionPolicy policy;
    assert(policy.Select(64,0)==512);
    assert(policy.Select(65,16)==256); // 65 MiB at 512: downgrade in this frame.
    assert(policy.Downgrades()==1 && policy.Recoveries()==0);
    // A light repeatedly crossing the budget boundary must not churn the pool.
    for(uint32_t t=32;t<8000;t+=16)assert(policy.Select((t/16)&1?64:65,t)==256);
    assert(policy.PendingSize()==0 && policy.Downgrades()==1);
    // Recovery is permitted at <=48 MiB, with a full two-second observation window.
    assert(policy.Select(48,8000)==256);
    assert(policy.PendingSize()==512);
    for(uint32_t t=8250;t<10000;t+=250)assert(policy.Select(48,t)==256);
    assert(policy.Select(48,10000)==512 && policy.Recoveries()==1);
    assert(policy.Select(65,10016)==256 && policy.Downgrades()==2);
}
static void RecoveryIsContinuous(){
    ResolutionPolicy policy(256);
    assert(policy.Select(7,0)==256);
    assert(policy.Select(7,1000)==256);
    assert(policy.Select(49,1500)==256 && policy.PendingSize()==0); // Headroom lost.
    assert(policy.Select(7,2000)==256);
    assert(policy.Select(7,3000)==256);
    assert(policy.Select(7,3999)==256);
    assert(policy.Select(7,4000)==512);
    policy.Reset(128);
    assert(policy.Select(7,0)==128);
    assert(policy.Select(7,1000)==128);
    assert(policy.Select(0,1500)==128 && policy.PendingSize()==0);
    assert(policy.Select(7,2000)==128);
    assert(policy.Select(7,3000)==128);
    assert(policy.Select(7,4000)==512); // One resize, no unnecessary 128->256->512 cycle.
    assert(policy.Recoveries()==1);
}
static void ClockAndSceneChanges(){
    ResolutionPolicy policy(256);
    for(unsigned frame=0;frame<10000;++frame)assert(policy.Select(7,0)==256);
    assert(policy.PendingSize()==512); // Repeated/high-FPS frames do not replace elapsed time.
    policy.Reset(256);
    const uint32_t start=UINT32_MAX-999;
    assert(policy.Select(7,start)==256);
    assert(policy.Select(7,start+1000)==256);
    assert(policy.Select(7,start+1999)==256);
    assert(policy.Select(7,start+2000)==512); // Clock wrap preserves elapsed interval.
    policy.Reset(256);
    assert(policy.Select(7,10)==256);
    assert(policy.Select(7,1010)==256);
    assert(policy.Select(7,10000)==256); // Long pause resets pending recovery.
    assert(policy.Select(7,11000)==256);
    assert(policy.Select(7,12000)==512);
    policy.Reset();
    assert(policy.Size()==512 && policy.Downgrades()==0 && policy.Recoveries()==0);
    assert(policy.Select(65,1000)==256);
    assert(policy.Select(7,2000)==256);
    assert(policy.Select(7,1500)==256); // Backwards clock is a discontinuity.
    assert(policy.Select(7,2500)==256);
    assert(policy.Select(7,3500)==512);
}
static void Bounds(){
    assert(!ValidResolution(0) && !ValidResolution(15) && !ValidResolution(257) && !ValidResolution(1024));
    assert(ValidResolution(16) && ValidResolution(512));
    assert(BestFit(0)==512 && BestFit(64)==512 && BestFit(65)==256);
    assert(BestFit(65536)==16 && BestFit(65537)==0);
    assert(BestFit(UINT32_MAX)==0); // No overflow in face/payload arithmetic.
    ResolutionPolicy policy;
    assert(policy.Select(65537,0)==0 && policy.Size()==512);
    // Exhaust the full registry envelope, including six faces for every source.
    for(uint32_t faces=1;faces<=4096*6;++faces){
        const uint32_t resolution=policy.Select(faces,faces);
        assert(Fits(faces,resolution));
        assert(Payload(faces,resolution)<=BudgetBytes);
    }
}
int main(){PressureAndRecovery();RecoveryIsContinuous();ClockAndSceneChanges();Bounds();
    puts("PASS: shadow pool pressure, continuous recovery, observation gaps, wrap and full registry budget");}
