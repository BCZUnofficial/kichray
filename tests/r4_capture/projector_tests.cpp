#include "../../xr_3da/KichR4Capture.h"
#include "../../xr_3da/KichR4CaptureData.h"
#include <cstdio>
#include <stdexcept>
namespace {unsigned checks=0;void Check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}}
int main(){try{
    KichR4_CaptureEnable(true);int ids[2]={};KichR4LocalLight light={};light.type=2;light.range=10;light.direction[2]=1;light.cone=1;light.color[0]=1;
    for(unsigned i=0;i<2;++i){KichR4LightIdentity identity={};identity.serial=7+i;identity.process_handle=uint64_t(reinterpret_cast<uintptr_t>(&ids[i]));
        Check(KichR4_CaptureLightIdentity(&ids[i],identity),"identity registration");KichR4_CaptureLight(&ids[i],light,true);
        KichR4LightProjector projector;projector.texture=i?"world/lamp":"lights/torch";projector.right[i]=1;
        KichR4_CaptureLightProjector(&ids[i],projector);projector.texture="caller_mutation";}
    const float camera[3]={};const auto vp=kich_r4_scene::Identity();KichR4LightingFrame frame;
    KichR4_CaptureBeginFrame(640,480,1);KichR4_FreezeLighting(1,100,camera,frame,false,vp.m);
    Check(frame.count==2 && frame.projectors.size()==2,"projectors accompany selected lights");
    for(unsigned i=0;i<frame.count;++i){const bool torch=frame.sources[i].serial==7;
        Check(frame.projectors[i].texture==(torch?"lights/torch":"world/lamp"),"selection sort preserves identity rather than matching equal numeric descriptors");
        Check(frame.projectors[i].right[torch?0:1]==1,"selection carries exact projector roll");}
    KichR4LightProjector clear;KichR4_CaptureLightProjector(&ids[0],clear);KichR4_CaptureLightForget(&ids[1]);
    KichR4LightingFrame frozen;Check(KichR4_CopyFrozenLighting(1,camera,vp.m,frozen) && frozen.count==2,"same-frame immutable snapshot outlives source mutation/deletion");
    Check(frozen.projectors[0].texture==frame.projectors[0].texture && frozen.projectors[1].texture==frame.projectors[1].texture,"frozen snapshot owns strings");
    KichR4_CaptureLighting(camera,frame,false,NULL);Check(frame.count==1 && frame.projectors.size()==1 && frame.projectors[0].texture.empty(),"clearing cookie preserves analytic numeric illumination");
    KichR4_CaptureLight(&ids[1],light,true);KichR4_CaptureLighting(camera,frame,false,NULL);
    Check(frame.count==2,"untextured source remains selected");for(unsigned i=0;i<frame.count;++i)Check(frame.projectors[i].texture.empty(),"reused address cannot inherit old projector");
    KichR4_CaptureEnable(false);KichR4_CaptureEnable(true);KichR4_CaptureLighting(camera,frame,false,NULL);
    Check(!frame.count && frame.projectors.empty(),"capture reset clears owned projector registry");KichR4_CaptureEnable(false);
    printf("E1.8 projector capture identity/lifetime/freeze: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
