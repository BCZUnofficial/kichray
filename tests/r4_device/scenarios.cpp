#include "fake_engine.h"

std::vector<std::string> events;
FakeDevice gpu;
FakeHW HW;
FakeCache RCache;
FakeMemory Memory;
FakeFlags psDeviceFlags;
FakeDrawUtils DU;
FakeFS FS;
FakeLight light;
FakeRenderer renderer;
FakeRenderer* Render=&renderer;
FakeSound sound;
FakeSound* Sound=&sound;
FakeApp app;
FakeApp* pApp=&app;
FakeHUD hud;
FakeLevel level={&hud};
FakeLevel* g_pGameLevel=&level;
void* g_pGamePersistent=&app;
BOOL bNeed_re_create_env=FALSE,g_bRendering=FALSE;
ref_light precache_light;
CRenderDevice Device;
u32 next_width=1280,next_height=720;
u32 psCurrentVidMode[2]={800,600};

void CResourceManager::reset_begin(){TraceNumber("resources.reset_begin",Device.dwWidth,Device.dwHeight);}
void CResourceManager::reset_end(){TraceNumber("resources.reset_end",Device.dwWidth,Device.dwHeight);}

static int checks=0;
static void Require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
static void Dump(const char* scenario)
{
    printf("SCENARIO %s\n",scenario);
    for(size_t i=0;i<events.size();++i)puts(events[i].c_str());
    events.clear();
}
static void Init(bool stencil,int overdraw,bool wire,bool fog)
{
    Device=CRenderDevice();gpu=FakeDevice();HW=FakeHW();psDeviceFlags=FakeFlags();
    HW.Caps.bStencil=stencil;HW.Caps.SceneMode=overdraw;HW.Caps.bTableFog=fog;
    psDeviceFlags.wire=wire;psDeviceFlags.clear=true;precache_light=ref_light();
    bNeed_re_create_env=FALSE;g_bRendering=FALSE;events.clear();
    Device.Create();
}
int main()
{
    try
    {
        for(int mode=0;mode<3;++mode)
        for(int bits=0;bits<8;++bits)
        {
            Init(bits&1,mode,bits&2,bits&4);
            Require(Device.b_is_Ready,"ready after create");
            Require(Device.dwWidth==800 && Device.dwHeight==600,"create dimensions");
            size_t creation_events=events.size();Device.Create();
            Require(events.size()==creation_events,"duplicate create is ignored");
            if(Device.Begin()){Device.Clear();Device.End();}
            psDeviceFlags.clear=false;
            if(Device.Begin()){Device.Clear();Device.End();}
            Device.PreCache(1);
            if(Device.Begin()){Device.Clear();Device.End();}
            next_width=1281;next_height=721;Device.Reset(false);
            Require(Device.dwWidth==1281 && Device.dwHeight==721,"new dimensions published");
            Require(Device.fWidth_2==640 && Device.fHeight_2==360,"legacy integer half-size preserved");
            Require(bNeed_re_create_env,"environment refresh requested");
            Device.Reset(true);
            if(Device.Begin()){Device.Clear();Device.End();}
            Device.Destroy();
            Require(!Device.b_is_Ready && !Device.Resources && !Device.Statistic,"destroy resources before device");
            size_t destroyed_events=events.size();Device.Destroy();
            Require(events.size()==destroyed_events,"duplicate destroy is ignored");
            char name[64];snprintf(name,sizeof(name),"normal-mode%d-bits%d",mode,bits);Dump(name);
        }

        Init(true,0,false,true);
        gpu.cooperative=D3DERR_DEVICELOST;
#ifndef DEDICATED_SERVER
        Require(!Device.Begin(),"lost device skips the frame");
        Require(!g_bRendering,"lost device does not enter rendering");
        gpu.cooperative=D3DERR_DEVICENOTRESET;next_width=1600;next_height=900;
        Require(Device.Begin(),"not-reset device is restored");
        Require(Device.dwWidth==1600 && Device.dwHeight==900,"reset-on-Begin dimensions");
        Device.End();
        gpu.present=D3DERR_DEVICELOST;
        Require(Device.Begin(),"Present loss happens after begin");Device.End();
        gpu.present=0;
        gpu.cooperative=static_cast<HRESULT>(0x80004005u);
        Require(Device.Begin(),"unexpected cooperative error retains legacy path");Device.End();
#else
        Require(Device.Begin(),"dedicated Begin retains no-op behavior");Device.End();
#endif
        Device.Destroy();Dump("device-loss");

        Init(false,0,false,false);
        HW.Caps.bForceGPU_REF=TRUE;Device.PreCache(20);
        Require(!Device.dwPrecacheFrame,"reference device disables precache");
        Device.Destroy();Dump("reference-device");

#ifdef TEST_B1
        // Adapter-only contracts that have no legacy external entry point.
        IKichRenderDevice& backend=KichRenderDevice();
        backend.SetupGPU(true,true,true);
        Require(HW.Caps.bForceGPU_SW && HW.Caps.bForceGPU_NonPure && backend.IsReferenceDevice(),"GPU options enabled");
        backend.SetupGPU(false,false,false);
        Require(!HW.Caps.bForceGPU_SW && !HW.Caps.bForceGPU_NonPure && !backend.IsReferenceDevice(),"GPU options cleared");
        CStatTimer real,total;
        backend.BeginFrameTiming(real);backend.EndFrameTiming(real,total);
        Require(total.accum==7654,"render timing accumulator forwarded");
        Fmatrix v,p,full,inverse;v.identity();p.identity();full.identity();
        backend.SetCameraMatrices(v,p,full,inverse);
        Require(inverse.values[0]==1 && inverse.values[15]==1,"camera inverse assigned");
        events.clear();
#endif
        fprintf(stderr,"B1 regression scenario assertions: %d passed\n",checks);
        return 0;
    }
    catch(const std::exception& e){fprintf(stderr,"FAILED: %s\n",e.what());return 1;}
}
