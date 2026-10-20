/* Compile-only seam: real Windows/D3D9 SDK interfaces; engine services declared. */
#ifndef KICH_B1_WIN32_COMPILE_H
#define KICH_B1_WIN32_COMPILE_H
#define STDAFX_3DA
#define ResourceManagerH
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <stdint.h>
#include "../../xr_3da/KichRenderDevice.h"
typedef uint32_t u32;
typedef uint64_t u64;
#define CHK_DX(expr) (expr)
#define VERIFY(expr) do { (void)(expr); } while(0)
#define _SHOW_REF(text,object) do { (void)(object); } while(0)
template<class T> struct _matrix { T elements[16]; };
class CStatTimer
{
  public:
    u64 accum;
    void FrameStart();void Begin();void End();void FrameEnd();
};
struct B1CompileCaps
{
    BOOL bForceGPU_SW,bForceGPU_NonPure,bForceGPU_REF,bTableFog,bStencil;
    int SceneMode;
    struct {u32 dwStages;} raster;
    void Update();
};
struct B1CompileHW
{
    IDirect3DDevice9* pDevice;
    D3DPRESENT_PARAMETERS DevPP;
    B1CompileCaps Caps;
    void CreateDevice(HWND);void Reset(HWND);void DestroyDevice();void Validate();
};
extern B1CompileHW HW;
struct B1CompileCache
{
    void OnDeviceCreate();void OnDeviceDestroy();void OnFrameBegin();void OnFrameEnd();
    void set_CullMode(u32);void set_xform_view(const Fmatrix&);void set_xform_project(const Fmatrix&);
};
extern B1CompileCache RCache;
struct B1CompileMemory {void dbg_check();};
extern B1CompileMemory Memory;
struct B1CompileFlags {bool test(u32)const;};
extern B1CompileFlags psDeviceFlags;
class CResourceManager {
public:
    void OnDeviceCreate(const char*); void OnDeviceDestroy(BOOL);
    void reset_begin(); void reset_end(); void DestroyNecessaryTextures();
};
struct B2CompileDevice { CResourceManager* Resources; };
extern B2CompileDevice Device;
template<class T> T* xr_new() { return new T; }
template<class T> void xr_delete(T*& p) { delete p; p=NULL; }
const u32 rsWireframe=1,CULL_CW=D3DCULL_CW,CULL_CCW=D3DCULL_CCW;
namespace FVF
{
const u32 F_TL=D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1;
struct TL {float x,y,z,rhw;u32 color;float u,v;void set(float,float,u32,float,float);};
}
void Msg(const char*,...);
#endif
