#ifndef KICH_R4_WATCH_H
#define KICH_R4_WATCH_H
#include "KichR4CaptureData.h"
#include "KichR4LightingMath.h"
#include "KichR4Deferred.h"
#include "KichR4RecordControl.h"
#include <cstdio>
#include <cmath>
#include <cstring>

// One render-camera bounds candidate, not an ID-buffer pick. All observations
// reuse submitted CPU data: no rays, poses, texture reads or GPU readback.
namespace kich_r4_watch {
typedef kich_r4_capture::NativeModelCommand Command;
enum Status { Off, Pending, Locked, Lost, NoCandidate };
enum Change { Acquired=1, Disappeared=2, RangeChanged=4, BindingChanged=8,
    ExposureChanged=16, CascadeChanged=32, RouteChanged=64 };
enum { MaxCandidates=4096, MaxEvents=8, MaxPasses=7 };
struct PassSample {
    bool observed,lightmap_authored,hemi_authored,has_uv,split,unlit;
    uint32_t flags,first,count,draws;uint64_t handles[6];float material_class,emissive_gain,emissive_specular;
    PassSample():observed(false),lightmap_authored(false),hemi_authored(false),has_uv(false),split(false),unlit(false),flags(0),first(0),count(0),draws(0),handles(),material_class(0),emissive_gain(0),emissive_specular(0){}
};
struct Sample {
    uint32_t frame,time_ms,source_frame,first,count,attributes,shader_element,shader_pass,route_mask,cascade;
    float center[3],camera_distance,actor_distance,hemi,sun,near_q[3],far_q[3],blend;
    float ambient_rgb[3],hemi_rgb[3],sun_rgb[3],sun_direction[3];
    bool actor_known,light_valid,projection_valid;PassSample passes[MaxPasses];
    Sample():frame(0),time_ms(0),source_frame(0),first(0),count(0),attributes(0),shader_element(0),shader_pass(0),route_mask(0),cascade(0),center(),camera_distance(0),actor_distance(0),hemi(1),sun(1),near_q(),far_q(),blend(0),ambient_rgb(),hemi_rgb(),sun_rgb(),sun_direction(),actor_known(false),light_valid(false),projection_valid(false){}
};
struct Event {uint32_t frame,time_ms,changes,first,count,cascade,routes;float hemi,sun,blend;};
inline bool Finite(float value){return kich_r4_lighting::Finite(value);}
inline float Distance(const float* a,const float* b){double d=0;for(unsigned i=0;i<3;++i){const double v=double(a[i])-b[i];d+=v*v;}return static_cast<float>(std::sqrt(d));}
inline bool RayBounds(const Command& c,const float* origin,const float* direction,float& distance){
    if(!c.bounds_known || !c.bounds.valid || !origin || !direction)return false;
    double lo=0,hi=10000;
    for(unsigned i=0;i<3;++i){if(!Finite(origin[i]) || !Finite(direction[i]) || !Finite(c.bounds.min[i]) || !Finite(c.bounds.max[i]) || c.bounds.min[i]>c.bounds.max[i])return false;
        const double d=direction[i],p=origin[i],a=c.bounds.min[i],b=c.bounds.max[i];
        if(std::fabs(d)<1.e-9){if(p<a || p>b)return false;continue;}
        double u=(a-p)/d,v=(b-p)/d;if(u>v){const double t=u;u=v;v=t;}if(u>lo)lo=u;if(v<hi)hi=v;if(lo>hi)return false;
    }
    distance=static_cast<float>(lo);return true;
}
inline bool Project(const float* point,const kich_r4_scene::Matrix& matrix,float* out){
    float q[4];kich_r4_capture::Transform(point,matrix,q);if(!Finite(q[3]) || std::fabs(q[3])<1.e-8)return false;
    for(unsigned i=0;i<3;++i){out[i]=q[i]/q[3];if(!Finite(out[i]))return false;}return true;
}
inline const char* MapState(bool authored,bool uv,uint32_t flags,uint32_t bit){return !authored?"NOT_AUTHORED":!uv?"NO_UV1":flags&bit?"BOUND":"NOT_BOUND_OR_UNAVAILABLE";}
class Tracker {
    Status status;bool active_frame,have_previous,pending_notice,sample_complete,suspended;
    uint64_t target_visual,target_lifetime,revision,epoch;
    std::weak_ptr<const kich_r4_capture::Mesh> source;
    Sample sample,previous;Event events[MaxEvents];uint32_t stored,omitted,scanned,skipped,last_report;
    char resource[192],shader[128];
    const char* lost_reason;
    void EventRecord(uint32_t changes){if(!changes)return;Event e={sample.frame,sample.time_ms,changes,sample.first,sample.count,sample.cascade,sample.route_mask,sample.hemi,sample.sun,sample.blend};
        if(stored<MaxEvents)events[stored++]=e;else ++omitted;}
    bool Matches(const Command& c)const{return c.visual_id==target_visual && c.lifecycle_serial==target_lifetime && c.mesh && !source.owner_before(c.mesh) && !c.mesh.owner_before(source);}
    void Lose(const char* reason){status=Lost;lost_reason=reason;active_frame=sample_complete=suspended=false;pending_notice=true;EventRecord(Disappeared);source.reset();}
public:
    Tracker():status(Off),active_frame(false),have_previous(false),pending_notice(false),sample_complete(false),suspended(false),target_visual(0),target_lifetime(0),revision(0),epoch(0),stored(0),omitted(0),scanned(0),skipped(0),last_report(0),resource(),shader(),lost_reason("NONE"){}
    void Request(bool enable){*this=Tracker();status=enable?Pending:Off;pending_notice=enable;}
    void Reset(){Request(false);}
    Status State()const{return status;}
    bool Active()const{return active_frame && status==Locked;}
    bool Completed(uint32_t frame)const{return status==Locked && sample_complete && sample.frame==frame;}
    const Sample& Latest()const{return sample;}
    uint32_t PendingEvents()const{return stored;}
    uint32_t OmittedEvents()const{return omitted;}
    uint64_t TargetLifetime()const{return target_lifetime;}
    // A menu/loading/occluded/failed frame is a gap, not evidence that the
    // watched model disappeared. Cancel partial observations and suppress the
    // old sample until the same lifetime is observed in a completed world frame.
    // reason is a static string owned by the caller.
    void Suspend(uint32_t frame,uint32_t now_ms,const char* reason){
        active_frame=sample_complete=false;
        if(status!=Locked)return;
        if(!suspended){suspended=true;pending_notice=true;sample.frame=frame;sample.time_ms=now_ms;lost_reason=reason?reason:"FRAME_NOT_COMPLETED";}
    }
    // Call before Draw, only in an ordinary recorded world frame. A target
    // leaves the session permanently when absent/replaced; reacquire explicitly.
    void Begin(uint32_t frame,uint32_t now_ms,uint64_t level_revision,const kich_r4_capture::Batch& batch,
        const float* camera,const float* direction,const float* actor=NULL){
        active_frame=sample_complete=false;if(!kich_r4_record::Enabled() || status==Off || status==Lost || status==NoCandidate)return;
        sample=Sample();sample.frame=frame;sample.time_ms=now_ms;
        if(epoch!=kich_r4_record::Epoch()){const bool was_locked=status==Locked;epoch=kich_r4_record::Epoch();stored=omitted=0;have_previous=false;if(was_locked){Lose("RECORD_EPOCH_CHANGED");return;}}
        if(!camera || !direction || batch.engine_frame!=frame){if(status==Locked)Lose("FRAME_CONTEXT_MISMATCH");return;}
        const float length=direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2];
        if(!Finite(length) || length<1.e-8f){if(status==Locked)Lose("INVALID_CAMERA_DIRECTION");else{status=NoCandidate;pending_notice=true;lost_reason="INVALID_CAMERA_DIRECTION";}return;}
        if(status==Locked && (revision!=level_revision || source.expired())){Lose(revision!=level_revision?"LEVEL_REVISION_CHANGED":"SOURCE_EXPIRED");return;}
        if(suspended){suspended=false;pending_notice=true;lost_reason="RESUMED_AFTER_FRAME_GAP";}
        const Command* found=NULL;
        if(status==Pending){
            float nearest=1.e20f;scanned=skipped=0;
            const size_t count=(std::min)(batch.native_models.size(),size_t(MaxCandidates));
            for(size_t i=0;i<count;++i){const Command& c=batch.native_models[i];++scanned;
                if(c.layer || !c.identity_measured || !c.lifecycle_serial || c.source_frame!=frame || !c.mesh)continue;
                float distance;if(RayBounds(c,camera,direction,distance) && distance<nearest){nearest=distance;found=&c;}}
            skipped=static_cast<uint32_t>(batch.native_models.size()-count);
            if(!found){status=NoCandidate;pending_notice=true;last_report=now_ms-200;return;}
            target_visual=found->visual_id;target_lifetime=found->lifecycle_serial;source=found->mesh;revision=level_revision;status=Locked;
            std::snprintf(resource,sizeof(resource),"%.180s",found->resource.c_str());std::snprintf(shader,sizeof(shader),"%.116s",found->shader.c_str());
            last_report=now_ms-200;pending_notice=true;
        }else{
            // No new target search and no mesh/pose inspection. Commands are
            // already captured; limit protects diagnostic work in extreme scenes.
            const size_t count=(std::min)(batch.native_models.size(),size_t(MaxCandidates));
            for(size_t i=0;i<count;++i)if(Matches(batch.native_models[i])){found=&batch.native_models[i];break;}
            if(!found){Lose(batch.native_models.size()>MaxCandidates?"NOT_IN_BOUNDED_CAPTURE_PREFIX":"NOT_IN_CURRENT_CAPTURE_OR_LIFETIME_CHANGED");return;}
        }
        sample.source_frame=found->source_frame;sample.first=found->first;sample.count=found->count;sample.attributes=found->available_attributes;
        sample.shader_element=found->shader_element;sample.shader_pass=found->shader_pass;sample.hemi=found->object_hemi;sample.sun=found->object_sun;sample.light_valid=found->object_light_valid;
        if(!found->bounds_known || !found->bounds.valid){Lose("BOUNDS_UNAVAILABLE");return;}
        for(unsigned i=0;i<3;++i)sample.center[i]=(found->bounds.min[i]+found->bounds.max[i])*.5f;
        sample.camera_distance=Distance(sample.center,camera);sample.actor_known=actor!=NULL;if(actor)sample.actor_distance=Distance(sample.center,actor);
        active_frame=true;
    }
    // Hook after successful model draw. Values are already applied bindings;
    // optional texture availability is never re-queried by the recorder.
    void ObserveModel(const Command& c,unsigned pass,const kich_r4_lighting::Constants& constants,
        const KichR4DeferredDrawDesc& draw,bool split=false,bool unlit=false){
        if(!Active() || pass>=MaxPasses || c.source_frame!=sample.frame || !Matches(c))return;
        sample.route_mask|=1u<<pass;
        if(pass!=0 && pass!=2 && pass!=4)return; // Shadow/local-light slots are not material bindings.
        PassSample& p=sample.passes[pass];p.observed=true;p.flags=uint32_t(constants.material[0]);p.first=draw.state.draw.start_index;p.count=draw.state.draw.index_count;++p.draws;p.split|=split;p.unlit=unlit;
        p.material_class=constants.material[3];p.emissive_gain=constants.ambient[3];p.emissive_specular=constants.hemi[3];
        p.lightmap_authored=c.has_material && !c.material.lightmap.empty();p.hemi_authored=c.has_material && !c.material.hemi.empty();p.has_uv=(c.available_attributes&kich_r4_material::ATTR_UV1)!=0;
        for(unsigned i=0;i<6;++i)p.handles[i]=draw.textures[i];
    }
    void Finish(const kich_r4_lighting::Constants& constants){
        if(!Active())return;
        for(unsigned i=0;i<3;++i){sample.ambient_rgb[i]=constants.ambient[i];sample.hemi_rgb[i]=constants.hemi[i];sample.sun_rgb[i]=constants.sun_color[i];sample.sun_direction[i]=constants.sun_direction[i];}
        float receiver[3];for(unsigned i=0;i<3;++i)receiver[i]=sample.center[i]-constants.sun_direction[i]*.01f;
        sample.projection_valid=Project(receiver,constants.shadow[0],sample.near_q) && Project(receiver,constants.shadow[1],sample.far_q);
        if(sample.projection_valid){const float edge=(std::max)(std::fabs(sample.near_q[0]),std::fabs(sample.near_q[1]));
            if(constants.sun_color[3]<=0)sample.cascade=0;
            else if(sample.near_q[2]<0 || sample.near_q[2]>1 || edge>=1){sample.cascade=3;sample.blend=1;}
            else if(edge>.9f){sample.cascade=2;sample.blend=(edge-.9f)*10.f;}else sample.cascade=1;
            if(sample.cascade==3 && (std::fabs(sample.far_q[0])>1 || std::fabs(sample.far_q[1])>1 || sample.far_q[2]<0 || sample.far_q[2]>1))sample.cascade=4;
        }
        uint32_t change=have_previous?0:Acquired;
        if(have_previous){if(sample.first!=previous.first || sample.count!=previous.count)change|=RangeChanged;
            if(sample.light_valid!=previous.light_valid || std::fabs(sample.hemi-previous.hemi)>.05f || std::fabs(sample.sun-previous.sun)>.05f)change|=ExposureChanged;
            if(sample.cascade!=previous.cascade)change|=CascadeChanged;
            if(sample.route_mask!=previous.route_mask)change|=RouteChanged;
            for(unsigned i=0;i<MaxPasses;++i){const PassSample& a=sample.passes[i];const PassSample& b=previous.passes[i];
                if(a.observed!=b.observed || a.flags!=b.flags || a.first!=b.first || a.count!=b.count || a.split!=b.split || a.unlit!=b.unlit || a.material_class!=b.material_class || a.emissive_gain!=b.emissive_gain || a.emissive_specular!=b.emissive_specular || std::memcmp(a.handles,b.handles,sizeof(a.handles)))change|=BindingChanged;}}
        EventRecord(change);previous=sample;have_previous=true;active_frame=false;sample_complete=true;
    }
    // Five reports/s at most; numeric event ring catches one-frame changes.
    // Logging uses the caller's bounded/asynchronous sink, never Msg/FlushLog.
    void Report(uint32_t now_ms,void (*log)(const char*)){
        if(!kich_r4_record::Enabled() || !log || status==Off || status==Pending || uint32_t(now_ms-last_report)<200)return;
        if((status!=Locked || !sample_complete) && !pending_notice && !stored)return;
        last_report=now_ms;char text[900];
        if(pending_notice){const char* name=status==Locked?(suspended?"SUSPENDED":sample_complete?"LOCKED":"AWAITING_COMPLETED_FRAME"):status==Lost?"LOST":status==NoCandidate?"NO_CANDIDATE":"PENDING";
            std::snprintf(text,sizeof(text),"[R4 E1.7 watch] epoch=%llu time_ms=%u status=%s frame=%u visual=%llu lifetime=%llu resource=%s shader=%s scanned=%u omitted_candidates=%u reason=%s; selection=CAMERA_RAY_AABB_CANDIDATE; occlusion=UNKNOWN; lock=EXPLICIT_REACQUIRE; values=CPU_INPUTS_NOT_PIXEL_IRRADIANCE",(unsigned long long)epoch,sample.time_ms,name,sample.frame,(unsigned long long)target_visual,(unsigned long long)target_lifetime,resource,shader,scanned,skipped,lost_reason);log(text);pending_notice=false;}
        if(status==Locked && sample_complete){
            std::snprintf(text,sizeof(text),"[R4 E1.7 watch_sample] epoch=%llu time_ms=%u frame=%u source_frame=%u visual=%llu lifetime=%llu camera_distance=%.3f controlled_known=%u controlled_distance=%.3f center=[%.3f %.3f %.3f] first=%u indices=%u attributes=%u element=%u pass=%u routes=%u light_valid=%u hemi=%.5f source_sun=%.5f; values=CPU_INPUTS_NOT_PIXEL_IRRADIANCE",(unsigned long long)epoch,sample.time_ms,sample.frame,sample.source_frame,(unsigned long long)target_visual,(unsigned long long)target_lifetime,sample.camera_distance,sample.actor_known?1:0,sample.actor_distance,sample.center[0],sample.center[1],sample.center[2],sample.first,sample.count,sample.attributes,sample.shader_element,sample.shader_pass,sample.route_mask,sample.light_valid?1:0,sample.hemi,sample.sun);log(text);
            std::snprintf(text,sizeof(text),"[R4 E1.7 watch_shadow] epoch=%llu time_ms=%u frame=%u projection_valid=%u cascade=%u blend=%.5f near=[%.5f %.5f %.5f] far=[%.5f %.5f %.5f]; point=BOUNDS_CENTER; cascade_0=OFF_1=NEAR_2=BLEND_3=FAR_4=OUTSIDE; PCF_VISIBILITY_NOT_READ",(unsigned long long)epoch,sample.time_ms,sample.frame,sample.projection_valid?1:0,sample.cascade,sample.blend,sample.near_q[0],sample.near_q[1],sample.near_q[2],sample.far_q[0],sample.far_q[1],sample.far_q[2]);log(text);
            std::snprintf(text,sizeof(text),"[R4 E1.7 watch_environment] epoch=%llu time_ms=%u frame=%u ambient=[%.5f %.5f %.5f] hemi_rgb=[%.5f %.5f %.5f] sun_rgb=[%.5f %.5f %.5f] sun_direction=[%.5f %.5f %.5f]; weather_inputs_not_final_model_luminance",(unsigned long long)epoch,sample.time_ms,sample.frame,sample.ambient_rgb[0],sample.ambient_rgb[1],sample.ambient_rgb[2],sample.hemi_rgb[0],sample.hemi_rgb[1],sample.hemi_rgb[2],sample.sun_rgb[0],sample.sun_rgb[1],sample.sun_rgb[2],sample.sun_direction[0],sample.sun_direction[1],sample.sun_direction[2]);log(text);
            // Shadow and local-add bindings are not material-map bindings.
            const unsigned routes[]={0,2,4};for(unsigned n=0;n<3;++n){const unsigned i=routes[n];const PassSample& p=sample.passes[i];if(!p.observed)continue;
                std::snprintf(text,sizeof(text),"[R4 E1.7 watch_binding] epoch=%llu time_ms=%u frame=%u route=%u flags=%u draws=%u first=%u indices=%u split=%u t0_t5=[%llu %llu %llu %llu %llu %llu] lightmap=%s hemi_map=%s sun_policy=%s material_class=%.3f emission=%.3f/%.3f gloss_source=%s; bindings=LAST_SUCCESSFUL_DRAW_IN_ROUTE; fallback_reason_scope=CPU_BINDING_FEATURE",(unsigned long long)epoch,sample.time_ms,sample.frame,i,p.flags,p.draws,p.first,p.count,p.split?1:0,(unsigned long long)p.handles[0],(unsigned long long)p.handles[1],(unsigned long long)p.handles[2],(unsigned long long)p.handles[3],(unsigned long long)p.handles[4],(unsigned long long)p.handles[5],i==0?MapState(p.lightmap_authored,p.has_uv,p.flags,2):"NOT_GEOMETRY_ROUTE",i==0?MapState(p.hemi_authored,p.has_uv,p.flags,4):"NOT_GEOMETRY_ROUTE",p.unlit?"UNLIT":i==0?"CURRENT_PCF_SOURCE_MASK_DIAGNOSTIC":sample.light_valid?"ROS_TIMES_PCF":"CURRENT_PCF",p.material_class,p.emissive_gain,p.emissive_specular,(p.flags&1)?"BUMP_R_SQUARED":"FLAT_DEFAULT");log(text);}
        }
        for(uint32_t i=0;i<stored;++i){const Event& e=events[i];std::snprintf(text,sizeof(text),"[R4 E1.7 watch_event] epoch=%llu time_ms=%u frame=%u changes=%u first=%u indices=%u cascade=%u routes=%u hemi=%.5f source_sun=%.5f blend=%.5f; bits=1_acquire_2_lost_4_range_8_bindings_16_exposure_32_cascade_64_route",(unsigned long long)epoch,e.time_ms,e.frame,e.changes,e.first,e.count,e.cascade,e.routes,e.hemi,e.sun,e.blend);log(text);}
        if(omitted){std::snprintf(text,sizeof(text),"[R4 E1.7 watch_loss] epoch=%llu time_ms=%u omitted_events=%u capacity=%u; bounded_no_allocations",(unsigned long long)epoch,now_ms,omitted,unsigned(MaxEvents));log(text);}stored=omitted=0;
    }
};
inline Tracker& Global(){static Tracker tracker;return tracker;}
}
#endif
