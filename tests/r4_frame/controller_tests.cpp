#include "../../xr_3da/xrRender_R4/R4FrameController.h"
#include "../../xr_3da/KichR4FrameSession.h"
#include <vector>
#include <string>
#include <stdexcept>
#include <stdio.h>
#include <limits>
#include <algorithm>
using namespace kich_r4;
namespace {
int checks=0;
const int32_t error=static_cast<int32_t>(UINT32_C(0x887a0005));
const int32_t occluded=static_cast<int32_t>(UINT32_C(0x087a0001));
void Require(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
struct Driver : FrameDriver {
    std::vector<std::string> trace;
    bool chain,view,bound;
    std::string fail;
    int32_t present_result,test_result;
    Driver():chain(false),view(false),bound(false),present_result(0),test_result(0){}
    void Event(const char* text){trace.push_back(text);}
    int32_t Create(void*,uint32_t,uint32_t){Event("create");chain=true;return fail=="create"?error:0;}
    void DropView(){Event("drop");bound=view=false;}
    int32_t Resize(uint32_t,uint32_t){Event("resize");Require(chain && !view && !bound,"all backbuffer references gone BEFORE ResizeBuffers");return fail=="resize"?error:0;}
    int32_t MakeView(){Event("view");if(fail=="view")return error;Require(chain && !view,"one native view owner");view=true;return 0;}
    void Bind(uint32_t w,uint32_t h){Event("bind");Require(view && chain && w && h,"bind valid target and viewport");bound=true;}
    void Clear(const float*){Event("clear");Require(view && bound,"clear valid bound view");}
    int32_t Present(uint32_t interval,bool test){Event(test?"test":"present");Require(chain && interval<=1,"valid presentation");return test?test_result:present_result;}
    int32_t RemovedReason(){Event("removed");return error;}
    void Close(){Event("close");chain=view=bound=false;}
};
KichR4FrameTargetDesc Desc(uint32_t w=960,uint32_t h=540){const KichR4FrameTargetDesc d={sizeof(d),w,h,reinterpret_cast<void*>(1)};return d;}
KichR4FrameColor Color(){const KichR4FrameColor c={sizeof(c),{0.1f,0.2f,0.3f,1.f}};return c;}
void Draw(FrameController& f){Require(f.Begin()==0,"begin");Require(f.Clear(Color())==0,"clear");Require(f.Present(1)==0,"present");}
void Sequence()
{
    Driver d;FrameController f(d);
    Require(f.Create(Desc())==0,"create target");Draw(f);
    Require(f.Resize(1280,720)==0,"resize");Draw(f);f.Close();
    const char* expected[]={"create","view","bind","clear","present","drop","resize","view","bind","clear","present","drop","close"};
    Require(d.trace.size()==sizeof(expected)/sizeof(expected[0]),"exact lifecycle trace length");
    for(size_t i=0;i<d.trace.size();++i)Require(d.trace[i]==expected[i],"exact lifecycle order");
    KichR4FrameStats s=f.Stats();
    Require(s.struct_size==sizeof(s) && !s.flags && s.width==1280 && s.height==720,"final dimensions/flags");
    Require(s.presents==2 && s.present_attempts==2 && s.clears==2 && s.resizes==1,"frame counters");
    Require(s.views_created==2 && s.views_released==2 && !d.chain && !d.view,"view/chain destruction");
    const size_t calls=d.trace.size();f.Close();Require(d.trace.size()==calls,"idempotent destruction");
}
void Validation()
{
    Driver d;FrameController f(d);
    Require(f.Begin()==KICH_R4_E_STATE && f.Present(1)==KICH_R4_E_STATE,"no frame before target");
    KichR4FrameTargetDesc bad=Desc();bad.window=NULL;Require(f.Create(bad)==KICH_R4_E_ARGUMENT,"null window");
    bad=Desc(0,540);Require(f.Create(bad)==KICH_R4_E_ARGUMENT,"zero initial size");
    bad=Desc(16385,540);Require(f.Create(bad)==KICH_R4_E_ARGUMENT,"size limit");
    bad=Desc();--bad.struct_size;Require(f.Create(bad)==KICH_R4_E_ARGUMENT && d.trace.empty(),"ABI validation before native calls");
    Require(f.Create(Desc())==0 && f.Create(Desc())==KICH_R4_E_STATE,"no double create");
    Require(f.Clear(Color())==KICH_R4_E_STATE,"no clear without begin");
    Require(f.Begin()==0 && f.Begin()==KICH_R4_E_STATE,"no nested begin");
    Require(f.Present(1)==KICH_R4_E_STATE,"uncleared frame cannot present");
    Require(f.Resize(800,600)==KICH_R4_E_STATE,"resize forbidden mid-frame");
    KichR4FrameColor c=Color();c.rgba[0]=std::numeric_limits<float>::quiet_NaN();
    Require(f.Clear(c)==KICH_R4_E_ARGUMENT,"NaN color rejected");
    c=Color();c.rgba[2]=std::numeric_limits<float>::infinity();Require(f.Clear(c)==KICH_R4_E_ARGUMENT,"infinite color rejected");
    c=Color();c.rgba[1]=-0.1f;Require(f.Clear(c)==KICH_R4_E_ARGUMENT,"negative color rejected");
    c=Color();c.rgba[3]=1.1f;Require(f.Clear(c)==KICH_R4_E_ARGUMENT,"out-of-range color rejected");
    Require(f.Clear(Color())==0 && f.Present(2)==KICH_R4_E_ARGUMENT && f.Present(1)==0,"bad interval preserves repairable frame");
    const size_t calls=d.trace.size();Require(f.Resize(16385,720)==KICH_R4_E_ARGUMENT && d.trace.size()==calls,"invalid resize keeps native target");
    Require(f.Resize(960,540)==0 && d.trace.size()==calls,"same dimensions keep existing view");f.Close();
}
void SuspendAndOcclude()
{
    Driver d;FrameController f(d);Require(f.Create(Desc())==0,"create");
    const size_t calls=d.trace.size();
    Require(f.Resize(0,0)==KICH_R4_FRAME_SUSPENDED && f.Begin()==KICH_R4_FRAME_SUSPENDED,"minimize suspends");
    Require(d.trace.size()==calls && d.view && (f.Stats().flags&KICH_R4_FRAME_PAUSED),"minimize never resizes zero buffer");
    Require(f.Resize(960,540)==0 && d.trace.size()==calls,"restore same size no reallocation");
    d.present_result=occluded;
    Require(f.Begin()==0 && f.Clear(Color())==0 && f.Present(1)==KICH_R4_FRAME_OCCLUDED,"occluded is a status, not failure");
    d.test_result=occluded;
    Require(f.Begin()==KICH_R4_FRAME_OCCLUDED && d.trace.back()=="test","test visibility without rendering");
    KichR4FrameStats s=f.Stats();Require(s.presents==0 && s.present_attempts==1 && s.occlusions==1,"test-only Present excluded from frame counts");
    d.test_result=d.present_result=0;Draw(f);
    Require(!(f.Stats().flags&KICH_R4_FRAME_HIDDEN) && f.Stats().presents==1,"occlusion recovery");
    d.present_result=occluded;Require(f.Begin()==0 && f.Clear(Color())==0 && f.Present(1)==KICH_R4_FRAME_OCCLUDED,"second occlusion");
    d.test_result=error;Require(f.Begin()==error,"device failure during visibility test");
    Require(f.Stats().device_removed_reason==error,"removed reason retained");
    const size_t stopped=d.trace.size();Require(f.Begin()==error && f.Resize(800,600)==error && d.trace.size()==stopped,"broken target never retries native work");
    f.Close();Require(!f.Stats().flags && !d.chain,"failed device still destroys target");
}
void Failures()
{
    for(unsigned mode=0;mode<5;++mode) {
        Driver d;FrameController f(d);
        if(mode<2) {d.fail=mode?"view":"create";Require(f.Create(Desc())==error && !d.chain && !d.view,"partial create cleanup");}
        else {
            Require(f.Create(Desc())==0,"setup");
            if(mode<4) {d.fail=mode==2?"resize":"view";Require(f.Resize(1280,720)==error,"resize/reacquire failure");}
            else {d.present_result=error;Require(f.Begin()==0 && f.Clear(Color())==0 && f.Present(1)==error,"Present failure");}
            Require(f.Stats().last_hresult==error && f.Stats().device_removed_reason==error,"failure diagnostics");
            const size_t calls=d.trace.size();Require(f.Begin()==error && d.trace.size()==calls,"no failure spin");
        }
        f.Close();KichR4FrameStats s=f.Stats();
        Require(!s.flags && s.views_created==s.views_released && !d.chain && !d.view,"failure releases every successful view");
        d.fail.clear();d.present_result=0;Require(f.Create(Desc())==0,"explicit recreate after destruction");Draw(f);f.Close();
    }
}
void ResizeStress()
{
    Driver d;FrameController f(d);Require(f.Create(Desc())==0,"create");
    for(uint32_t i=0;i<200;++i) {
        Require(f.Resize(0,540)==KICH_R4_FRAME_SUSPENDED,"partial zero size suspends");
        Require(f.Resize(640+i,360+i)==0,"varying dimensions");Draw(f);
    }
    Require(f.Stats().resizes==200 && f.Stats().presents==200,"stress counters");
    f.Close();Require(f.Stats().views_created==201 && f.Stats().views_released==201,"resize stress retains no views");
}

Driver* session_driver=NULL;FrameController* session_frame=NULL;
int api_calls=0,api_fail=0;bool live_device=false;
bool Inject(){return ++api_calls==api_fail;}
int32_t CreateDevice(KichR4Handle* h){*h=0;if(Inject())return error;live_device=true;*h=7;return 0;}
int32_t DestroyDevice(KichR4Handle,KichR4FrameStats* out,uint32_t){if(Inject())return error;session_frame->Close();if(out)*out=session_frame->Stats();live_device=false;return 0;}
int32_t CreateTarget(KichR4Handle,const KichR4FrameTargetDesc* d){return Inject()?error:session_frame->Create(*d);}
int32_t DestroyTarget(KichR4Handle){session_frame->Close();return 0;}
int32_t Resize(KichR4Handle,uint32_t w,uint32_t h){return Inject()?error:session_frame->Resize(w,h);}
int32_t Begin(KichR4Handle){return Inject()?error:session_frame->Begin();}
int32_t Clear(KichR4Handle,const KichR4FrameColor* c){return Inject()?error:session_frame->Clear(*c);}
int32_t Present(KichR4Handle,uint32_t interval){return Inject()?error:session_frame->Present(interval);}
int32_t Stats(KichR4Handle,KichR4FrameStats* out,uint32_t){*out=session_frame->Stats();return 0;}
KichR4FrameAPI API(){const KichR4FrameAPI a={sizeof(a),KICH_R4_FRAME_ABI,sizeof(void*),0,
    CreateDevice,DestroyDevice,CreateTarget,DestroyTarget,Resize,Begin,Clear,Present,Stats};return a;}
void Sessions()
{
    for(int fail=0;fail<=7;++fail) {
        Driver d;FrameController f(d);session_driver=&d;session_frame=&f;api_calls=0;api_fail=fail;live_device=false;
        KichR4FrameSession session={};session.api=API();
        Require(KichR4FrameSession::ValidAPI(session.api),"valid frame table");
        if(session.Start(reinterpret_cast<void*>(1),960,540)==0 && session.Resize(800,600)==0)session.Draw(Color());
        KichR4FrameStats final_stats={};
        if(session.Stop(&final_stats)<0)Require(session.Stop(NULL)==0,"cleanup retry after failed destroy");
        Require(!session.device && !live_device && !d.chain && !d.view,"EXE session always releases device/target");
        Require(fail?(session.failure==error):(session.rendered==1 && !session.failure),"frame result not hidden by cleanup");
    }
    KichR4FrameAPI good=API();
    for(int n=0;n<13;++n) {
        KichR4FrameAPI bad=good;
        switch(n){case 0:++bad.struct_size;break;case 1:++bad.abi_version;break;case 2:++bad.pointer_size;break;case 3:bad.flags=1;break;
        case 4:bad.create_device=NULL;break;case 5:bad.destroy_device=NULL;break;case 6:bad.create_target=NULL;break;case 7:bad.destroy_target=NULL;break;
        case 8:bad.resize=NULL;break;case 9:bad.begin_frame=NULL;break;case 10:bad.clear=NULL;break;case 11:bad.present=NULL;break;case 12:bad.get_stats=NULL;break;}
        Require(!KichR4FrameSession::ValidAPI(bad),"bad API rejected before window/device");
    }
}
}
int main(){try{Sequence();Validation();SuspendAndOcclude();Failures();ResizeStress();Sessions();
    printf("B3 production frame controller/session: %d checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
