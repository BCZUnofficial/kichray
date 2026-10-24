#ifndef KICH_R4_LIGHT_TRANSITIONS_H
#define KICH_R4_LIGHT_TRANSITIONS_H
#include "KichR4Lighting.h"
#include <algorithm>

namespace kich_r4_light_transitions {
enum State {NOT_SELECTED_NOT_CAUSAL=0,SELECTED=1,HOM_REJECTED=2};
inline const char* Name(State value){return value==SELECTED?"SELECTED":value==HOM_REJECTED?"HOM_REJECTED":"NOT_SELECTED_NOT_CAUSAL";}
struct Key {
    uint64_t serial,process_handle;
    bool operator<(const Key& other)const{return serial!=other.serial?serial<other.serial:process_handle<other.process_handle;}
    bool operator==(const Key& other)const{return serial==other.serial && process_handle==other.process_handle;}
};
struct Event {uint32_t frame;Key source;State from,to;};
struct Counters {
    uint64_t snapshots,skipped_snapshots,added,removed,selected_to_hom,hom_to_selected;
    uint64_t transitions,dropped_events,overflow_inputs,invalid_inputs,conflicting_inputs;
    Counters():snapshots(0),skipped_snapshots(0),added(0),removed(0),selected_to_hom(0),hom_to_selected(0),
        transitions(0),dropped_events(0),overflow_inputs(0),invalid_inputs(0),conflicting_inputs(0){}
};
struct FrameStats {
    uint32_t frame,baseline_frame,selected,hom_rejected,observed,duplicates,invalid,conflicting;
    uint64_t requested_selected,requested_hom,overflow;
    bool complete,baseline_created;
    FrameStats():frame(0),baseline_frame(0),selected(0),hom_rejected(0),observed(0),duplicates(0),invalid(0),conflicting(0),
        requested_selected(0),requested_hom(0),overflow(0),complete(true),baseline_created(false){}
};

// Compare only the selection/HOM sets observed in successive frozen frames.
// An absent ID does not establish destruction, inactivity or frustum rejection.
// History contains at most one complete snapshot; both vectors reserve once.
// A truncated/invalid snapshot cannot cause invented source disappearances.
class Tracker {
    struct Record {Key source;State state;};
    struct Less {bool operator()(const Record& a,const Record& b)const{return a.source<b.source;}};
    size_t capacity;std::vector<Record> previous,current;bool initialized;uint32_t baseline_frame;
    void Add(const KichR4LightIdentity& identity,State state){
        if(!identity.serial && !identity.process_handle){++last.invalid;last.complete=false;return;}
        if(current.size()==capacity){++last.overflow;last.complete=false;return;}
        const Record record={{identity.serial,identity.process_handle},state};current.push_back(record);
    }
    void Changed(const Key& source,State from,State to){
        ++window.transitions;
        if(from==NOT_SELECTED_NOT_CAUSAL)++window.added;
        else if(to==NOT_SELECTED_NOT_CAUSAL)++window.removed;
        else if(to==HOM_REJECTED)++window.selected_to_hom;
        else ++window.hom_to_selected;
        if(event_count<EventCapacity){const Event event={last.frame,source,from,to};events[event_count++]=event;}
        else ++window.dropped_events;
    }
public:
    enum {EventCapacity=8};
    Counters window;FrameStats last;Event events[EventCapacity];unsigned event_count;
    explicit Tracker(size_t limit=KICH_R4_LIGHT_REGISTRY_CAPACITY):capacity((std::min)(limit,size_t(KICH_R4_LIGHT_REGISTRY_CAPACITY))),
        initialized(false),baseline_frame(0),events(),event_count(0){previous.reserve(capacity);current.reserve(capacity);}
    void Reported(){window=Counters();event_count=0;}
    void Reset(){previous.clear();current.clear();initialized=false;baseline_frame=0;last=FrameStats();Reported();}
    size_t Tracked()const{return previous.size();}
    size_t Capacity()const{return capacity;}
    void Observe(const KichR4LightingFrame& lighting){
        last=FrameStats();last.frame=lighting.selection_frame;last.baseline_frame=baseline_frame;
        last.requested_selected=lighting.count;last.requested_hom=lighting.occlusion_rejected.size();
        current.clear();++window.snapshots;
        const size_t selected=(std::min)(size_t(lighting.count),lighting.sources.size());
        if(selected!=lighting.count){last.invalid=lighting.count-static_cast<uint32_t>(selected);last.complete=false;}
        const size_t selected_read=(std::min)(selected,capacity);
        last.overflow+=selected-selected_read;
        for(size_t i=0;i<selected_read;++i)Add(lighting.sources[i],SELECTED);
        const size_t hom_read=(std::min)(lighting.occlusion_rejected.size(),capacity-current.size());
        last.overflow+=lighting.occlusion_rejected.size()-hom_read;
        for(size_t i=0;i<hom_read;++i)Add(lighting.occlusion_rejected[i],HOM_REJECTED);
        if(last.overflow)last.complete=false;
        std::sort(current.begin(),current.end(),Less());
        size_t unique=0;
        for(size_t i=0;i<current.size();++i){
            if(unique && current[unique-1].source==current[i].source){++last.duplicates;
                if(current[unique-1].state!=current[i].state){++last.conflicting;last.complete=false;}
            }else current[unique++]=current[i];
        }
        current.resize(unique);last.observed=static_cast<uint32_t>(unique);
        for(size_t i=0;i<unique;++i)if(current[i].state==SELECTED)++last.selected;else ++last.hom_rejected;
        window.overflow_inputs+=last.overflow;window.invalid_inputs+=last.invalid;window.conflicting_inputs+=last.conflicting;
        if(!last.complete){++window.skipped_snapshots;return;}
        if(initialized){size_t old=0,now=0;
            while(old<previous.size() || now<current.size()){
                if(now==current.size() || (old<previous.size() && previous[old].source<current[now].source)){
                    Changed(previous[old].source,previous[old].state,NOT_SELECTED_NOT_CAUSAL);++old;
                }else if(old==previous.size() || current[now].source<previous[old].source){
                    Changed(current[now].source,NOT_SELECTED_NOT_CAUSAL,current[now].state);++now;
                }else{if(previous[old].state!=current[now].state)Changed(current[now].source,previous[old].state,current[now].state);++old;++now;}
            }
        }else{initialized=true;last.baseline_created=true;}
        previous.swap(current);baseline_frame=last.frame;
    }
};
}
#endif
