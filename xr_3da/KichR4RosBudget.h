#ifndef KICH_R4_ROS_BUDGET_H
#define KICH_R4_ROS_BUDGET_H
#include <stdint.h>
#include <vector>
#include <algorithm>
namespace kich_r4_ros {
struct Entry {
    uint32_t updated,requested,granted;bool initialized,seen,allowed;
    Entry():updated(0),requested(0),granted(0),initialized(false),seen(false),allowed(false){}
    bool Due(uint32_t now)const{return !initialized || uint32_t(now-updated)>=200;}
};
// Round-robin admission prevents a stable front-to-back queue from starving
// later objects. AI/actor queries bypass admission but keep the 200 ms cache.
class Budget {
    std::vector<Entry*> entries;size_t cursor;uint32_t frame;bool valid;
public:
    Budget():cursor(0),frame(0),valid(false){}
    void Add(Entry& entry){entries.push_back(&entry);}
    void Remove(Entry& entry){const auto at=std::find(entries.begin(),entries.end(),&entry);
        if(at==entries.end())return;
        const size_t index=static_cast<size_t>(at-entries.begin());entries.erase(at);
        if(index<cursor && cursor)--cursor;
        if(cursor>=entries.size())cursor=0;}
    void Begin(uint32_t current,uint32_t now,unsigned limit=8){
        if(valid && current==frame)return;
        frame=current;valid=true;
        unsigned admitted=0;size_t scanned=0;
        while(scanned<entries.size() && admitted<limit){if(cursor>=entries.size())cursor=0;Entry& entry=*entries[cursor++];++scanned;
            if(entry.seen && uint32_t(current-entry.requested)<=2 && entry.Due(now)){
                entry.granted=current;entry.allowed=true;++admitted;}}
    }
    bool Request(Entry& entry,uint32_t current,uint32_t now,bool priority=false){
        Begin(current,now);entry.requested=current;entry.seen=true;
        // First exposure remains exact; steady render-only work is budgeted.
        return entry.Due(now) && (!entry.initialized || priority || (entry.allowed && entry.granted==current));
    }
};
}
#endif
