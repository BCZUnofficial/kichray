/* EXE-owned CPU mirror. Borrowed stream pointers are consumed before Unlock. */
#include "KichR4Capture.h"
#include "KichR4CaptureData.h"
#include "RenderSnapshot.h"
#include "KichR4ShadowPoolPolicy.h"
#include "KichR4LightTransitions.h"
#include <memory>
#include <cstdio>
namespace {
kich_r4_capture::State* capture=NULL;
KichR4LightVisibility light_visibility=NULL;
struct FrozenLighting {
    KichR4LightingFrame value;
    kich_r4_shadow_pool::ResolutionPolicy resolution;
    std::unique_ptr<kich_r4_light_transitions::Tracker> transitions;
    std::unique_ptr<kich_r4_light_admission::Policy> admission;
    float camera[3];uint32_t frame;bool ready;
    FrozenLighting():camera(),frame(0),ready(false){}
} frozen_lighting;
template<class F>void GuardCapture(F action){if(!capture)return;try{action(*capture);}
    catch(const std::exception& e){try{capture->frame.Fail(e.what());}catch(...) {}}
    catch(...){try{capture->frame.Fail("CPU capture exception");}catch(...) {}}}
}
kich_r4_capture::State* KichR4_CaptureState(){return capture;}
void KichR4_CaptureLight(const void* id,const KichR4LocalLight& light,bool active){
    if(!capture)return;
    try{capture->Light(id,light,active);}catch(...){} /* light hooks never break legacy rendering */
}
bool KichR4_CaptureLightIdentity(const void* id,const KichR4LightIdentity& source){
    if(!capture || !id)return false;
    try{if(capture->light_sources.size()<KICH_R4_LIGHT_REGISTRY_CAPACITY || capture->light_sources.find(id)!=capture->light_sources.end()){capture->light_sources[id]=source;return true;}}catch(...){}
    ++capture->light_identity_rejections;return false;
}
void KichR4_CaptureLightForget(const void* id){if(!capture)return;
    if(frozen_lighting.admission){KichR4LightIdentity source={};const auto found=capture->light_sources.find(id);
        if(found!=capture->light_sources.end())source=found->second;else source.process_handle=static_cast<uint64_t>(reinterpret_cast<uintptr_t>(id));
        frozen_lighting.admission->Forget(source);}
    capture->lights.erase(id);capture->light_sources.erase(id);capture->unshadowed_lights.erase(id);capture->light_observations.erase(id);}
void KichR4_CaptureLightShadow(const void* id,bool shadow){if(!capture)return;if(shadow)capture->unshadowed_lights.erase(id);else capture->unshadowed_lights.insert(id);}
void KichR4_SetLightVisibility(KichR4LightVisibility query){light_visibility=query;}
void KichR4_CaptureLighting(const float* camera,KichR4LightingFrame& frame,bool r1_lights_enabled,const float* camera_vp){
    frame.selection_frame=frame.local_shadow_size=frame.shadow_plan_faces=0;
    frame.shadow_resolution_changes=frame.shadow_resolution_reason=0;
    frame.count=0;frame.shadow_mask=0;frame.occlusion_enabled=frame.occlusion_tested=0;frame.occlusion_rejected.clear();
    frame.r1_lights_requested=frame.r1_lights_applied=r1_lights_enabled?1u:0u;if(!capture || !camera)return;
    try{capture->SelectLights(camera,frame,r1_lights_enabled,camera_vp,light_visibility);}catch(...){frame.count=0;frame.shadow_mask=0;}
}
void KichR4_ResetFrozenLighting(){frozen_lighting.ready=false;frozen_lighting.resolution.Reset();
    if(frozen_lighting.admission)frozen_lighting.admission->Reset();
    if(frozen_lighting.transitions)frozen_lighting.transitions->Reset();}
