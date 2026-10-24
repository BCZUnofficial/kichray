#include "../../xr_3da/KichR4LightTransitions.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>

static size_t allocations=0;
void* operator new(size_t size){++allocations;void* memory=std::malloc(size?size:1);if(!memory)throw std::bad_alloc();return memory;}
void operator delete(void* memory)noexcept{std::free(memory);}

namespace {
using namespace kich_r4_light_transitions;
unsigned checks=0;
void Require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
KichR4LightIdentity ID(uint64_t serial,uint64_t handle=0){KichR4LightIdentity id={};id.serial=serial;id.process_handle=handle?handle:serial+100;return id;}
KichR4LightingFrame Frame(uint32_t frame,std::initializer_list<KichR4LightIdentity> selected,std::initializer_list<KichR4LightIdentity> hom={}){
    KichR4LightingFrame result;result.selection_frame=frame;result.sources=selected;result.count=static_cast<uint32_t>(selected.size());result.occlusion_rejected=hom;return result;
}
void Changes(){
    Tracker tracker;
    tracker.Observe(Frame(1,{ID(1),ID(2)},{ID(3)}));
    Require(tracker.last.complete && tracker.last.baseline_created && tracker.Tracked()==3,"first complete snapshot establishes observed baseline");
    Require(!tracker.window.transitions && !tracker.event_count,"initial snapshot must not invent activation events");
    tracker.Observe(Frame(2,{ID(2),ID(1)},{ID(3)}));
    Require(!tracker.window.transitions,"importance reordering does not change source state");
    tracker.Observe(Frame(3,{ID(3),ID(4)},{ID(2)}));
    Require(tracker.window.added==1 && tracker.window.removed==1 && tracker.window.selected_to_hom==1 && tracker.window.hom_to_selected==1,"all observed transition classes counted separately");
    Require(tracker.window.transitions==4 && tracker.event_count==4,"one event recorded for each changed identity");
    Require(tracker.events[0].source.serial==1 && tracker.events[0].to==NOT_SELECTED_NOT_CAUSAL,"absence never invents inactive/frustum/destruction cause");
    Require(tracker.last.baseline_frame==2 && tracker.events[0].frame==3,"event compares frozen frame against last complete baseline");
    Require(!std::strcmp(Name(NOT_SELECTED_NOT_CAUSAL),"NOT_SELECTED_NOT_CAUSAL"),"uncertain absence has explicit diagnostic label");
    tracker.Reported();
    Require(tracker.window.snapshots==0 && tracker.event_count==0 && tracker.Tracked()==3,"report rollover retains current state without new events");
    tracker.Observe(Frame(4,{ID(4),ID(3)},{ID(2)}));
    Require(!tracker.window.transitions && tracker.window.snapshots==1,"report rollover does not re-add unchanged lights");
    tracker.Observe(Frame(5,{}));
    Require(tracker.window.removed==3 && tracker.Tracked()==0,"empty complete frame removes all observed entries");
    tracker.Reset();tracker.Observe(Frame(6,{ID(9)}));
    Require(tracker.last.baseline_created && !tracker.window.transitions,"scene reset establishes fresh baseline without prior-level removals");
}
void ReuseAndInvalid(){
    Tracker tracker;
    tracker.Observe(Frame(1,{ID(1,55)}));
    tracker.Observe(Frame(2,{ID(2,55)}));
    Require(tracker.window.added==1 && tracker.window.removed==1,"pointer reuse with a new serial is a new source");
    tracker.Reset();tracker.Observe(Frame(3,{ID(0,77)}));tracker.Observe(Frame(4,{}, {ID(0,77)}));
    Require(tracker.window.selected_to_hom==1,"process handle remains usable when serial was not captured");
    tracker.Reset();tracker.Observe(Frame(5,{ID(1)}));
    tracker.Observe(Frame(6,{ID(1),ID(1)}));
    Require(tracker.last.complete && tracker.last.duplicates==1 && tracker.Tracked()==1 && !tracker.window.transitions,"duplicate identical observations do not invent changes");
    tracker.Observe(Frame(7,{ID(1)},{ID(1)}));
    Require(!tracker.last.complete && tracker.last.conflicting==1 && tracker.window.skipped_snapshots==1 && !tracker.window.transitions,"contradictory selection/HOM records cannot become transitions");
    KichR4LightIdentity missing={};tracker.Observe(Frame(8,{missing}));
    Require(!tracker.last.complete && tracker.last.invalid==1 && tracker.Tracked()==1,"unknown identity cannot remove last known source");
    KichR4LightingFrame malformed=Frame(9,{ID(2)});malformed.count=5;tracker.Observe(malformed);
    Require(!tracker.last.complete && tracker.last.invalid==4 && !tracker.window.transitions,"truncated source list preserves last complete baseline");
    tracker.Observe(Frame(10,{}, {ID(1)}));
    Require(tracker.last.baseline_frame==6 && tracker.window.selected_to_hom==1,"next complete frame compares against retained complete baseline");
}
void BoundsAndAllocation(){
    Tracker tracker(2);tracker.Observe(Frame(1,{ID(1),ID(2)}));
    tracker.Observe(Frame(2,{ID(3),ID(4),ID(5)}));
    Require(!tracker.last.complete && tracker.last.overflow==1 && tracker.window.overflow_inputs==1 && tracker.Tracked()==2,"input cap is explicit and preserves baseline");
    tracker.Observe(Frame(3,{ID(1)},{ID(2),ID(3)}));
    Require(!tracker.last.complete && tracker.last.overflow==1 && !tracker.window.transitions,"combined selected and HOM input shares one bounded capacity");
    tracker.Observe(Frame(4,{ID(2)},{ID(1)}));
    Require(tracker.window.selected_to_hom==1 && tracker.window.removed==0,"overflow cannot invent intermediate removals");
    Tracker full(999999);
    Require(full.Capacity()==KICH_R4_LIGHT_REGISTRY_CAPACITY,"caller cannot raise bound past registry capacity");
    full.Observe(Frame(1,{}));KichR4LightingFrame many=Frame(2,{});
    for(unsigned i=1;i<=20;++i)many.sources.push_back(ID(i));
    many.count=20;
    const size_t before=allocations;full.Observe(many);
    Require(allocations==before,"observing newly seen sources uses reserved storage only");
    Require(full.event_count==8 && full.window.added==20 && full.window.dropped_events==12,"bounded event examples keep complete aggregate counts");
    full.Reported();std::reverse(many.sources.begin(),many.sources.end());many.selection_frame=3;
    const size_t stable=allocations;
    for(unsigned i=0;i<100;++i)full.Observe(many);
    Require(allocations==stable && !full.window.transitions && full.window.snapshots==100,"steady tracking and order normalization allocate nothing");
    full.Reset();const size_t reset=allocations;full.Observe(many);
    Require(allocations==reset && full.last.baseline_created,"reset keeps reserved storage for next level");
}
}
int main(){try{Changes();ReuseAndInvalid();BoundsAndAllocation();std::printf("E1.5 bounded source transitions: %u checks passed\n",checks);return 0;}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
