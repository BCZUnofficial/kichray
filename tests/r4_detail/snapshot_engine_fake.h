#pragma once
#include "../../xr_3da/RenderSnapshot.h"
#include "../../xr_3da/KichR4BuildIdentity.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <string>
#define _TRUNCATE (-1)
#define SUCCEEDED(h) ((h)>=0)
#define FAILED(h) ((h)<0)
#define CopyMemory memcpy
#define _RELEASE(p) do{if(p){p->Release();p=NULL;}}while(0)
typedef int HRESULT;
typedef unsigned long DWORD;
template<size_t N>int strcpy_s(char (&out)[N],const char* in){return snprintf(out,N,"%s",in);}
template<size_t N>int strncpy_s(char (&out)[N],const char* in,int){const size_t length=strlen(in),count=length<N?length:N-1;memcpy(out,in,count);out[count]=0;return length<N?0:1;}
inline void Msg(const char*,...){}
enum D3DRENDERSTATETYPE {D3DRS_ZENABLE=1,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAFUNC,D3DRS_ALPHAREF,
    D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_CULLMODE,D3DRS_STENCILENABLE,D3DRS_STENCILFUNC,
    D3DRS_STENCILREF,D3DRS_STENCILMASK,D3DRS_STENCILWRITEMASK,D3DRS_STENCILFAIL,D3DRS_STENCILPASS,D3DRS_STENCILZFAIL,D3DRS_COLORWRITEENABLE,D3DRS_SCISSORTESTENABLE};
enum D3DSAMPLERSTATETYPE {D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER};
enum {D3DPT_POINTLIST=1,D3DPT_LINELIST=2,D3DPT_LINESTRIP=3,D3DPT_TRIANGLELIST=4,D3DPT_TRIANGLESTRIP=5,D3DPT_TRIANGLEFAN=6,HUD_DRAW=16,rsDetails=32};
struct QueryObject {int refs=0;void Release(){--refs;}};
struct IDirect3DVertexShader9:QueryObject{};
struct IDirect3DPixelShader9:QueryObject{};
struct IDirect3DBaseTexture9:QueryObject{};
struct D3DADAPTER_IDENTIFIER9 {char Description[256];};
struct FakeD3D {int GetAdapterIdentifier(unsigned,unsigned,D3DADAPTER_IDENTIFIER9* out){strcpy(out->Description,"test GPU");return 0;}};
struct SnapshotGPU {
    IDirect3DVertexShader9 vs;IDirect3DPixelShader9 ps;IDirect3DBaseTexture9 texture;
    bool fail=false;
    int GetVertexShader(IDirect3DVertexShader9** out){*out=&vs;++vs.refs;return 0;}
    int GetPixelShader(IDirect3DPixelShader9** out){*out=&ps;++ps.refs;return 0;}
    int GetTexture(unsigned i,IDirect3DBaseTexture9** out){*out=i==0?&texture:NULL;if(*out)++texture.refs;return 0;}
    int GetRenderState(D3DRENDERSTATETYPE key,DWORD* out){*out=key==D3DRS_ALPHAREF?200:1;return fail && key==D3DRS_ZENABLE?-1:0;}
    int GetSamplerState(unsigned i,D3DSAMPLERSTATETYPE,DWORD* out){*out=1;return fail && i==1?-1:0;}
};
struct SnapshotHW {FakeD3D* pD3D;SnapshotGPU* pDevice;unsigned DevAdapter;};
struct CResourceManager {const char* SnapshotVSName(IDirect3DVertexShader9*)const{return "bound_vs";}const char* SnapshotPSName(IDirect3DPixelShader9*)const{return "bound_ps";}};
struct Fvector {float x,y,z;};
struct SnapshotDevice {uint32_t dwFrame=7,dwWidth=1920,dwHeight=1080,dwPrecacheFrame=0;float fFOV=75,fTimeGlobal=8;Fvector vCameraPosition={1,2,3},vCameraDirection={0,0,1};CResourceManager* Resources;bool Paused(){return false;}};
struct SnapshotFlags {bool test(unsigned){return true;}};
struct SnapshotConsole {bool bVisible=true;float GetFloat(const char* name,float& value,float& low,float& high){low=0;high=100;value=strstr(name,"radius")?60:.3f;return value;}};
struct CEnvironment {std::string CurrentWeatherName="clear";float wind_strength_factor=.25f;struct Env {Fvector sun_dir={0,-1,0};}CurrentEnv;float SnapshotGameTime()const{return 32000;}};
struct SnapshotPersistent {CEnvironment env;bool menu=false;bool OnRenderPPUI_query(){return menu;}CEnvironment& Environment(){return env;}};
struct SnapshotLevel {bool bReady=true;std::string name()const{return "l01_escape";}};
struct CTexture {std::string cName="grass";};
struct SnapshotCache {CTexture texture;CTexture* get_ActiveTexture(unsigned i){return i==0?&texture:NULL;}};
// CLocatorAPI takes a reference to the exact engine array type, not char*.
typedef char string_path[2 * 260];
struct SnapshotFS {std::string directory;const char* update_path(string_path& out,const char*,const char*){strcpy(out,directory.c_str());return out;}};
extern SnapshotDevice Device;
extern SnapshotHW HW;
extern SnapshotFlags psHUD_Flags,psDeviceFlags;
extern SnapshotConsole* Console;
extern SnapshotPersistent* g_pGamePersistent;
extern SnapshotLevel* g_pGameLevel;
extern SnapshotCache RCache;
extern SnapshotFS FS;
extern std::vector<int> g_loading_events;
extern bool native;
inline bool KichR4_NativeActive(){return native;}
