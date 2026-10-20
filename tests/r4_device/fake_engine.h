#ifndef KICH_B1_FAKE_ENGINE_H
#define KICH_B1_FAKE_ENGINE_H
#define STDAFX_3DA
#define ResourceManagerH
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <stdexcept>
#include "../../xr_3da/KichRenderDevice.h"
#include "../../xr_3da/KichR4Game.h"
#include "../../xr_3da/KichR4Capture.h"
inline void KichR4_CaptureBeginFrame(uint32_t,uint32_t,uint32_t){}
#ifndef KICH_R4_TEST_GAME_HOOKS
inline void KichR4_GameBeforeReset(){}
inline void KichR4_GameShutdown(){}
inline void KichR4_GamePrepareEngineFrame(){}
inline bool KichR4_GamePresentPrepared(){return false;}
#endif

typedef uint32_t u32;
typedef uint64_t u64;
typedef uint32_t DWORD;
typedef DWORD* LPDWORD;
typedef int32_t HRESULT;
typedef int BOOL;
typedef const char* LPCSTR;
typedef void* HWND;
#define TRUE 1
#define FALSE 0
#define SUCCEEDED(h) ((h) >= 0)
#define FAILED(h) ((h) < 0)
#define D3DERR_DEVICELOST static_cast<HRESULT>(0x88760868u)
#define D3DERR_DEVICENOTRESET static_cast<HRESULT>(0x88760869u)
#define D3DCLEAR_TARGET 1u
#define D3DCLEAR_ZBUFFER 2u
#define D3DCLEAR_STENCIL 4u
#define D3DCOLOR_XRGB(r,g,b) (0xff000000u | (u32(r)<<16) | (u32(g)<<8) | u32(b))
#define _SHOW_REF(text,object) Trace(text)
#ifdef DEBUG
#define CHK_DX(expr) do { HRESULT checked_hr=(expr); if (FAILED(checked_hr)) throw std::runtime_error("CHK_DX"); } while (0)
#define VERIFY(expr) do { if (!(expr)) throw std::runtime_error("VERIFY"); } while (0)
#else
#define CHK_DX(expr) (expr)
#define VERIFY(expr) do {} while (0)
#endif

enum FakeStates
{
    D3DSAMP_MAXANISOTROPY=1, D3DSAMP_MIPMAPLODBIAS, D3DSAMP_MINFILTER,
    D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR,
    D3DRS_DITHERENABLE, D3DRS_COLORVERTEX, D3DRS_ZENABLE, D3DRS_SHADEMODE,
    D3DSHADE_GOURAUD, D3DRS_CULLMODE, D3DCULL_CCW, D3DRS_ALPHAFUNC,
    D3DCMP_GREATER, D3DRS_LOCALVIEWER, D3DRS_DIFFUSEMATERIALSOURCE,
    D3DMCS_MATERIAL, D3DRS_SPECULARMATERIALSOURCE, D3DRS_AMBIENTMATERIALSOURCE,
    D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_COLOR1, D3DRS_MULTISAMPLEANTIALIAS,
    D3DRS_NORMALIZENORMALS, D3DRS_FILLMODE, D3DFILL_WIREFRAME, D3DFILL_SOLID,
    D3DRS_FOGCOLOR, D3DRS_RANGEFOGENABLE, D3DRS_FOGTABLEMODE, D3DFOG_LINEAR,
    D3DRS_FOGVERTEXMODE, D3DFOG_NONE, D3DRS_STENCILENABLE, D3DRS_STENCILFUNC,
    D3DCMP_ALWAYS, D3DRS_STENCILREF, D3DRS_STENCILMASK, D3DRS_STENCILWRITEMASK,
    D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP, D3DRS_STENCILPASS, D3DSTENCILOP_INCRSAT,
    D3DRS_STENCILZFAIL, D3DCMP_EQUAL, D3DPT_TRIANGLESTRIP,
    CULL_CW, CULL_CCW, rsWireframe, rsClearBB, rp_DeviceReset
};

extern std::vector<std::string> events;
inline void Trace(const std::string& s) { events.push_back(s); }
inline void TraceNumber(const char* name, uint64_t a, uint64_t b=0, uint64_t c=0)
{
    char text[160];
    snprintf(text,sizeof(text),"%s:%llu:%llu:%llu", name,
        static_cast<unsigned long long>(a),static_cast<unsigned long long>(b),static_cast<unsigned long long>(c));
    Trace(text);
}
inline void Msg(const char* fmt,...)
{
    char text[768]; va_list args; va_start(args,fmt); vsnprintf(text,sizeof(text),fmt,args); va_end(args);
    if (!strstr(text,"[R4 bridge B1]") && !strstr(text,"[R4 bridge B2]")) Trace(std::string("Msg:")+text);
}
inline void Log(const char* text) { Trace(text); }

