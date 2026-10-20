#ifndef KICH_R4_CAPTURE_FAKE_ENGINE_H
#define KICH_R4_CAPTURE_FAKE_ENGINE_H
#include "../../xr_3da/KichR4CaptureInput.h"
#include "../../xr_3da/KichR4TestScene.h"
#include <vector>
#include <map>
#include <string>
#if defined(KICH_R4_REAL_SDK)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d9.h>
#else
typedef uint32_t DWORD;typedef int32_t LONG;typedef unsigned UINT;typedef uint32_t HRESULT;
#define SUCCEEDED(hr) (static_cast<int32_t>(hr)>=0)
#define FAILED(hr) (static_cast<int32_t>(hr)<0)
enum D3DRENDERSTATETYPE {D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_ALPHAFUNC,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAREF,D3DRS_CULLMODE,D3DRS_SCISSORTESTENABLE};
enum {D3DBLENDOP_ADD=1,D3DCULL_CW=2,D3DCULL_CCW=3,D3DDECLUSAGE_POSITIONT=9,D3DDECLUSAGE_COLOR=10,D3DDECLUSAGE_TEXCOORD=5,
    D3DDECLTYPE_FLOAT4=3,D3DDECLTYPE_D3DCOLOR=4,D3DDECLTYPE_FLOAT2=1,MAX_FVF_DECL_SIZE=65};
struct RECT {LONG left,top,right,bottom;};
struct D3DVIEWPORT9 {DWORD X,Y,Width,Height;float MinZ,MaxZ;};
struct D3DVERTEXELEMENT9 {uint16_t Stream,Offset;unsigned char Type,Method,Usage,UsageIndex;};
struct IDirect3DVertexDeclaration9 {
    std::vector<D3DVERTEXELEMENT9> elements;
    HRESULT GetDeclaration(D3DVERTEXELEMENT9* out,UINT* n){if(*n<elements.size())return 0x80004005u;*n=static_cast<UINT>(elements.size());std::copy(elements.begin(),elements.end(),out);return 0;}
};
struct IDirect3DDevice9 {
    std::map<D3DRENDERSTATETYPE,DWORD> states;D3DVIEWPORT9 view;RECT rect;
    IDirect3DDevice9(){view={0,0,960,540,0,1};rect={10,20,300,400};states[D3DRS_SRCBLEND]=5;states[D3DRS_DESTBLEND]=6;
        states[D3DRS_BLENDOP]=1;states[D3DRS_ALPHAFUNC]=5;states[D3DRS_CULLMODE]=3;}
    HRESULT GetRenderState(D3DRENDERSTATETYPE key,DWORD* value){*value=states[key];return 0;}
    HRESULT GetViewport(D3DVIEWPORT9* value){*value=view;return 0;}
    HRESULT GetScissorRect(RECT* value){*value=rect;return 0;}
};
#endif
typedef uint16_t u16;
struct Fmatrix {
    float m[16];Fmatrix(){const kich_r4_scene::Matrix identity=kich_r4_scene::Identity();memcpy(m,identity.m,sizeof(m));}
    void mul(const Fmatrix& projection,const Fmatrix& view){kich_r4_scene::Matrix a,b;memcpy(a.m,view.m,sizeof(a));memcpy(b.m,projection.m,sizeof(b));const auto r=kich_r4_scene::Multiply(a,b);memcpy(m,r.m,sizeof(m));}
};
struct CTexture {
    std::string cName;bool user;void* pAVI;void* pTheora;std::vector<void*> seqDATA;
    CTexture():user(false),pAVI(NULL),pTheora(NULL){}bool isUser(){return user;}
};
struct CaptureDevice {uint32_t dwWidth,dwHeight;Fmatrix mView,mProject;CaptureDevice():dwWidth(960),dwHeight(540){}};
struct CaptureCache {
    Fmatrix world,view,projection;CTexture* texture;CaptureCache():texture(NULL){}
    const Fmatrix& get_xform_world(){return world;}const Fmatrix& get_xform_view(){return view;}const Fmatrix& get_xform_project(){return projection;}
    CTexture* get_ActiveTexture(unsigned){return texture;}
};
struct CaptureHardware {IDirect3DDevice9* pDevice;};
class CKinematics {
public:std::vector<Fmatrix> bones;u16 LL_BoneCount(){return static_cast<u16>(bones.size());}Fmatrix& LL_GetTransform_R(u16 i){return bones.at(i);}
};
extern CaptureDevice Device;extern CaptureCache RCache;extern CaptureHardware HW;
#endif
