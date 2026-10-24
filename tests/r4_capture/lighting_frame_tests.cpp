#include "../../xr_3da/KichR4Capture.h"
#include "../../xr_3da/KichR4CaptureData.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>
static size_t failure_size=0;
static int failure_skip=-1;
static unsigned injected_failures=0;
void* operator new(size_t size){
    if(failure_skip>=0 && (!failure_size || failure_size==size)){
        if(!failure_skip){failure_skip=-1;++injected_failures;throw std::bad_alloc();}
        --failure_skip;
    }
    void* memory=std::malloc(size?size:1);if(!memory)throw std::bad_alloc();return memory;
}
void operator delete(void* memory)noexcept{std::free(memory);}
namespace {
unsigned checks=0,queries=0;
bool reject_light=false;
std::vector<std::string> messages;
void Require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
bool Visible(const KichR4LocalLight&,const float*){++queries;return !reject_light;}
void Log(const char* value){messages.push_back(value);}
bool Contains(size_t line,const char* text){return line<messages.size() && messages[line].find(text)!=std::string::npos;}
void AdmissionGates(const float* camera,const float* vp,int* id,KichR4LocalLight light){
    KichR4LightingFrame value;reject_light=true;
    KichR4_FreezeLighting(126,4248,camera,value,false,vp);
    Require(value.count==1,"single raw rejection keeps current active source");
    KichR4_CaptureLight(id,light,false);KichR4_FreezeLighting(127,4264,camera,value,false,vp);
    Require(!value.count && value.occlusion_rejected.empty(),"inactive source is removed immediately, not retained by HOM history");
    KichR4_CaptureLight(id,light,true);KichR4_FreezeLighting(128,4280,camera,value,false,vp);
    Require(!value.count,"reactivation begins fresh observation history without admitting a hidden source");
    light.color[0]=0;KichR4_CaptureLight(id,light,true);KichR4_FreezeLighting(129,4296,camera,value,false,vp);
    Require(!value.count && value.occlusion_rejected.empty(),"zero intensity is not prolonged by visibility history");
    light.color[0]=.3f;KichR4_CaptureLight(id,light,true);KichR4_FreezeLighting(130,4312,camera,value,false,vp);
    Require(!value.count,"restored intensity uses current HOM without stale visible history");
    light.position[0]=100;KichR4_CaptureLight(id,light,true);KichR4_FreezeLighting(131,4328,camera,value,false,vp);
    Require(!value.count && value.occlusion_rejected.empty(),"frustum exclusion is immediate");
    light.position[0]=0;KichR4_CaptureLight(id,light,true);KichR4_FreezeLighting(132,4344,camera,value,false,vp);
    Require(!value.count,"re-entry uses current HOM after old camera coverage is invalidated");
    light.range=0;KichR4_CaptureLight(id,light,true);KichR4_FreezeLighting(133,4360,camera,value,false,vp);
    Require(!value.count && value.occlusion_rejected.empty(),"invalid source is removed immediately");
    light.range=10;KichR4_CaptureLight(id,light,true);reject_light=false;
    KichR4_FreezeLighting(134,4376,camera,value,false,vp);Require(value.count==1,"valid visible source admitted immediately");
}
void AllocationFailures(const float* camera,const float* vp,int* ids,KichR4LocalLight light){
    int extra[2]={};
    for(unsigned i=0;i<66;++i)KichR4_CaptureLight(&ids[i],light,true);
    for(unsigned i=0;i<2;++i){KichR4LightIdentity source={};source.serial=67+i;source.process_handle=uint64_t(reinterpret_cast<uintptr_t>(&extra[i]));
        KichR4_CaptureLightIdentity(&extra[i],source);KichR4_CaptureLightShadow(&extra[i],true);}
    KichR4_CaptureLight(&extra[0],light,true);
    KichR4_CaptureBeginFrame(640,480,200);
    KichR4LightingFrame first;first.valid=1;KichR4_FreezeLighting(200,6000,camera,first,false,vp);
    Require(first.count==67,"allocation test baseline has larger vectors than default frame");
    KichR4_ReportLightingTransitions(200,Log);const size_t logged=messages.size();
    KichR4LightingFrame destination;const unsigned failures=injected_failures;
    failure_size=67*sizeof(KichR4LocalLight);failure_skip=0;
    const bool copied=KichR4_CopyFrozenLighting(200,camera,vp,destination);failure_skip=-1;
    Require(!copied && injected_failures==failures+1,"frozen consumer copy allocation failure is caught");
    Require(!destination.valid && !destination.count && !KichR4_CapturedFrame()->error.empty(),"failed copy invalidates output and records capture error");
    Require(!KichR4_CopyFrozenLighting(200,camera,vp,destination),"failed copy cannot leave a ready frozen publication");
    KichR4_ReportLightingTransitions(200,Log);Require(messages.size()==logged,"failed publication has no diagnostic report");

    KichR4_CaptureLight(&extra[1],light,true);KichR4_CaptureBeginFrame(640,480,201);
    KichR4LightingFrame publishing;publishing.valid=1;
    // First matching allocation is SelectLights' output; the second is the
    // immutable value copy, after the policy has been calculated.
    failure_size=68*sizeof(KichR4LocalLight);failure_skip=1;
    KichR4_FreezeLighting(201,6100,camera,publishing,false,vp);failure_skip=-1;
    Require(injected_failures==failures+2 && !publishing.count && !publishing.valid,"publication copy allocation failure is caught after planning");
    Require(!KichR4_CapturedFrame()->error.empty() && !KichR4_CopyFrozenLighting(201,camera,vp,destination),"partially copied snapshot never becomes ready");
    KichR4_ReportLightingTransitions(201,Log);Require(messages.size()==logged,"failed freeze is not observed or reported");

    KichR4_CaptureBeginFrame(640,480,202);publishing.valid=1;
    KichR4_FreezeLighting(202,6200,camera,publishing,false,vp);
    Require(publishing.count==68 && KichR4_CapturedFrame()->error.empty(),"next clean frame can publish after allocation failure");
    KichR4_ReportLightingTransitions(202,Log);
    Require(Contains(logged,"observed_snapshots=1 ") && Contains(logged,"added_to_observed=1 "),"only successful freezes advance source history");
    KichR4_CaptureLightForget(&extra[0]);KichR4_CaptureLightForget(&extra[1]);
}
}
int main(){try{
    KichR4_CaptureEnable(true);KichR4_SetLightVisibility(Visible);
    const float camera[3]={};const kich_r4_scene::Matrix vp=kich_r4_scene::Identity();
    int ids[66]={};KichR4LocalLight light={};light.type=2;light.range=10;light.direction[2]=1;light.cone=1;light.color[0]=1;
    for(unsigned i=0;i<66;++i){KichR4LightIdentity source={};source.process_handle=uint64_t(reinterpret_cast<uintptr_t>(&ids[i]));source.serial=i+1;
        KichR4_CaptureLightIdentity(&ids[i],source);KichR4_CaptureLight(&ids[i],light,true);KichR4_CaptureLightShadow(&ids[i],true);}
    KichR4LightingFrame first;first.valid=1;first.hemi[1]=.3f;first.sun_direction[1]=-1;
    KichR4_FreezeLighting(100,1000,camera,first,false,vp.m);
    Require(first.count==66 && queries==66,"one volume selection for complete frozen source list");
    Require(first.selection_frame==100 && first.local_shadow_size==256 && first.shadow_plan_faces==66,"shadow pressure planned before caster collection");
    Require(first.shadow_resolution_changes==1 && first.shadow_resolution_reason==1,"pressure transition identified");
    Require(messages.empty(),"freeze cannot log per frame");
    KichR4_ReportLightingTransitions(99,Log);KichR4_ReportLightingTransitions(100,NULL);
    Require(messages.empty(),"stale report frame and missing logger leave report pending");
    KichR4_ReportLightingTransitions(100,Log);
    Require(messages.size()==1 && Contains(0,"observed_snapshots=1 ") && Contains(0,"pool_size=256 ") && Contains(0,"pool_resizes_total=1 ") && Contains(0,"pool_resize_reason_current=PRESSURE"),"initial report describes current pool without fake transitions");
    for(unsigned i=1;i<66;++i)KichR4_CaptureLight(&ids[i],light,false);
    light.color[0]=.2f;KichR4_CaptureLight(&ids[0],light,true);
    KichR4LightingFrame present;present.hemi[1]=.9f;
    Require(KichR4_CopyFrozenLighting(100,camera,vp.m,present),"same frame consumer reuses captured source/weather state");
    Require(present.count==66 && present.hemi[1]==.3f && present.lights[0].color[0]==1 && queries==66,"late mutation cannot change already-collected frame");
    KichR4_FreezeLighting(100,1100,camera,present,true,vp.m);
    Require(present.count==66 && !present.r1_lights_applied && queries==66,"repeat publication does not reselect or advance recovery");
    KichR4_ReportLightingTransitions(100,Log);
    Require(messages.size()==1,"copies and repeated freezes cannot duplicate observation or report window");
    KichR4LightingFrame ai;KichR4_CaptureLighting(camera,ai,false,NULL);
    Require(ai.count==1 && ai.lights[0].color[0]==.2f && !ai.local_shadow_size && queries==66,"AI remains live and independent of frozen rendering");
    Require(!KichR4_CopyFrozenLighting(101,camera,vp.m,present),"old engine frame never reused");
    float moved[3]={.01f,0,0};Require(!KichR4_CopyFrozenLighting(100,moved,vp.m,present),"different camera rejected");
    kich_r4_scene::Matrix changed=vp;changed.m[0]=.5f;
    Require(!KichR4_CopyFrozenLighting(100,camera,changed.m,present),"different view rejected");
    for(unsigned i=0;i<=20;++i){present.hemi[1]=.9f;KichR4_FreezeLighting(101+i,1200+i*100,camera,present,false,vp.m);
        Require(present.count==1 && present.lights[0].color[0]==.2f,"next-frame changes adopted");
        Require(present.local_shadow_size==(i==20?512u:256u),"resolution restores only after sustained headroom");}
    Require(present.shadow_resolution_changes==2 && present.shadow_resolution_reason==2,"recovery transition identified");
    Require(messages.size()==1,"gameplay changes remain buffered until caller's status cadence");
    const size_t batch_start=messages.size();KichR4_ReportLightingTransitions(121,Log);
    Require(messages.size()-batch_start==9 && Contains(batch_start,"observed_snapshots=21 ") && Contains(batch_start,"left_observed=65 ") && Contains(batch_start,"events_omitted=57 "),"report bounds examples to eight and retains complete window counts");
    Require(Contains(batch_start,"pool_size=512 ") && Contains(batch_start,"pool_resizes_total=2 ") && Contains(batch_start,"pool_resize_reason_current=RECOVERY"),"recovery status accompanies source changes");
    for(size_t i=batch_start+1;i<messages.size();++i)Require(Contains(i,"to=NOT_SELECTED_NOT_CAUSAL"),"absence events do not invent visibility or lifecycle causes");
    KichR4_ReportLightingTransitions(121,Log);Require(messages.size()-batch_start==9,"consumed window cannot be logged twice");
    const unsigned before=queries;KichR4_ResetFrozenLighting();
    Require(!KichR4_CopyFrozenLighting(121,camera,vp.m,present),"level/reset invalidates same-frame cache");
    KichR4_FreezeLighting(121,4000,camera,present,false,vp.m);
    Require(present.local_shadow_size==512 && !present.shadow_resolution_changes && queries==before+1,"scene reset restores initial policy");
    const size_t reset_start=messages.size();KichR4_ReportLightingTransitions(121,Log);
    Require(messages.size()==reset_start+1 && Contains(reset_start,"transitions=0 ") && Contains(reset_start,"pool_resizes_total=0 "),"reset removes prior-level history and pending transition examples");
    reject_light=true;KichR4_FreezeLighting(122,4100,camera,present,false,vp.m);const size_t hom_start=messages.size();KichR4_ReportLightingTransitions(122,Log);
    Require(present.count==1 && messages.size()==hom_start+1 && Contains(hom_start,"selected_to_hom=0 ") && Contains(hom_start,"hom_raw_rejected=1 ") && Contains(hom_start,"hom_retained=1 "),"one raw HOM rejection is retained without an effective transition");
    KichR4_CaptureLighting(camera,ai,false,vp.m);
    Require(ai.count==0 && ai.occlusion_rejected.size()==1,"legacy live selection keeps immediate HOM rejection without native history");
    KichR4_FreezeLighting(123,4200,camera,present,false,vp.m);const size_t hidden_start=messages.size();KichR4_ReportLightingTransitions(123,Log);
    Require(!present.count && messages.size()==hidden_start+2 && Contains(hidden_start,"selected_to_hom=1 ") && Contains(hidden_start,"hom_effective_rejected=1 ") && Contains(hidden_start+1,"to=HOM_REJECTED"),"sustained HOM rejection has its effective selection reason");
    reject_light=false;KichR4_FreezeLighting(124,4216,camera,present,false,vp.m);const size_t visible_start=messages.size();KichR4_ReportLightingTransitions(124,Log);
    Require(messages.size()==visible_start+2 && Contains(visible_start,"hom_to_selected=1 "),"HOM recovery observed without source recreation");
    light.color[0]=.3f;KichR4_CaptureLight(&ids[0],light,true);KichR4_FreezeLighting(125,4232,camera,present,false,vp.m);
    const size_t color_start=messages.size();KichR4_ReportLightingTransitions(125,Log);
    Require(messages.size()==color_start+1 && Contains(color_start,"transitions=0 "),"animated intensity changes are not misreported as visibility flicker");
    AdmissionGates(camera,vp.m,&ids[0],light);
    AllocationFailures(camera,vp.m,ids,light);
    KichR4_CaptureEnable(false);
    Require(!KichR4_CopyFrozenLighting(121,camera,vp.m,present),"disabled capture cannot reuse previous game");
    const size_t disabled=messages.size();KichR4_ReportLightingTransitions(202,Log);Require(messages.size()==disabled,"disabled capture cannot report stale lighting");
    KichR4_SetLightVisibility(NULL);std::printf("E1.5 immutable lighting frame: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());KichR4_SetLightVisibility(NULL);KichR4_CaptureEnable(false);return 1;}}
