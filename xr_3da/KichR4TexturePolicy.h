#ifndef KICH_R4_TEXTURE_POLICY_H
#define KICH_R4_TEXTURE_POLICY_H
#include <stdint.h>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
namespace kich_r4_texture {
enum Failure { Ready,SourceUnavailable,InvalidLayout,BudgetPressure,UploadFailed,Evicted };
inline bool FatalDevice(int32_t hr){const uint32_t code=static_cast<uint32_t>(hr);return code==0x887a0005u || code==0x887a0006u || code==0x887a0007u || code==0x887a0020u;}
inline bool Layout(uint32_t w,uint32_t h,uint32_t pitch,uint32_t bytes){
    return w && h && w<=16384 && h<=16384 && pitch>=uint64_t(w)*4 &&
        uint64_t(h-1)*pitch+uint64_t(w)*4<=bytes;
}
inline std::string Key(std::string name){
    for(size_t i=0;i<name.size();++i){if(name[i]=='/')name[i]='\\';
        if(name[i]>='A' && name[i]<='Z')name[i]=char(name[i]-'A'+'a');}
    if(name.size()>=4 && name.substr(name.size()-4)==".dds")name.resize(name.size()-4);
    return name;
}
enum Role { UI, HUD, EFFECT, BASE, LIGHTING, NORMAL, DETAIL, SHADOW_ALPHA };
struct Mip {uint32_t dimension,width,height,count;uint64_t bytes;};
struct Entry {
    uint64_t handle,bytes,touched,retry;
    int32_t failure;
    uint32_t reason,width,height,mips,faces;
    std::string source;std::vector<Mip> levels;
    uint64_t wanted,planned;uint32_t role,choice,resident;
    // P0-S11: epoch whose bytes are still being read in the background; such
    // an entry is neither planned nor uploaded this epoch.
    uint64_t pending;
    Entry():handle(0),bytes(0),touched(0),retry(0),failure(0),reason(Ready),width(0),height(0),mips(0),faces(0),wanted(0),planned(0),role(SHADOW_ALPHA),choice(0),resident(0),pending(0){}
};
// Pins last for the complete submission epoch, including all material slots.
// Consumers store source names, never these evictable handles.
struct Registry {
    std::map<std::string,Entry> entries;
    uint64_t epoch,bytes,budget,evictions,planned_bytes;
    uint32_t planned_sources,planned_downgrades,planned_denied;
    Registry():epoch(1),bytes(0),budget(UINT64_C(384)*1024*1024),evictions(0),planned_bytes(0),planned_sources(0),planned_downgrades(0),planned_denied(0){}
    void Begin(){++epoch;}
    void Plan(){
        planned_bytes=0;planned_sources=planned_downgrades=planned_denied=0;
        std::vector<Entry*> work;
        for(auto i=entries.begin();i!=entries.end();++i){Entry& e=i->second;
            if(e.wanted!=epoch || e.levels.empty() || e.pending==epoch || (e.retry>epoch && e.reason!=BudgetPressure))continue;
            e.planned=epoch;e.choice=e.resident;
            // Do not oscillate between full-size and reduced uploads on a
            // stationary camera. A retained mip is the starting quality floor.
            if(e.choice>=e.levels.size())e.choice=0;
            planned_bytes+=e.levels[e.choice].bytes;work.push_back(&e);++planned_sources;
        }
        while(planned_bytes>budget){Entry* victim=NULL;uint64_t saving=0;
            for(size_t i=0;i<work.size();++i){Entry& e=*work[i];
                if(e.choice==UINT32_MAX || e.choice+1>=e.levels.size())continue;
                const uint64_t delta=e.levels[e.choice].bytes-e.levels[e.choice+1].bytes;
                if(!victim || e.role>victim->role || (e.role==victim->role && delta>saving)){victim=&e;saving=delta;}}
            if(!victim)break;
            ++victim->choice;planned_bytes-=saving;++planned_downgrades;
        }
        // Only an irreducible working set can be denied. Preserve UI/HUD and
        // visible base maps before optional maps and shadow-only alpha sources.
        while(planned_bytes>budget){Entry* victim=NULL;
            for(size_t i=0;i<work.size();++i){Entry& e=*work[i];if(e.choice==UINT32_MAX)continue;
                if(!victim || e.role>victim->role || (e.role==victim->role && e.levels[e.choice].bytes>victim->levels[victim->choice].bytes))victim=&e;}
            if(!victim)break;
            planned_bytes-=victim->levels[victim->choice].bytes;victim->choice=UINT32_MAX;++planned_denied;
        }
    }
    template<class Release> bool MakeRoom(uint64_t required,Release release){
        if(required>budget)return false;
        // Admission is transactional with respect to capacity: a failed large
        // request must not evict half the working set before trying a small mip.
        uint64_t evictable=0;
        for(auto i=entries.begin();i!=entries.end();++i)
            if(i->second.handle && i->second.touched!=epoch)evictable+=i->second.bytes;
        if(bytes-evictable>budget-required)return false;
        while(bytes>budget-required){
            auto victim=entries.end();
            for(auto i=entries.begin();i!=entries.end();++i)
                if(i->second.handle && i->second.touched!=epoch &&
                    (victim==entries.end() || i->second.touched<victim->second.touched))victim=i;
            if(victim==entries.end())return false;
            Entry& e=victim->second;if(release(e.handle)<0)return false;
            bytes-=e.bytes;e.handle=e.bytes=0;e.retry=0;e.reason=Evicted;++evictions;
        }return true;
    }
};
}
#endif
