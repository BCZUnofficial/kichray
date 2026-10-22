#pragma once
#include <stdint.h>
typedef int32_t HRESULT;
typedef unsigned long DWORD;
typedef unsigned int UINT;
#define E_NOTIMPL (-1)
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
enum D3DRESOURCETYPE{D3DRTYPE_TEXTURE=1,D3DRTYPE_CUBETEXTURE,D3DRTYPE_VOLUMETEXTURE};
enum D3DSAMPLERSTATETYPE{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE,D3DSAMP_MAXANISOTROPY,D3DSAMP_MAXMIPLEVEL};
enum{D3DRS_SRGBWRITEENABLE=1,D3DVERTEXTEXTURESAMPLER0=256,MAXD3DDECLLENGTH=64,D3DDECLUSAGE_NORMAL=1,D3DDECLUSAGE_TANGENT,D3DDECLUSAGE_BINORMAL};
struct D3DSURFACE_DESC{DWORD Width,Height,Format,Usage,Pool,MultiSampleType,MultiSampleQuality;};
struct D3DVOLUME_DESC{DWORD Width,Height,Depth,Format,Pool;};
struct D3DVIEWPORT9{DWORD X,Y,Width,Height;float MinZ,MaxZ;};
struct RECT{long left,top,right,bottom;};
struct D3DVERTEXELEMENT9{UINT Stream,Offset,Type,Usage,UsageIndex;};
struct IDirect3DSurface9{int refs=0;bool fail=false;HRESULT GetDesc(D3DSURFACE_DESC* d){*d={640,480,21,1,0,0,0};return fail?-1:0;}void Release(){--refs;}};
struct IDirect3DBaseTexture9{int refs=0;D3DRESOURCETYPE type=D3DRTYPE_TEXTURE;bool fail=false;D3DRESOURCETYPE GetType(){return type;}DWORD GetLevelCount(){return 4;}DWORD GetLOD(){return 1;}void Release(){--refs;}};
struct IDirect3DTexture9:IDirect3DBaseTexture9{HRESULT GetLevelDesc(UINT,D3DSURFACE_DESC* d){*d={256,128,21,1,0,0,0};return fail?-1:0;}};
struct IDirect3DCubeTexture9:IDirect3DBaseTexture9{IDirect3DCubeTexture9(){type=D3DRTYPE_CUBETEXTURE;}HRESULT GetLevelDesc(UINT,D3DSURFACE_DESC* d){*d={64,64,21,1,0,0,0};return fail?-1:0;}};
struct IDirect3DVolumeTexture9:IDirect3DBaseTexture9{IDirect3DVolumeTexture9(){type=D3DRTYPE_VOLUMETEXTURE;}HRESULT GetLevelDesc(UINT,D3DVOLUME_DESC* d){*d={32,16,8,21,0};return fail?-1:0;}};
struct IDirect3DVertexDeclaration9{int refs=0;bool oversize=false;HRESULT GetDeclaration(D3DVERTEXELEMENT9* e,UINT* n){e[0]={0,12,1,D3DDECLUSAGE_NORMAL,0};e[1]={0,24,1,D3DDECLUSAGE_TANGENT,0};e[2]={0,36,1,D3DDECLUSAGE_BINORMAL,0};e[3]={0xff,0,0,0,0};*n=oversize?MAXD3DDECLLENGTH+2:4;return 0;}void Release(){--refs;}};
struct IDirect3DDevice9{
    IDirect3DTexture9 tex;IDirect3DCubeTexture9 cube;IDirect3DVolumeTexture9 volume;IDirect3DSurface9 color,depth;IDirect3DVertexDeclaration9 decl;
    unsigned queries=0,sampler_queries=0,texture_queries=0;bool fail=false,empty=false;
    HRESULT GetTexture(DWORD stage,IDirect3DBaseTexture9** p){++queries;++texture_queries;*p=empty?0:stage==1?static_cast<IDirect3DBaseTexture9*>(&cube):stage==2?static_cast<IDirect3DBaseTexture9*>(&volume):&tex;if(*p)++(*p)->refs;return fail?-1:0;}
    HRESULT GetSamplerState(DWORD,D3DSAMPLERSTATETYPE,DWORD* v){++queries;++sampler_queries;*v=1;return fail?-1:0;}
    HRESULT GetRenderState(DWORD,DWORD* v){++queries;*v=1;return fail?-1:0;}
    HRESULT GetViewport(D3DVIEWPORT9* p){++queries;*p={0,0,640,480,0,1};return fail?-1:0;}
    HRESULT GetScissorRect(RECT* p){++queries;*p={10,20,100,200};return fail?-1:0;}
    HRESULT GetRenderTarget(UINT,IDirect3DSurface9** p){++queries;*p=&color;++color.refs;return fail?-1:0;}
    HRESULT GetDepthStencilSurface(IDirect3DSurface9** p){++queries;*p=&depth;++depth.refs;return fail?-1:0;}
    HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9** p){++queries;*p=&decl;++decl.refs;return fail?-1:0;}
    HRESULT GetFVF(DWORD* p){++queries;*p=2;return fail?-1:0;}
};
