#include "../r4_device/fake_engine.h"
#include <algorithm>
namespace {
bool r4_output=false;
int checks=0;u32 captured_precache=0;
void Require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
bool Before(const char* a,const char* b){const auto first=std::find(events.begin(),events.end(),a),second=std::find(events.begin(),events.end(),b);
    return first!=events.end() && second!=events.end() && first<second;}
}
void KichR4_GamePrepareEngineFrame(){captured_precache=Device.dwPrecacheFrame;Trace("R4.prepare");}
void KichR4_GameBeforeReset(){Trace("R4.close-before-reset");}
void KichR4_GameShutdown(){Trace("R4.shutdown");}
bool KichR4_GamePresentPrepared(){Trace("R4.try-present");return r4_output;}
int main(){try {
    IKichRenderDevice& backend=KichRenderDevice();KichRenderDeviceSize size={};backend.CreateHW(reinterpret_cast<void*>(1),size);
    events.clear();backend.BeginFrame();backend.EndFrame();
    Require(Before("EndScene","R4.try-present") && Before("R4.try-present","Present"),"legacy presentation after DX9 EndScene and R4 decision");
    r4_output=true;events.clear();backend.BeginFrame();backend.EndFrame();
    Require(Before("EndScene","R4.try-present"),"DX9 scene finishes before native R4 begins");
    Require(std::find(events.begin(),events.end(),"Present")==events.end(),"DX9 Present skipped for an R4-owned frame");
    r4_output=false;events.clear();backend.BeginFrame();backend.EndFrame();
    Require(std::count(events.begin(),events.end(),"Present")==1,"immediate legacy fallback presents exactly once");
    events.clear();backend.ResetHW(reinterpret_cast<void*>(1),size);
    Require(Before("R4.close-before-reset","HW.Reset"),"R4 releases device and target before DX9 reset");
    events.clear();backend.DestroyHW();Require(Before("R4.shutdown","HW.Destroy"),"R4 shuts down before legacy hardware destruction");
    Device.Create();Device.PreCache(1);events.clear();Require(Device.Begin()!=FALSE,"last precache frame begins");Device.End();
    Require(captured_precache==1 && Device.dwPrecacheFrame==0 && Before("R4.prepare","loading.draw"),"eligibility captured before decrementing last precache frame");
    Device.Destroy();
    printf("B4.3a production DX9 presenter / reset seam: %d checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
