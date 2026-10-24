#include "../../xr_3da/KichR4Capture.h"
#include "../../xr_3da/KichR4CaptureData.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <fstream>
#include <sstream>
namespace {
unsigned checks=0,queries=0;bool fail_query=false;
void Require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
bool Visibility(const KichR4LocalLight& light,const float* vp){
    ++queries;Require(vp && vp[15]==1,"visibility receives the exact selected view");
    if(fail_query)throw std::runtime_error("query unavailable");
    return light.position[0]>=0;
}
}
int main(){try{
    KichR4_CaptureEnable(true);KichR4_SetLightVisibility(Visibility);
    const kich_r4_scene::Matrix vp=kich_r4_scene::Identity();const float camera[3]={};
    int identities[20]={};
    for(unsigned i=0;i<20;++i){KichR4LocalLight light={};light.type=1;light.range=.1f;light.position[0]=i==0?-.5f:.5f;light.position[2]=.5f;light.color[0]=1;
        KichR4LightIdentity source={};source.process_handle=static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&identities[i]));source.serial=i+1;source.role=KICH_R4_LIGHT_ROLE_RUNTIME;
        Require(KichR4_CaptureLightIdentity(&identities[i],source),"source identity registered");
        KichR4_CaptureLight(&identities[i],light,true);KichR4_CaptureLightShadow(&identities[i],i%2==0);
    }
    KichR4LightingFrame frame;KichR4_CaptureLighting(camera,frame,false,vp.m);
    Require(frame.count==19 && frame.occlusion_tested==20 && frame.occlusion_rejected.size()==1,"only query-rejected light volume removed; no top8 quota");
    Require(frame.occlusion_rejected[0].serial==1,"rejection retains source lifetime identity");
    for(unsigned i=0;i<frame.count;++i)Require(frame.CastsShadow(i)==((frame.sources[i].serial-1)%2==0),"shadow participation follows source after compaction");
    RenderSnapshotInfo info={};std::strcpy(info.directory,".");std::strcpy(info.renderer,"R4");RenderSnapshot_Request();RenderSnapshot_Begin(info);
    KichR4_CaptureLightingDiagnostics(camera,frame);RenderSnapshot_End("COMPLETE");
    std::ifstream file(RenderSnapshot_LastPath());std::ostringstream out;out<<file.rdbuf();file.close();std::remove(RenderSnapshot_LastPath());
    Require(out.str().find("rejected_OCCLUDED_LIGHT_VOLUME=1")!=std::string::npos && out.str().find("rejected_SELECTION_NOT_CAPTURED=0")!=std::string::npos,"diagnostics explain occlusion instead of missing selection");
    const unsigned old_queries=queries;KichR4_CaptureLighting(camera,frame,false,NULL);
    Require(frame.count==20 && queries==old_queries && !frame.occlusion_enabled && frame.occlusion_rejected.empty(),"ROS/AI query keeps off-camera sources and clears old rejection provenance");
    fail_query=true;KichR4_CaptureLighting(camera,frame,false,vp.m);Require(frame.count==20 && frame.occlusion_rejected.empty(),"unavailable visibility query fails open per light");
    KichR4_SetLightVisibility(NULL);fail_query=false;queries=0;KichR4_CaptureLighting(camera,frame,false,vp.m);
    Require(frame.count==20 && !frame.occlusion_enabled && !queries,"uninstalled native callback preserves legacy source selection");
    KichR4_CaptureEnable(false);std::printf("E1.3 light-volume selection: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());KichR4_SetLightVisibility(NULL);KichR4_CaptureEnable(false);return 1;}}
