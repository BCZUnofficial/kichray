#ifndef KICH_R4_LOCAL_SHADOW_CACHE_H
#define KICH_R4_LOCAL_SHADOW_CACHE_H
/* P0-S8 static lamp shadow cache. R2 (and R4 until now) redraws every static
 * caster of every lamp face each frame, although a campfire or a hanging lamp
 * and the level geometry never move. A face whose light keeps the same shadow
 * matrix (position, direction, range, cone, size) for two frames gets a cache
 * map holding its fixed statics; later frames copy it and draw only models and
 * wind-animated trees. Moving lights (NPC flashlights) never match and keep
 * the direct path at no extra cost. Policy only; the session owns the maps. */
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <map>
#include <vector>
namespace kich_r4_local_cache {
typedef uint64_t Handle; // KichR4Handle
enum Action {Uncached=0,Build=1,Reuse=2};
// Static LODs follow the camera distance; past this the cache is rebuilt.
static const float RebuildDistance=4.f;
static const uint32_t IdleFrames=300;
static const uint64_t BudgetBytes=64ull*1024*1024;
struct Key {uint64_t source;uint32_t face;
    bool operator<(const Key& o)const{return source<o.source || (source==o.source && face<o.face);}};
struct Entry {Handle map;uint32_t size,revision,last_frame,stable;float matrix[16],camera[3];bool valid;
    Entry():map(0),size(0),revision(0),last_frame(0),stable(0),valid(false){memset(matrix,0,sizeof(matrix));memset(camera,0,sizeof(camera));}};
struct Decision {Action action;Entry* entry;std::vector<Handle> release;bool allocate;Decision():action(Uncached),entry(NULL),allocate(false){}};
inline uint64_t Bytes(uint32_t size){return uint64_t(size)*size*4;}
struct Policy {
    std::map<Key,Entry> entries;uint64_t bytes=0;
    uint32_t builds=0,reuses=0,uncached=0;
    void Drop(Entry& e,std::vector<Handle>& release){if(e.map){release.push_back(e.map);bytes-=Bytes(e.size);e.map=0;}e.valid=false;}
    // Frame start: cached maps of lights unseen for IdleFrames are released.
    std::vector<Handle> BeginFrame(uint32_t frame){std::vector<Handle> release;builds=reuses=uncached=0;
        for(auto i=entries.begin();i!=entries.end();){if(frame-i->second.last_frame>IdleFrames){Drop(i->second,release);i=entries.erase(i);}else ++i;}
        return release;}
    Decision Use(const Key& key,uint32_t size,const float* matrix,uint32_t revision,const float* camera,uint32_t frame,uint64_t budget=BudgetBytes){
        Decision d;Entry& e=entries[key];d.entry=&e;
        const bool same=e.stable || e.last_frame || e.size?(e.size==size && e.revision==revision && !memcmp(e.matrix,matrix,sizeof(e.matrix))):false;
        e.last_frame=frame;
        if(!same){if(e.map && e.size!=size)Drop(e,d.release);
            e.valid=false;e.stable=0;e.size=size;e.revision=revision;memcpy(e.matrix,matrix,sizeof(e.matrix));++uncached;return d;}
        if(e.stable<2)++e.stable;
        if(e.valid){float d2=0;for(unsigned a=0;a<3;++a){const float v=camera[a]-e.camera[a];d2+=v*v;}
            if(!(d2<=RebuildDistance*RebuildDistance))e.valid=false;}
        if(e.valid){d.action=Reuse;++reuses;return d;}
        if(!e.map){
            // Room first from maps no face used this frame, oldest first.
            while(bytes+Bytes(size)>budget){Entry* oldest=NULL;
                for(auto& other:entries)if(other.second.map && other.second.last_frame!=frame && (!oldest || other.second.last_frame<oldest->last_frame))oldest=&other.second;
                if(!oldest)break;Drop(*oldest,d.release);}
            if(bytes+Bytes(size)>budget){++uncached;return d;}
            d.allocate=true;}
        d.action=Build;++builds;return d;
    }
    // The session created the map for a Build decision.
    void Allocated(Entry& e,Handle map){e.map=map;bytes+=Bytes(e.size);}
    // The fixed statics are in the map, rendered from this camera position.
    void Built(Entry& e,const float* camera){e.valid=true;memcpy(e.camera,camera,sizeof(e.camera));}
    std::vector<Handle> Clear(){std::vector<Handle> release;for(auto& e:entries)Drop(e.second,release);entries.clear();bytes=0;builds=reuses=uncached=0;return release;}
};
}
#endif
