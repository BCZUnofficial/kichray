// P0-S8/S9 static lamp shadow cache policy (KichR4LocalShadowCache.h).
#include "KichR4LocalShadowCache.h"
#include <cstdio>
#include <stdexcept>
using namespace kich_r4_local_cache;
static unsigned checks=0;
static void Require(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
static void Matrix(float* m,float x){for(unsigned i=0;i<16;++i)m[i]=i%5==0?1.f:0.f;m[12]=x;}
int main(){try{
    Policy p;float m[16],moved[16];Matrix(m,10);Matrix(moved,10.5f);
    const Key campfire={7,2},flashlight={9,0};Handle next=100;
    const uint64_t base=11,lod_switched=12,alpha_reloaded=13;
    // As the session: the signature is only computed when the matrix held.
    auto use=[&](const Key& k,uint32_t size,const float* matrix,uint64_t signature,uint32_t frame,uint32_t revision=1,uint64_t budget=BudgetBytes){
        const uint64_t s=p.Matches(k,size,matrix,revision)?signature:0;
        Decision d=p.Use(k,size,matrix,revision,s,frame,budget);
        if(d.allocate){Require(d.action==Build,"only a build allocates");p.Allocated(*d.entry,next++);}
        if(d.action==Build)p.Built(*d.entry,s);
        return d;};
    Require(!p.Matches(campfire,256,m,1),"unknown face never matches");
    // A static lamp: direct on its first frame, cached build on the second, reuse after.
    Require(use(campfire,256,m,base,1).action==Uncached,"first sighting draws directly");
    Require(p.Matches(campfire,256,m,1) && !p.Matches(campfire,256,moved,1) && !p.Matches(campfire,512,m,1) && !p.Matches(campfire,256,m,2),"Matches compares matrix, size and revision");
    Decision d=use(campfire,256,m,base,2);Require(d.action==Build && d.entry->map==100 && p.bytes==Bytes(256),"unchanged matrix builds the cache");
    for(uint32_t f=3;f<10;++f)Require(use(campfire,256,m,base,f).action==Reuse,"stable lamp reuses its fixed statics");
    // P0-S9: a static LOD switch (camera moved) or a reloaded alpha texture
    // changes the signature and rebuilds into the same map, exactly when needed.
    d=use(campfire,256,m,lod_switched,10);Require(d.action==Build && !d.allocate && d.entry->map==100,"LOD switch rebuilds without reallocating");
    Require(use(campfire,256,m,lod_switched,11).action==Reuse,"rebuilt cache is reused");
    Require(use(campfire,256,m,alpha_reloaded,12).action==Build && use(campfire,256,m,alpha_reloaded,13).action==Reuse,"alpha texture change rebuilds once");
    // A moving flashlight never builds a cache (and needs no signature).
    for(uint32_t f=12;f<20;++f){float mf[16];Matrix(mf,float(f));Require(!p.Matches(flashlight,512,mf,1) && use(flashlight,512,mf,base,f).action==Uncached,"moving light keeps the direct path");}
    Require(p.entries[flashlight].map==0,"moving light owns no cache map");
    // Matrix change (light moved, range changed): direct this frame, cached again after one stable frame.
    Require(use(campfire,256,moved,base,20).action==Uncached && use(campfire,256,moved,base,21).action==Build,"changed matrix invalidates, then rebuilds");
    // A new size class releases the old map.
    d=use(campfire,512,moved,base,22);Require(d.action==Uncached && d.release.size()==1 && d.release[0]==100 && p.bytes==0,"size change releases the map");
    d=use(campfire,512,moved,base,23);Require(d.action==Build && d.allocate && p.bytes==Bytes(512),"new size allocates");
    // Level revision change rebuilds.
    Require(use(campfire,512,moved,base,24,2).action==Uncached && use(campfire,512,moved,base,25,2).action==Build,"level revision invalidates");
    // Budget: a 1 MiB budget holds one 512 map; another stable lamp evicts the one unused this frame.
    {Policy q;p=q;next=200;const Key a={1,0},b={2,0};
        use(a,512,m,base,1,1,Bytes(512));Require(use(a,512,m,base,2,1,Bytes(512)).action==Build,"first lamp builds");
        use(b,512,m,base,3,1,Bytes(512));d=use(b,512,m,base,4,1,Bytes(512));
        Require(d.action==Build && d.release.size()==1 && d.release[0]==200 && p.entries[a].map==0,"idle lamp is evicted for a stable one");
        // Both in one frame (lights come sorted by importance): the first takes
        // the room, the second draws directly, and a map used this frame is never evicted.
        Require(use(a,512,m,base,5,1,Bytes(512)).action==Build && p.entries[b].map==0,"the more important lamp takes the room");
        Require(use(b,512,m,base,5,1,Bytes(512)).action==Uncached && p.entries[a].map!=0,"no room: the other lamp draws directly");
        Require(use(a,512,m,base,6,1,Bytes(512)).action==Reuse && use(b,512,m,base,6,1,Bytes(512)).action==Uncached,"no thrashing on later frames");
        Require(p.bytes<=Bytes(512),"budget holds");}
    // Idle lamps release their maps.
    {const std::vector<Handle> idle=p.BeginFrame(6+IdleFrames+1);Require(idle.size()==1 && p.entries.empty() && p.bytes==0,"idle cache maps are released");}
    use(campfire,256,m,base,400);use(campfire,256,m,base,401);
    Require(p.Clear().size()==1 && p.entries.empty() && p.bytes==0,"clear releases every map");
    // Signature: order and every field matter.
    {Signature x,y,z;x.Add(1);x.Add(2);y.Add(2);y.Add(1);z.Add(1);z.Add(2);Require(x.value!=y.value && x.value==z.value,"signature is order and value sensitive");}
    std::printf("P0-S9 static lamp shadow cache policy: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