bool KichR4_CopyFrozenLighting(uint32_t engine_frame,const float* camera,const float* camera_vp,KichR4LightingFrame& out){
    if(!capture || !frozen_lighting.ready || frozen_lighting.frame!=engine_frame || !camera || !camera_vp ||
        memcmp(camera,frozen_lighting.camera,sizeof(frozen_lighting.camera)) ||
        memcmp(camera_vp,frozen_lighting.value.visibility_vp,sizeof(frozen_lighting.value.visibility_vp)))return false;
    bool copied=false;GuardCapture([&](kich_r4_capture::State&){out=frozen_lighting.value;copied=true;});
    if(!copied){frozen_lighting.ready=false;out.valid=out.count=out.shadow_mask=0;out.local_shadow_size=out.shadow_plan_faces=0;}
    return copied;
}
void KichR4_FreezeLighting(uint32_t engine_frame,uint32_t now_ms,const float* camera,KichR4LightingFrame& frame,bool r1_lights_enabled,const float* camera_vp){
    if(KichR4_CopyFrozenLighting(engine_frame,camera,camera_vp,frame))return;
    frozen_lighting.ready=false;
    frame.selection_frame=frame.local_shadow_size=frame.shadow_plan_faces=0;
    frame.shadow_resolution_changes=frame.shadow_resolution_reason=0;
    frame.count=frame.shadow_mask=0;
    if(!capture || !camera || !camera_vp || !capture->frame.error.empty())return;
    GuardCapture([&](kich_r4_capture::State& c){
        // Native publication must not silently turn an allocation failure into
        // an empty lighting frame. Legacy/AI snapshots retain their own policy.
        if(!frozen_lighting.admission)frozen_lighting.admission.reset(new kich_r4_light_admission::Policy());
        frozen_lighting.admission->Begin(engine_frame,now_ms);
        c.SelectLights(camera,frame,r1_lights_enabled,camera_vp,light_visibility,frozen_lighting.admission.get());
        uint32_t faces=0;
        for(uint32_t i=0;i<frame.count;++i)if(frame.CastsShadow(i))faces+=frame.lights[i].type==2?1:6;
        kich_r4_shadow_pool::ResolutionPolicy next=frozen_lighting.resolution;
        const uint32_t before=next.Size();frame.local_shadow_size=next.Select(faces,now_ms);
        frame.shadow_plan_faces=faces;frame.selection_frame=engine_frame;
        frame.shadow_resolution_changes=next.Downgrades()+next.Recoveries();
        frame.shadow_resolution_reason=frame.local_shadow_size<before?1u:frame.local_shadow_size>before?2u:0u;
        if(!frozen_lighting.transitions)frozen_lighting.transitions.reset(new kich_r4_light_transitions::Tracker());
        // Publish only after every potentially allocating copy succeeded.
        // Later mutations enter the next frame, including weather changes.
        frozen_lighting.value=frame;
        frozen_lighting.transitions->Observe(frozen_lighting.value);
        frozen_lighting.admission->Commit();
        frozen_lighting.resolution=next;memcpy(frozen_lighting.camera,camera,sizeof(frozen_lighting.camera));
        frozen_lighting.frame=engine_frame;frozen_lighting.ready=true;
    });
    if(!frozen_lighting.ready){frame.valid=frame.count=frame.shadow_mask=0;frame.local_shadow_size=frame.shadow_plan_faces=0;}
}
void KichR4_ReportLightingTransitions(uint32_t engine_frame,void (*log)(const char*)){
    if(!log || !capture || !frozen_lighting.ready || frozen_lighting.frame!=engine_frame || !frozen_lighting.transitions)return;
    kich_r4_light_transitions::Tracker& tracker=*frozen_lighting.transitions;
    if(!tracker.window.snapshots)return;
    GuardCapture([&](kich_r4_capture::State&){
        const kich_r4_light_transitions::Counters& counts=tracker.window;
        const kich_r4_light_admission::Counts& hom=frozen_lighting.admission->last;
        const kich_r4_light_admission::Counts& hom_window=frozen_lighting.admission->window;
        const KichR4LightingFrame& frame=frozen_lighting.value;
        const char* reason=frame.shadow_resolution_reason==1?"PRESSURE":frame.shadow_resolution_reason==2?"RECOVERY":"UNCHANGED";
        char text[2048];
        snprintf(text,sizeof(text),"[R4 E1.5 lights] frame=%u observed_snapshots=%llu selected=%u hom_rejected=%u observed_set_complete=%u added_to_observed=%llu left_observed=%llu selected_to_hom=%llu hom_to_selected=%llu transitions=%llu event_examples=%u events_omitted=%llu skipped_snapshots=%llu truncated_inputs=%llu invalid_inputs=%llu conflicting_inputs=%llu pool_size=%u pool_faces=%u pool_resizes_total=%u pool_resize_reason_current=%s hom_raw_rejected=%llu hom_retained=%llu hom_effective_rejected=%llu hom_retained_shadow_faces=%llu hom_raw_rejected_window=%llu hom_retained_window=%llu hom_effective_rejected_window=%llu hom_history_invalid=%llu hom_history_overflow=%llu; visibility=EFFECTIVE_HOM; visible_to_hidden_delay_ms=100; cold_hidden=IMMEDIATE; absence=NOT_SELECTED_NOT_CAUSAL; no_creation_destruction_inference",
            engine_frame,static_cast<unsigned long long>(counts.snapshots),tracker.last.selected,tracker.last.hom_rejected,unsigned(tracker.last.complete),
            static_cast<unsigned long long>(counts.added),static_cast<unsigned long long>(counts.removed),
            static_cast<unsigned long long>(counts.selected_to_hom),static_cast<unsigned long long>(counts.hom_to_selected),
            static_cast<unsigned long long>(counts.transitions),tracker.event_count,static_cast<unsigned long long>(counts.dropped_events),
            static_cast<unsigned long long>(counts.skipped_snapshots),static_cast<unsigned long long>(counts.overflow_inputs),
            static_cast<unsigned long long>(counts.invalid_inputs),static_cast<unsigned long long>(counts.conflicting_inputs),
            frame.local_shadow_size,frame.shadow_plan_faces,frame.shadow_resolution_changes,reason,
            static_cast<unsigned long long>(hom.raw_rejected),static_cast<unsigned long long>(hom.retained),static_cast<unsigned long long>(hom.effective_rejected),
            static_cast<unsigned long long>(hom.retained_shadow_faces),static_cast<unsigned long long>(hom_window.raw_rejected),
            static_cast<unsigned long long>(hom_window.retained),static_cast<unsigned long long>(hom_window.effective_rejected),
            static_cast<unsigned long long>(hom_window.invalid),static_cast<unsigned long long>(hom_window.overflow));
        log(text);
        for(unsigned i=0;i<tracker.event_count;++i){const kich_r4_light_transitions::Event& event=tracker.events[i];
            snprintf(text,sizeof(text),"[R4 E1.5 light_transition] frame=%u source_serial=%llu process_handle=%llu from=%s to=%s; observed_selection_transition_only",
                event.frame,static_cast<unsigned long long>(event.source.serial),static_cast<unsigned long long>(event.source.process_handle),
                kich_r4_light_transitions::Name(event.from),kich_r4_light_transitions::Name(event.to));
            log(text);
        }
        tracker.Reported();
        frozen_lighting.admission->Reported();
    });
}
void KichR4_CaptureLightingDiagnostics(const float* camera,const KichR4LightingFrame& frame){
    if(!g_render_snapshot_active || !capture)return;
    try{capture->SnapshotLights(camera,frame);}catch(...){RenderSnapshot_Text("TELEMETRY_LOSS","light_registry","ALLOCATION_FAILED");}
}
const kich_r4_capture::Batch* KichR4_CapturedFrame(){return capture?&capture->frame:NULL;}
void KichR4_CaptureEnable(bool enabled,void (*log)(const char*)){if(enabled){if(!capture)try{capture=new kich_r4_capture::State();}catch(...) {}if(capture)capture->log=log;}
    else {KichR4_ResetFrozenLighting();delete capture;capture=NULL;}}
