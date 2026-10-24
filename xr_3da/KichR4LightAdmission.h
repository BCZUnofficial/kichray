#ifndef KICH_R4_LIGHT_ADMISSION_H
#define KICH_R4_LIGHT_ADMISSION_H
#include "KichR4Lighting.h"
#include <algorithm>
#include <vector>

namespace kich_r4_light_admission {
static const uint32_t HideDelayMs=100,ObservationGapMs=250;
struct Key {
    uint64_t serial,process_handle;
    explicit Key(const KichR4LightIdentity& source):serial(source.serial),process_handle(source.process_handle){}
    bool operator<(const Key& other)const{return serial!=other.serial?serial<other.serial:process_handle<other.process_handle;}
    bool operator==(const Key& other)const{return serial==other.serial && process_handle==other.process_handle;}
};
struct Counts {
    uint64_t observations,raw_rejected,retained,effective_rejected,retained_shadow_faces,overflow,invalid;
    Counts():observations(0),raw_rejected(0),retained(0),effective_rejected(0),retained_shadow_faces(0),overflow(0),invalid(0){}
    void Add(const Counts& other){observations+=other.observations;raw_rejected+=other.raw_rejected;
        retained+=other.retained;effective_rejected+=other.effective_rejected;retained_shadow_faces+=other.retained_shadow_faces;
        overflow+=other.overflow;invalid+=other.invalid;}
};
// R2 retains positive HOM visibility; native admission uses a short continuous
// negative interval instead of letting one coarse HOM result remove a visible
// light. A newly observed hidden source is not admitted just to warm history.
// These are source-admission decisions only. Light data and every shadow map
// are current-frame data; neither intensity nor shadow contents are cached.
class Policy {
    struct Entry {Key key;uint32_t hidden_since,negative_samples;
        explicit Entry(const Key& source):key(source),hidden_since(0),negative_samples(0){}};
    struct Less {bool operator()(const Entry& a,const Entry& b)const{return a.key<b.key;}
        bool operator()(const Entry& a,const Key& b)const{return a.key<b;}};
    size_t capacity;std::vector<Entry> previous,current;
    bool initialized,continuous;uint32_t previous_frame,previous_time,pending_frame,pending_time;
    Counts pending;
    bool Result(bool raw,bool effective,uint32_t shadow_faces){
        if(!raw){++pending.raw_rejected;if(effective){++pending.retained;pending.retained_shadow_faces+=shadow_faces;}
            else ++pending.effective_rejected;}
        return effective;
    }
public:
    Counts last,window;
    explicit Policy(size_t limit=KICH_R4_LIGHT_REGISTRY_CAPACITY):capacity((std::min)(limit,size_t(KICH_R4_LIGHT_REGISTRY_CAPACITY))),
        initialized(false),continuous(false),previous_frame(0),previous_time(0),pending_frame(0),pending_time(0){
        previous.reserve(capacity);current.reserve(capacity);
    }
    void Reset(){previous.clear();current.clear();initialized=continuous=false;previous_frame=previous_time=0;last=window=Counts();}
    void Reported(){window=Counts();}
    void Begin(uint32_t frame,uint32_t now){
        current.clear();pending=Counts();pending_frame=frame;pending_time=now;
        const uint32_t elapsed=now-previous_time,frames=frame-previous_frame;
        continuous=initialized && elapsed<=ObservationGapMs && frames<=1;
    }
    bool Observe(const KichR4LightIdentity& source,bool raw_visible,uint32_t shadow_faces=0){
        ++pending.observations;const Key key(source);
        if(!key.serial && !key.process_handle){++pending.invalid;return Result(raw_visible,true,shadow_faces);}
        if(current.size()>=capacity){++pending.overflow;return Result(raw_visible,true,shadow_faces);}
        Entry next(key);
        if(!raw_visible){
            next.hidden_since=pending_time-HideDelayMs;next.negative_samples=2;
            if(continuous){const auto found=std::lower_bound(previous.begin(),previous.end(),key,Less());
                if(found!=previous.end() && found->key==key){
                    if(found->negative_samples){next.hidden_since=found->hidden_since;
                        next.negative_samples=found->negative_samples;
                        if(pending_frame!=previous_frame && next.negative_samples<2)++next.negative_samples;
                    }else{next.hidden_since=pending_time;next.negative_samples=1;}
                }
            }
        }
        current.push_back(next);
        const bool visible=raw_visible || next.negative_samples<2 || uint32_t(pending_time-next.hidden_since)<HideDelayMs;
        return Result(raw_visible,visible,shadow_faces);
    }
    // Only successful publication advances history. Sources omitted by the
    // active/valid/color/frustum gates disappear here, never carried forward.
    void Commit(){
        std::sort(current.begin(),current.end(),Less());
        size_t write=0;
        for(size_t i=0;i<current.size();){size_t end=i+1;
            while(end<current.size() && current[end].key==current[i].key)++end;
            if(end==i+1)current[write++]=current[i];else pending.invalid+=end-i;
            i=end;
        }
        current.erase(current.begin()+write,current.end());previous.swap(current);
        previous_frame=pending_frame;previous_time=pending_time;initialized=true;
        last=pending;window.Add(last);
    }
    void Forget(const KichR4LightIdentity& source){const Key key(source);
        const auto found=std::lower_bound(previous.begin(),previous.end(),key,Less());
        if(found!=previous.end() && found->key==key)previous.erase(found);
    }
    size_t Tracked()const{return previous.size();}
};
}
#endif
