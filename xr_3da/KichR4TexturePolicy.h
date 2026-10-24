#ifndef KICH_R4_TEXTURE_POLICY_H
#define KICH_R4_TEXTURE_POLICY_H
#include <stdint.h>
#include <string>
#include <map>
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
struct Entry {
    uint64_t handle,bytes,touched,retry;
    int32_t failure;
    uint32_t reason,width,height,mips;
    Entry():handle(0),bytes(0),touched(0),retry(0),failure(0),reason(Ready),width(0),height(0),mips(0){}
};
// Pins last for the complete submission epoch, including all material slots.
// Consumers store source names, never these evictable handles.
struct Registry {
    std::map<std::string,Entry> entries;
    uint64_t epoch,bytes,budget,evictions;
    Registry():epoch(1),bytes(0),budget(UINT64_C(384)*1024*1024),evictions(0){}
    void Begin(){++epoch;}
    template<class Release> bool MakeRoom(uint64_t required,Release release){
        if(required>budget)return false;
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