bool KichR4_CaptureActive(){return capture && capture->frame.width && capture->frame.error.empty();}
void KichR4_CaptureBeginFrame(uint32_t w,uint32_t h,uint32_t frame){GuardCapture([&](kich_r4_capture::State& c){c.Begin(w,h,frame);});}
void KichR4_CaptureHUD(bool begin){GuardCapture([&](kich_r4_capture::State& c){c.frame.ui_seen=true;
    if(begin)++c.frame.ui_scope;else {kich_r4_level::Check(c.frame.ui_scope!=0,"HUD scope imbalance");--c.frame.ui_scope;}});}
void KichR4_CaptureLoad(void* visual,const void* data,uint32_t bytes,const char* name){GuardCapture([&](kich_r4_capture::State& c){
    kich_r4_level::Check(visual && data && bytes,"empty model source");c.Load(visual,kich_r4_level::Span(static_cast<const unsigned char*>(data),bytes),name);});}
void KichR4_CaptureCopy(void* visual,const void* source){GuardCapture([&](kich_r4_capture::State& c){c.Copy(visual,source);});}
void KichR4_CaptureForget(void* visual){if(capture)capture->models.erase(visual);}
void KichR4_CaptureLock(void* buffer,const void* data,uint32_t at,uint32_t bytes,uint32_t discard){GuardCapture([&](kich_r4_capture::State& c){c.Lock(buffer,data,at,bytes,discard);});}
void KichR4_CaptureUnlock(void* buffer,uint32_t bytes){GuardCapture([&](kich_r4_capture::State& c){c.Unlock(buffer,bytes);});}
void KichR4_CaptureBuffer(void* buffer,const void* data,uint32_t bytes){GuardCapture([&](kich_r4_capture::State& c){
    kich_r4_capture::Buffer& b=c.buffers[buffer];b.segments.clear();b.Put(data,0,bytes);b.fixed=true;});}