template <class T> struct _matrix
{
    T values[16];
    void identity() { memset(values,0,sizeof(values)); for(int i=0;i<4;++i)values[i*5]=1; Trace("matrix.identity"); }
};
struct Fvector
{
    float x,y,z;
    void set(float a,float b,float c) { x=a;y=b;z=c;Trace("vector.set"); }
};
typedef Fmatrix D3DXMATRIX;
inline D3DXMATRIX* D3DXMatrixInverse(D3DXMATRIX* dest,float*,const D3DXMATRIX* src)
{
    *dest=*src; Trace("matrix.inverse"); return dest;
}
namespace FVF
{
const DWORD F_TL=123;
struct TL
{
    float x,y;u32 color;
    void set(float a,float b,u32 c,float,float) { x=a;y=b;color=c; }
};
}

struct FakeDevice
{
    HRESULT cooperative, present, begin;
    FakeDevice():cooperative(0),present(0),begin(0) {}
    HRESULT TestCooperativeLevel() { Trace("TestCooperativeLevel"); return cooperative; }
    HRESULT BeginScene() { Trace("BeginScene"); return begin; }
    HRESULT EndScene() { Trace("EndScene"); return 0; }
    HRESULT Present(void*,void*,void*,void*) { Trace("Present"); return present; }
    HRESULT Clear(u32,const void*,DWORD flags,DWORD color,float depth,DWORD stencil)
    { TraceNumber("Clear",flags,color,stencil); TraceNumber("clear.depth",depth==1.f); return 0; }
    HRESULT SetSamplerState(u32 stage,int state,DWORD value)
    { TraceNumber("Sampler",stage,state,value); return 0; }
    HRESULT SetRenderState(int state,DWORD value)
    { TraceNumber("State",state,value); return 0; }
    HRESULT SetFVF(DWORD value) { TraceNumber("FVF",value); return 0; }
    HRESULT DrawPrimitiveUP(int kind,u32 count,const void* data,u32 stride)
    {
        TraceNumber("DrawUP",kind,count,stride);
        const FVF::TL* v=static_cast<const FVF::TL*>(data);
        for(int i=0;i<4;++i){TraceNumber("vertex",u32(v[i].x),u32(v[i].y),v[i].color);}
        return 0;
    }
};
extern FakeDevice gpu;
struct FakeCaps
{
    BOOL bForceGPU_SW,bForceGPU_NonPure,bForceGPU_REF,bTableFog,bStencil;
    int SceneMode;
    struct {u32 dwStages;} raster;
    FakeCaps():bForceGPU_SW(0),bForceGPU_NonPure(0),bForceGPU_REF(0),bTableFog(0),bStencil(0),SceneMode(0){raster.dwStages=2;}
    void Update() { Trace("caps.update"); }
};
extern u32 next_width,next_height;
struct FakeHW
{
    FakeCaps Caps;
    FakeDevice* pDevice;
    struct {u32 BackBufferWidth,BackBufferHeight;} DevPP;
    FakeHW():pDevice(NULL){DevPP.BackBufferWidth=800;DevPP.BackBufferHeight=600;}
    void Validate()
    {
#ifdef DEBUG
        Trace("HW.Validate");
#endif
    }
    void CreateDevice(HWND) { pDevice=&gpu;Trace("HW.Create"); }
    void Reset(HWND) { Trace("HW.Reset");DevPP.BackBufferWidth=next_width;DevPP.BackBufferHeight=next_height;gpu.cooperative=0; }
    void DestroyDevice() { Trace("HW.Destroy");pDevice=NULL; }
};
extern FakeHW HW;
struct FakeCache
{
    void OnDeviceCreate(){Trace("cache.create");}
    void OnDeviceDestroy(){Trace("cache.destroy");}
    void OnFrameBegin(){Trace("cache.begin");}
    void OnFrameEnd(){Trace("cache.end");}
    void set_CullMode(int value){TraceNumber("cull",value);}
    void set_xform_view(const Fmatrix&){Trace("cache.view");}
    void set_xform_project(const Fmatrix&){Trace("cache.projection");}
};
extern FakeCache RCache;
struct FakeMemory
{
    void mem_compact(){Trace("mem.compact");}
    void dbg_check(){Trace("mem.check");}
    u32 mem_usage(){Trace("mem.usage");return 1024;}
};
extern FakeMemory Memory;
struct FakeFlags
{
    bool wire,clear;
    FakeFlags():wire(false),clear(false){}
    bool test(int flag) const{return flag==rsWireframe?wire:clear;}
};
extern FakeFlags psDeviceFlags;
namespace FPU {inline void m24r(){Trace("FPU.m24r");}}
inline void Sleep(u32 t){TraceNumber("Sleep",t);}
inline void ShowCursor(BOOL b){TraceNumber("cursor",b);}

