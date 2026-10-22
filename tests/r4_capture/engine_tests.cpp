#include "fake_engine.h"
#include "../../xr_3da/KichR4CaptureData.h"
#include "fixture.h"
#include "../../xr_3da/KichR4Capture.h"
#include <stdio.h>
CaptureDevice Device;CaptureCache RCache;CaptureHardware HW;
// This suite exercises the DX9 snapshot adapter. Native state is covered by
// r4_native; satisfy the native hooks added to the production adapter in B5.
bool KichR4_NativeActive(){return false;}
bool KichR4_NativeStyle(kich_r4_capture::Style&){return false;}
void KichR4_NativeModelMaterial(kich_r4_capture::ModelInput&){}
namespace {
int checks=0;void Require(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
IDirect3DDevice9 gpu;IDirect3DVertexDeclaration9 declaration;int vb,ib;
struct TL {float x,y,z,w;uint32_t color;float u,v;};
TL quad[]={{10,20,.0001f,1,0x80402010,0,0},{10,40,.0001f,1,0xff0000ff,0,1},{30,20,.0001f,1,0xff00ff00,1,0},{30,40,.0001f,1,0xffff0000,1,1}};
void Begin(){HW.pDevice=&gpu;KichR4_CaptureBeginFrame(960,540,1);KichR4_CaptureHUD(true);
    KichR4_CaptureLock(&vb,quad,28*4,28*4,0);KichR4_CaptureUnlock(&vb,28*4);}
void Draw(uint32_t topology,uint32_t primitives,bool indexed=false){KichR4_CaptureUIDraw(&declaration,&vb,&ib,28,topology,indexed?4:0,indexed?0:4,indexed?4:0,0,primitives,indexed);}
void UI(){declaration.elements={{0,0,3,0,9,0},{0,16,4,0,10,0},{0,20,1,0,5,0},{255,0,17,0,0,0}};
    CTexture texture;texture.cName="ui\\atlas";RCache.texture=&texture;
    gpu.states[D3DRS_ALPHABLENDENABLE]=1;gpu.states[D3DRS_SCISSORTESTENABLE]=1;
    for(unsigned topology=2;topology<=6;++topology){Begin();Draw(topology,topology==4?1:2);
        const kich_r4_capture::Batch& b=*KichR4_CapturedFrame();Require(b.error.empty() && b.ui==1,"real adapter captures HUD topology");
        Require(b.commands[0].texture=="ui\\atlas" && b.commands[0].style.rect[0]==10 && b.commands[0].style.blend==1,"HUD texture/blending/scissor preserved");
        Require(b.indices.size()==(topology<4?12u:topology==4?3u:6u),"lines expanded / strips and fans triangulated");
        KichR4_CaptureHUD(false);}
    const uint16_t indices[]={0,1,2,3,2,1};KichR4_CaptureBuffer(&ib,indices,sizeof(indices));Begin();Draw(4,2,true);
    Require(KichR4_CapturedFrame()->error.empty() && KichR4_CapturedFrame()->indices[5]==1,"QuadIB and BaseVertexIndex captured");KichR4_CaptureHUD(false);
    Begin();texture.user=true;Draw(4,1);Require(!KichR4_CapturedFrame()->error.empty() && !KichR4_CapturedFrame()->Ready(),"runtime HUD textures select complete legacy frame");texture.user=false;
    Begin();gpu.view.Width=512;Draw(4,1);Require(!KichR4_CapturedFrame()->error.empty(),"offscreen target excluded");gpu.view.Width=960;
    Begin();const uint16_t bad[]={0,1,99};KichR4_CaptureBuffer(&ib,bad,sizeof(bad));Draw(4,1,true);Require(!KichR4_CapturedFrame()->error.empty(),"out-of-range HUD index contained");
    Begin();declaration.elements[0].Usage=0;Draw(4,1);Require(!KichR4_CapturedFrame()->error.empty(),"world-space geometry cannot masquerade as HUD");declaration.elements[0].Usage=9;
    RCache.texture=NULL;
}
void Models(){const fixture::Bytes source=capture_fixture::Model(2);int original,copy;KichR4_CaptureLoad(&original,source.data(),static_cast<uint32_t>(source.size()));KichR4_CaptureCopy(&copy,&original);KichR4_CaptureForget(&original);
    CKinematics skeleton;skeleton.bones.resize(2);skeleton.bones[0].m[12]=10;skeleton.bones[1].m[12]=20;
    KichR4_CaptureBeginFrame(960,540,2);KichR4_CaptureModel(&copy,&skeleton,3,3,1,KICH_R4_SKIN_SOFTWARE);
    Require(KichR4_CapturedFrame()->models==1 && fabsf(KichR4_CapturedFrame()->vertices[0].p[0]-12.5f)<.0001f,"engine bone matrices copied and posed at draw time");
    KichR4_CaptureBeginFrame(960,540,3);gpu.view.MaxZ=.02f;RCache.projection.m[0]=1.5f;KichR4_CaptureModel(&copy,&skeleton,3,3,1,KICH_R4_SKIN_SOFTWARE);
    Require(KichR4_CapturedFrame()->hands==1 && fabsf(KichR4_CapturedFrame()->vertices[0].p[0]-18.75f)<.0001f,"weapon keeps separate HUD FOV and near depth");
    gpu.view.MaxZ=1;KichR4_CaptureBeginFrame(960,540,4);KichR4_CaptureModel(&copy,&skeleton,3,3,1,KICH_R4_SKIN_SOFTWARE);Require(KichR4_CapturedFrame()->commands.empty(),"non-camera projection omitted");
    RCache.projection=Device.mProject;KichR4_CaptureBeginFrame(960,540,5);KichR4_CaptureModel(&copy,&skeleton,3,3,1);
    Require(KichR4_CapturedFrame()->error.find("skin mode handshake")!=std::string::npos,"old 2W renderer export cannot silently select wrong skinning semantics");
    KichR4_CaptureBeginFrame(960,540,6);KichR4_CaptureModel(&copy,&skeleton,3,3,1,KICH_R4_SKIN_HARDWARE);
    Require(KichR4_CapturedFrame()->error.empty() && fabsf(KichR4_CapturedFrame()->vertices[0].p[0]-(10.f+10.f*64.f/255.f))<.0001f,"real renderer hardware flag applies legacy byte weight");
    skeleton.bones.resize(1);KichR4_CaptureBeginFrame(960,540,7);KichR4_CaptureModel(&copy,&skeleton,3,3,1,KICH_R4_SKIN_HARDWARE);
    Require(KichR4_CapturedFrame()->error.find("required=2 actual=1")!=std::string::npos,"actual pose bone bounds retained with detailed diagnostic");
    RCache.projection=Device.mProject;KichR4_CaptureForget(&copy);KichR4_CaptureBeginFrame(960,540,5);KichR4_CaptureModel(&copy,&skeleton,3,3,1);
    Require(!KichR4_CapturedFrame()->error.empty(),"missing source never silently drops model");
}
}
int main(){try{KichR4_CaptureEnable(true);HW.pDevice=&gpu;UI();Models();KichR4_CaptureEnable(false);printf("B4.3b production engine model / HUD adapter: %d checks passed\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