void KichR4_CaptureForgetBuffer(void* buffer){if(capture)capture->buffers.erase(buffer);}
void KichR4_CaptureFail(const char* why){GuardCapture([&](kich_r4_capture::State& c){c.frame.Fail(why);});}
void KichR4_CaptureModelFail(void* visual,const char* why){GuardCapture([&](kich_r4_capture::State& c){
    const std::map<const void*,kich_r4_capture::Entry>::const_iterator found=c.models.find(visual);
    c.frame.Fail(found==c.models.end()?why:kich_r4_capture::ModelError(found->second,why).c_str());});}
bool KichR4_CaptureUIActive(){return KichR4_CaptureActive() && capture->frame.ui_scope!=0;}
bool KichR4_CaptureModelNeeded(void* visual,uint32_t& bones){bool needed=false;bones=0;
    GuardCapture([&](kich_r4_capture::State& c){c.frame.renderer_seen=true;
        const std::map<const void*,kich_r4_capture::Entry>::const_iterator found=c.models.find(visual);
        kich_r4_level::Check(found!=c.models.end(),"visual missing CPU registration; rebuild XR_3DA and renderer");
        kich_r4_level::Check(found->second.error.empty(),found->second.error.c_str());
        if(found->second.mesh){bones=found->second.mesh->bones;needed=true;}});return needed;
}
void KichR4_CaptureSubmitModel(const kich_r4_capture::ModelInput& input){GuardCapture([&](kich_r4_capture::State& c){
    using namespace kich_r4_capture;const std::map<const void*,Entry>::const_iterator found=c.models.find(input.visual);
    Check(found!=c.models.end() && found->second.mesh,"model registration changed during capture");
    Matrix world,vp;memcpy(world.m,input.world,sizeof(world));memcpy(vp.m,input.vp,sizeof(vp));
    Matrix bones[256];Check(input.bone_count<=256 && (!input.bone_count || input.bones),"model pose snapshot");
    if(input.bone_count)memcpy(bones,input.bones,input.bone_count*sizeof(Matrix));
    Mesh& mesh=*found->second.mesh;
    if(mesh.two_weights && (mesh.ignored_weights || mesh.outside_weights) && input.skin_mode>=1 && input.skin_mode<=2){
        const uint32_t bit=1u<<input.skin_mode;if(!(mesh.diagnostic_modes&bit)){mesh.diagnostic_modes|=bit;
            c.Diagnostic("2W skin path: model="+found->second.name+"; mode="+(input.skin_mode==KICH_R4_SKIN_HARDWARE?"hardware clamp/u8":"software raw")+"; "+mesh.first_weight_detail);}}
    try{if(c.native_models)c.frame.NativeModel(input.visual,found->second.mesh,world,vp,bones,input.bone_count,input.count,input.first,input.triangles,input.style,input.skin_mode,
            found->second.name.c_str(),input.shader,input.texture,input.secondary,input.effect,input.shader_element,input.shader_pass,input.skeleton,input.material,input.object_light_valid,input.object_hemi,input.object_sun);
        else c.frame.Model(input.visual,mesh,world,vp,bones,input.bone_count,input.count,input.first,input.triangles,input.style,input.skin_mode,found->second.name.c_str(),g_render_snapshot_active);}
    catch(const std::exception& e){c.frame.Fail(ModelError(found->second,e.what()).c_str());}
});}