class CStatTimer
{
  public:
    u64 accum;
    CStatTimer():accum(0){}
    void FrameStart(){Trace("timer.FrameStart");accum=0;}
    void Begin(){Trace("timer.Begin");}
    void End(){Trace("timer.End");accum=7654;}
    void FrameEnd(){Trace("timer.FrameEnd");}
};
class CStats
{
  public:
    CStatTimer RenderTOTAL_Real,RenderTOTAL;
    CStats(){Trace("stats.new");}
    ~CStats(){Trace("stats.delete");}
    void OnDeviceCreate(){Trace("stats.create");}
    void OnDeviceDestroy(){Trace("stats.destroy");}
};
class CResourceManager
{
  public:
    CResourceManager(){Trace("resources.new");}
    ~CResourceManager(){Trace("resources.delete");}
    void OnDeviceCreate(LPCSTR){Trace("resources.create");}
    void OnDeviceDestroy(BOOL){Trace("resources.destroy");}
    void reset_begin();
    void reset_end();
    void DestroyNecessaryTextures(){Trace("resources.necessary.destroy");}
};
struct FakeShader
{
    void create(LPCSTR name){Trace(std::string("shader.create:")+name);}
    void destroy(){Trace("shader.destroy");}
};
struct FakeGamma {void Update(){Trace("gamma.update");}};
struct FakeDrawUtils
{
    void OnDeviceCreate(){Trace("DU.create");}
    void OnDeviceDestroy(){Trace("DU.destroy");}
};
extern FakeDrawUtils DU;
struct FakeLight
{
    void set_active(bool){Trace("light.active");}
    void set_shadow(bool){Trace("light.shadow");}
    void set_position(const Fvector&){Trace("light.position");}
    void set_color(float,float,float){Trace("light.color");}
    void set_range(float){Trace("light.range");}
};
extern FakeLight light;
struct ref_light
{
    FakeLight* ptr;
    ref_light(int=0):ptr(NULL){}
    explicit ref_light(FakeLight* p):ptr(p){}
    operator bool()const{return ptr!=NULL;}
    FakeLight* operator->()const{return ptr;}
    void destroy(){Trace("light.destroy");ptr=NULL;}
};
struct FakeRenderer
{
    void create(){Trace("renderer.create");}
    void destroy(){Trace("renderer.destroy");}
    ref_light light_create(){Trace("light.create");return ref_light(&light);}
};
extern FakeRenderer* Render;
struct FakeSound {void set_master_volume(float v){TraceNumber("sound.volume",v==1.f);}};
extern FakeSound* Sound;
struct FakeApp
{
    void load_draw_internal(){Trace("loading.draw");}
    void destroy_loading_shaders(){Trace("loading.destroy");}
};
extern FakeApp* pApp;
inline void CheckPrivilegySlowdown(){Trace("privilege.check");}
struct FakeHUD {void OnScreenRatioChanged(){Trace("HUD.ratio");}};
struct FakeLevel {FakeHUD* pHUD;};
extern FakeLevel* g_pGameLevel;
extern void* g_pGamePersistent;
extern BOOL bNeed_re_create_env,g_bRendering;
extern ref_light precache_light;
struct FakeSeq
{
    struct {void clear(){Trace("seq.clear");}} R;
    void clear(){Trace("seqParallel.clear");}
    void Process(int){Trace("seq.reset");}
};
typedef char string_path[260];
struct FakeFS {void update_path(char* dest,LPCSTR,LPCSTR name){strcpy(dest,name);Trace("FS.path");}};
extern FakeFS FS;
extern u32 psCurrentVidMode[2];
template<class T> T* xr_new(){return new T;}
template<class T> void xr_delete(T*& p){delete p;p=NULL;}

class CRenderDevice
{
  public:
    HWND m_hWnd;
    u32 dwWidth,dwHeight,dwFrame,dwPrecacheFrame,dwPrecacheTotal;
    float fWidth_2,fHeight_2,fFOV,fASPECT;
    BOOL b_is_Ready;
    Fmatrix mView,mProject,mFullTransform,mInvFullTransform;
    Fvector vCameraPosition,vCameraDirection,vCameraTop,vCameraRight;
    CStats* Statistic;
    CResourceManager* Resources;
    FakeGamma Gamma;
    FakeShader m_WireShader,m_SelectionShader;
    FakeSeq seqRender,seqAppActivate,seqAppDeactivate,seqAppStart,seqAppEnd,seqFrame,seqFrameMT,seqDeviceReset,seqParallel;
    CRenderDevice():m_hWnd(NULL),dwWidth(800),dwHeight(600),dwFrame(0),dwPrecacheFrame(0),dwPrecacheTotal(0),
        fWidth_2(400),fHeight_2(300),fFOV(90),fASPECT(1),b_is_Ready(FALSE),Statistic(NULL),Resources(NULL){}
    u32 TimerAsync(){static u32 tick=0;return ++tick;}
    void _SetupStates();void _Create(LPCSTR);void Create();void _Destroy(BOOL);void Destroy();void Reset(bool=true);
    BOOL Begin();void Clear();void End();void PreCache(u32);void overdrawBegin();void overdrawEnd();
};
extern CRenderDevice Device;
#endif
