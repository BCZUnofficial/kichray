#ifndef KICH_R4_FACE_ADMISSION_H
#define KICH_R4_FACE_ADMISSION_H
/* P0-S11 LIT01 shadow face admission history. FaceTest decides every face
 * from the current frame alone, so one coarse HOM false swapped a face for the
 * 16x16 lit map at once while its light stayed on: a shadow vanished for a
 * frame. Per source and face this keeps the source rule of
 * kich_r4_light_admission: an occluded face stays admitted until it has been
 * occluded for r4_lamp_face_hold ms (100) over at least two frames; a face that
 * is occluded on first sight is not admitted to warm history. Frustum exits
 * (no visible receiver can lie there) and budget drops are never held. A held
 * face is rendered this frame with current casters; no shadow content or pose
 * is cached. History restarts after an observation gap (250 ms or a skipped
 * frame), when the light jumps (teleport, re-aim, range/cone/type change) and
 * when the source is forgotten. */
#include "KichR4LocalShadowPlan.h"
#include "KichR4LightAdmission.h"
#include <algorithm>
#include <math.h>
#include <string.h>
#include <vector>
namespace kich_r4_face_admission {
using namespace kich_r4_local_shadow;
static const uint32_t DefaultHoldMs=kich_r4_light_admission::HideDelayMs,ObservationGapMs=kich_r4_light_admission::ObservationGapMs;
static const unsigned MaxEvents=16;
inline uint64_t SourceKey(const KichR4LightIdentity& id){return id.serial?id.serial:id.process_handle^0x8000000000000000ull;}
struct Counts {
    uint64_t faces,raw_occluded,held,occluded,frustum,budget,transitions,size_changes,restarts;
    Counts():faces(0),raw_occluded(0),held(0),occluded(0),frustum(0),budget(0),transitions(0),size_changes(0),restarts(0){}
    void Add(const Counts& o){faces+=o.faces;raw_occluded+=o.raw_occluded;held+=o.held;occluded+=o.occluded;frustum+=o.frustum;
        budget+=o.budget;transitions+=o.transitions;size_changes+=o.size_changes;restarts+=o.restarts;}
};
// A change of a face between admitted and not (face < 6), or of the light's
// map size class (face 255; from/to are then unused).
struct Event {uint32_t frame;uint64_t source;uint8_t face,from,to;uint32_t size_from,size_to;};
// Larger than a swinging lamp moves in one frame; what a teleport or re-aim does.
inline bool Jumped(const KichR4LocalLight& a,const KichR4LocalLight& b){
    if(a.type!=b.type)return true;
    const float range=(std::max)(a.range,b.range),reach=.25f*range+.1f;float d2=0;
    for(unsigned i=0;i<3;++i){const float d=a.position[i]-b.position[i];d2+=d*d;}
    if(!(d2<=reach*reach) || !(fabsf(a.range-b.range)<=.25f*(std::max)(a.range,.01f)))return true;
    if(a.type==2){const float na=sqrtf(a.direction[0]*a.direction[0]+a.direction[1]*a.direction[1]+a.direction[2]*a.direction[2]);
        const float nb=sqrtf(b.direction[0]*b.direction[0]+b.direction[1]*b.direction[1]+b.direction[2]*b.direction[2]);
        if(!(na>1e-6f) || !(nb>1e-6f))return true;
        const float c=(a.direction[0]*b.direction[0]+a.direction[1]*b.direction[1]+a.direction[2]*b.direction[2])/(na*nb);
        if(!(c>=.906f) || !(fabsf(a.cone-b.cone)<=.2f))return true;} // 25 degrees
    return false;
}
class Policy {
    struct Entry {uint64_t key;KichR4LocalLight light;uint32_t hidden_since[6],size;uint8_t negative[6],reasons[6];int32_t previous;
        Entry():key(0),light(),size(0),previous(-1){memset(hidden_since,0,sizeof(hidden_since));memset(negative,0,sizeof(negative));memset(reasons,0,sizeof(reasons));}};
    struct Less {bool operator()(const Entry& a,const Entry& b)const{return a.key<b.key;}
        bool operator()(const Entry& a,uint64_t b)const{return a.key<b;}};
    size_t capacity;std::vector<Entry> previous,current;
    bool initialized,continuous;uint32_t previous_frame,previous_time,pending_frame,pending_time,hold;
    Counts pending;
    void Record(uint64_t source,uint8_t face,uint8_t from,uint8_t to,uint32_t size_from,uint32_t size_to){
        if(event_count<MaxEvents){Event& e=events[event_count++];e.frame=pending_frame;e.source=source;e.face=face;e.from=from;e.to=to;e.size_from=size_from;e.size_to=size_to;}
        else ++events_omitted;}
public:
    Counts last,window;Event events[MaxEvents];unsigned event_count;uint64_t events_omitted;
    explicit Policy(size_t limit=KICH_R4_LIGHT_REGISTRY_CAPACITY):capacity(limit),initialized(false),continuous(false),
        previous_frame(0),previous_time(0),pending_frame(0),pending_time(0),hold(DefaultHoldMs),events(),event_count(0),events_omitted(0){
        previous.reserve(capacity);current.reserve(capacity);}
    void Reset(){previous.clear();current.clear();initialized=continuous=false;previous_frame=previous_time=0;last=window=Counts();Reported();}
    void Reported(){window=Counts();event_count=0;events_omitted=0;}
    // hold_ms 0: the P0-S7 behaviour, every occluded face leaves at once.
    void Begin(uint32_t frame,uint32_t now,uint32_t hold_ms=DefaultHoldMs){
        current.clear();pending=Counts();pending_frame=frame;pending_time=now;hold=hold_ms;
        continuous=initialized && now-previous_time<=ObservationGapMs && frame-previous_frame<=1;}
    // reasons: FaceTest output, rewritten to FaceHeld for kept faces. Returns the
    // admitted mask and a ticket for Final (-1: not tracked, capacity).
    uint32_t Admit(uint64_t key,const KichR4LocalLight& l,uint8_t* reasons,int32_t& ticket){
        ticket=-1;uint32_t mask=0;const unsigned faces=l.type==2?1u:6u;
        const Entry* before=NULL;
        if(continuous){const auto found=std::lower_bound(previous.begin(),previous.end(),key,Less());
            if(found!=previous.end() && found->key==key){if(Jumped(found->light,l))++pending.restarts;else before=&*found;}}
        Entry next;next.key=key;next.light=l;next.previous=before?int32_t(before-previous.data()):-1;
        for(unsigned face=0;face<faces;++face){++pending.faces;uint8_t& r=reasons[face];
            if(r==FaceOccluded){++pending.raw_occluded;
                if(before && before->negative[face]){next.hidden_since[face]=before->hidden_since[face];next.negative[face]=before->negative[face];
                    if(pending_frame!=previous_frame && next.negative[face]<2)++next.negative[face];}
                else if(before){next.hidden_since[face]=pending_time;next.negative[face]=1;}
                else{next.hidden_since[face]=pending_time-hold;next.negative[face]=2;} // cold: not admitted
                const bool kept=hold && (next.negative[face]<2 || uint32_t(pending_time-next.hidden_since[face])<hold);
                if(kept){r=FaceHeld;++pending.held;}else ++pending.occluded;}
            else if(r==FaceFrustum){++pending.frustum;next.hidden_since[face]=pending_time-hold;next.negative[face]=2;}
            if(FaceAdmitted(r))mask|=1u<<face;}
        if(current.size()<capacity){ticket=int32_t(current.size());current.push_back(next);}
        return mask;}
    // After FitBudget: admitted faces outside final_mask become BUDGET; records
    // admission and size transitions against the previous frame.
    void Final(int32_t ticket,uint32_t final_mask,uint32_t size,uint8_t* reasons){
        if(ticket<0 || size_t(ticket)>=current.size())return;Entry& e=current[ticket];
        const unsigned faces=e.light.type==2?1u:6u;
        for(unsigned face=0;face<faces;++face)if(FaceAdmitted(reasons[face]) && !(final_mask&(1u<<face))){reasons[face]=FaceBudget;++pending.budget;}
        memcpy(e.reasons,reasons,6);e.size=final_mask?size:0;
        if(e.previous<0)return;const Entry& p=previous[e.previous];
        for(unsigned face=0;face<faces;++face)if(FaceAdmitted(p.reasons[face])!=FaceAdmitted(e.reasons[face])){++pending.transitions;Record(e.key,uint8_t(face),p.reasons[face],e.reasons[face],p.size,e.size);}
        if(p.size && e.size && p.size!=e.size){++pending.size_changes;Record(e.key,255,0,0,p.size,e.size);}}
    // Only a published frame advances history; unplanned sources drop out.
    void Commit(){
        for(size_t i=0;i<current.size();++i)current[i].previous=-1;
        std::stable_sort(current.begin(),current.end(),Less());
        current.erase(std::unique(current.begin(),current.end(),[](const Entry& a,const Entry& b){return a.key==b.key;}),current.end());
        previous.swap(current);previous_frame=pending_frame;previous_time=pending_time;initialized=true;
        last=pending;window.Add(last);}
    void Forget(uint64_t key){const auto found=std::lower_bound(previous.begin(),previous.end(),key,Less());
        if(found!=previous.end() && found->key==key)previous.erase(found);}
    size_t Tracked()const{return previous.size();}
};
}
#endif