void KichR4_CaptureSubmitUI(const kich_r4_capture::UIInput& input) {
    GuardCapture([&](kich_r4_capture::State& c){
        using namespace kich_r4_capture;Batch& frame=c.frame;if(!frame.ui_scope || !input.primitives)return;
        const uint32_t stride=input.stride,topology=input.topology,base=input.base,start=input.start,vertex_count=input.vertex_count,first=input.first,primitives=input.primitives;
        const uint32_t position=input.position,color=input.color,uv=input.uv;const bool indexed=input.indexed;
        Check(topology>=2 && topology<=6 && primitives<=131072,"HUD primitive topology/count");
        const uint32_t count=indexed?vertex_count:topology==2?primitives*2:topology==3?primitives+1:topology==4?primitives*3:primitives+2;
        Check(count && count<=262144 && stride>=20 && stride<=2048,"HUD vertex layout/count");
        Check(position!=UINT32_MAX && position<=stride-16 && color!=UINT32_MAX && color<=stride-4 &&
            (uv==UINT32_MAX || uv<=stride-8),"HUD vertex semantics");
        const std::map<void*,Buffer>::const_iterator source=c.buffers.find(input.vb);Check(source!=c.buffers.end(),"HUD vertex stream missing CPU mirror");
        const Style& style=input.style;std::string texture;
        if(input.texture && *input.texture && strcmp(input.texture,"$null")){texture=kich_r4_level::Lower(input.texture);
            if(texture.size()>4 && texture.substr(texture.size()-4)==".dds")texture.resize(texture.size()-4);
            Check(kich_r4_level::SafeName(texture) && texture[0]!='$',"HUD texture path");}
        Check(uint64_t(base)+start+count<=UINT32_MAX/stride,"HUD vertex offset overflow");
        frame.Reserve(count,primitives*6,input.texture,2);std::vector<Vertex> data;data.reserve(count);
        for(uint32_t i=0;i<count;++i){const unsigned char* p=source->second.Read((base+start+i)*stride,stride);float xyzw[4],tc[2]={};uint32_t argb;
            memcpy(xyzw,p+position,16);memcpy(&argb,p+color,4);if(uv!=UINT32_MAX)memcpy(tc,p+uv,8);
            data.push_back(frame.Screen(xyzw[0],xyzw[1],xyzw[2],xyzw[3],tc[0],tc[1],argb));}
        std::vector<uint32_t> order;const uint32_t required=topology==2?primitives*2:topology==3?primitives+1:topology==4?primitives*3:primitives+2;
        Check(required<=786432,"HUD index count");order.reserve(required);
        const Buffer* indices=NULL;if(indexed){const std::map<void*,Buffer>::const_iterator ii=c.buffers.find(input.ib);Check(ii!=c.buffers.end(),"HUD index stream missing CPU mirror");indices=&ii->second;
            Check(uint64_t(first)+required<=UINT32_MAX/2,"HUD index offset overflow");}
        for(uint32_t i=0;i<required;++i){uint32_t at=i;if(indexed){uint16_t value;memcpy(&value,indices->Read((first+i)*2,2),2);Check(value>=start,"HUD min vertex index");at=value-start;}
            Check(at<count,"HUD vertex index range");order.push_back(at);}
        const uint32_t vertex_base=static_cast<uint32_t>(frame.vertices.size());Command cmd={static_cast<uint32_t>(frame.indices.size()),0,2,texture,style};cmd.semantic=CAPTURE_UI;
        if(topology>=4){frame.vertices.insert(frame.vertices.end(),data.begin(),data.end());
            for(uint32_t i=0;i<primitives;++i){uint32_t a,b,d;
                if(topology==4){a=order[i*3];b=order[i*3+1];d=order[i*3+2];}
                else if(topology==5){a=order[i+(i&1)];b=order[i+1-(i&1)];d=order[i+2];}
                else {a=order[0];b=order[i+1];d=order[i+2];}
                frame.indices.push_back(vertex_base+a);frame.indices.push_back(vertex_base+b);frame.indices.push_back(vertex_base+d);}}
        else {for(uint32_t i=0;i<primitives;++i){const Vertex& a=data[order[topology==2?i*2:i]];const Vertex& b=data[order[topology==2?i*2+1:i+1]];
                const float dx=(b.p[0]/b.p[3]-a.p[0]/a.p[3])*frame.width*.5f;
                const float dy=(b.p[1]/b.p[3]-a.p[1]/a.p[3])*frame.height*.5f;const float length=sqrtf(dx*dx+dy*dy);if(length<1.e-6f)continue;
                frame.Reserve(4,6,input.texture,2);Vertex line[4]={a,a,b,b};const float ox=-dy/length/frame.width,oy=dx/length/frame.height;
                for(unsigned j=0;j<4;++j){const float sign=(j&1)?-1.f:1.f;line[j].p[0]+=sign*ox*line[j].p[3];line[j].p[1]+=sign*oy*line[j].p[3];}
                const uint32_t at=static_cast<uint32_t>(frame.vertices.size());frame.vertices.insert(frame.vertices.end(),line,line+4);
                const uint32_t ix[]={0,1,2,2,1,3};for(unsigned j=0;j<6;++j)frame.indices.push_back(at+ix[j]);}}
        cmd.count=static_cast<uint32_t>(frame.indices.size())-cmd.first;cmd.captured_vertices=static_cast<uint32_t>(frame.vertices.size())-vertex_base;if(cmd.count){
            SnapshotCommand(cmd,frame.vertices,frame.indices,"UI_GEOMETRY",NULL,0,uv!=UINT32_MAX);
            frame.commands.push_back(cmd);++frame.ui;}
    });
}
