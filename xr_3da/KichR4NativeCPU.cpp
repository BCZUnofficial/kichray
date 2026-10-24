#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "KichR4NativeCPU.h"
#include "KichR4Native.h"
#include <vector>
#include <string>
#include <new>
#include <string.h>

namespace {
struct Bytes {virtual ~Bytes(){};virtual const unsigned char* Data(uint32_t&)const=0;};
template<class Interface,class Desc,D3DRESOURCETYPE Type> class Buffer:public Interface,public Bytes {
    ULONG refs;std::vector<unsigned char> data;
public:
    explicit Buffer(uint32_t bytes):refs(1),data(bytes){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out){if(!out)return E_POINTER;*out=NULL;
        if(iid==__uuidof(IUnknown) || iid==__uuidof(IDirect3DResource9) || iid==__uuidof(Interface)){*out=static_cast<Interface*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef(){return ++refs;}
    ULONG STDMETHODCALLTYPE Release(){const ULONG n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out){if(out)*out=NULL;return D3DERR_INVALIDCALL;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,const void*,DWORD,DWORD){return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,void*,DWORD*){return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID){return E_NOTIMPL;}
    DWORD STDMETHODCALLTYPE SetPriority(DWORD){return 0;}
    DWORD STDMETHODCALLTYPE GetPriority(){return 0;}
    void STDMETHODCALLTYPE PreLoad(){}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType(){return Type;}
    HRESULT STDMETHODCALLTYPE Lock(UINT offset,UINT count,void** out,DWORD){if(!out)return E_POINTER;*out=NULL;
        if(offset>data.size() || (count && count>data.size()-offset))return D3DERR_INVALIDCALL;
        *out=data.data()+offset;return S_OK;}
    HRESULT STDMETHODCALLTYPE Unlock(){return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDesc(Desc* out){if(!out)return E_POINTER;memset(out,0,sizeof(*out));out->Size=static_cast<UINT>(data.size());out->Type=Type;out->Pool=D3DPOOL_SYSTEMMEM;out->Format=Type==D3DRTYPE_INDEXBUFFER?D3DFMT_INDEX16:D3DFMT_VERTEXDATA;return S_OK;}
    const unsigned char* Data(uint32_t& bytes)const {bytes=static_cast<uint32_t>(data.size());return data.data();}
};
class Declaration:public IDirect3DVertexDeclaration9 {
    ULONG refs;
public:
    std::vector<D3DVERTEXELEMENT9> elements;
    virtual ~Declaration(){}
    explicit Declaration(const D3DVERTEXELEMENT9* p):refs(1){for(unsigned i=0;i<65;++i){elements.push_back(p[i]);if(p[i].Stream==255)return;}throw std::bad_alloc();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out){if(!out)return E_POINTER;*out=NULL;if(iid!=__uuidof(IUnknown) && iid!=__uuidof(IDirect3DVertexDeclaration9))return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef(){return ++refs;}
    ULONG STDMETHODCALLTYPE Release(){const ULONG n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out){if(out)*out=NULL;return D3DERR_INVALIDCALL;}
    HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* out,UINT* count){if(!count)return E_POINTER;const UINT n=static_cast<UINT>(elements.size());
        if(out){if(*count<n)return D3DERR_INVALIDCALL;memcpy(out,elements.data(),n*sizeof(*out));}*count=n;return S_OK;}
};
class Texture:public IDirect3DTexture9 {
    ULONG refs;uint32_t width,height;std::vector<unsigned char> pixels;
public:
    std::string name;uint64_t revision;
    Texture(const char* n,uint32_t w,uint32_t h):refs(1),width(w),height(h),name(n?n:""),revision(0){}
    virtual ~Texture(){KichR4_NativeForgetTexture(this);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out){if(!out)return E_POINTER;*out=NULL;
        if(iid!=__uuidof(IUnknown) && iid!=__uuidof(IDirect3DResource9) && iid!=__uuidof(IDirect3DBaseTexture9) && iid!=__uuidof(IDirect3DTexture9))return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef(){return ++refs;}
    ULONG STDMETHODCALLTYPE Release(){const ULONG n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out){if(out)*out=NULL;return D3DERR_INVALIDCALL;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,const void*,DWORD,DWORD){return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,void*,DWORD*){return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID){return E_NOTIMPL;}
    DWORD STDMETHODCALLTYPE SetPriority(DWORD){return 0;}
    DWORD STDMETHODCALLTYPE GetPriority(){return 0;}
    void STDMETHODCALLTYPE PreLoad(){}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType(){return D3DRTYPE_TEXTURE;}
    DWORD STDMETHODCALLTYPE SetLOD(DWORD){return 0;}
    DWORD STDMETHODCALLTYPE GetLOD(){return 0;}
    DWORD STDMETHODCALLTYPE GetLevelCount(){return 1;}
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE){return S_OK;}
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType(){return D3DTEXF_LINEAR;}
    void STDMETHODCALLTYPE GenerateMipSubLevels(){}
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level,D3DSURFACE_DESC* out){if(!out || level)return D3DERR_INVALIDCALL;memset(out,0,sizeof(*out));out->Width=width;out->Height=height;out->Format=D3DFMT_A8R8G8B8;out->Type=D3DRTYPE_SURFACE;out->Pool=D3DPOOL_SYSTEMMEM;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT,IDirect3DSurface9** out){if(out)*out=NULL;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE LockRect(UINT level,D3DLOCKED_RECT* out,const RECT* rect,DWORD){if(level || !out)return D3DERR_INVALIDCALL;
        if(rect && (rect->left<0 || rect->top<0 || rect->right>static_cast<LONG>(width) || rect->bottom>static_cast<LONG>(height) || rect->right<rect->left || rect->bottom<rect->top))return D3DERR_INVALIDCALL;
        try{pixels.resize(static_cast<size_t>(width)*height*4);}catch(...){return E_OUTOFMEMORY;}
        out->Pitch=static_cast<INT>(width*4);out->pBits=pixels.data()+(rect?(rect->top*width+rect->left)*4:0);return S_OK;}
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT level){if(level)return D3DERR_INVALIDCALL;++revision;KichR4_NativeTextureChanged(this);return S_OK;}
    HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT*){++revision;KichR4_NativeTextureChanged(this);return S_OK;}
    const unsigned char* Pixels(uint32_t& w,uint32_t& h,uint64_t& r)const {w=width;h=height;r=revision;return pixels.empty()?NULL:pixels.data();}
};
class StateBlock:public IDirect3DStateBlock9 {
    ULONG refs;std::vector<KichR4CPUState> states;
public:
    virtual ~StateBlock(){}
    StateBlock(const KichR4CPUState* p,uint32_t n):refs(1),states(p,p+n){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out){if(!out)return E_POINTER;*out=NULL;if(iid!=__uuidof(IUnknown) && iid!=__uuidof(IDirect3DStateBlock9))return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef(){return ++refs;}
    ULONG STDMETHODCALLTYPE Release(){const ULONG n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out){if(out)*out=NULL;return D3DERR_INVALIDCALL;}
    HRESULT STDMETHODCALLTYPE Capture(){return S_OK;}
    HRESULT STDMETHODCALLTYPE Apply(){for(size_t i=0;i<states.size();++i){const KichR4CPUState& s=states[i];
        if(!s.type)KichR4_NativeSetState(s.a,s.b);
        else if(s.type==2)KichR4_NativeSetSampler(s.a,s.b,s.c);}
        return S_OK;}
};
}
HRESULT KichR4_CPUVertexBuffer(uint32_t bytes,IDirect3DVertexBuffer9** out){if(!out || !bytes || bytes>256u*1024u*1024u)return E_INVALIDARG;*out=NULL;
    try{*out=new Buffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC,D3DRTYPE_VERTEXBUFFER>(bytes);return S_OK;}catch(...){return E_OUTOFMEMORY;}}
HRESULT KichR4_CPUIndexBuffer(uint32_t bytes,IDirect3DIndexBuffer9** out){if(!out || !bytes || bytes>128u*1024u*1024u)return E_INVALIDARG;*out=NULL;
    try{*out=new Buffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC,D3DRTYPE_INDEXBUFFER>(bytes);return S_OK;}catch(...){return E_OUTOFMEMORY;}}
HRESULT KichR4_CPUDeclaration(const D3DVERTEXELEMENT9* p,IDirect3DVertexDeclaration9** out){if(!p || !out)return E_INVALIDARG;*out=NULL;
    try{*out=new Declaration(p);return S_OK;}catch(...){return E_OUTOFMEMORY;}}
HRESULT KichR4_CPUTexture(const char* name,uint32_t w,uint32_t h,IDirect3DTexture9** out){if(!out || !w || !h || w>16384 || h>16384)return E_INVALIDARG;*out=NULL;
    try{*out=new Texture(name,w,h);return S_OK;}catch(...){return E_OUTOFMEMORY;}}
const unsigned char* KichR4_CPUBuffer(void* p,uint32_t& bytes){bytes=0;if(!p)return NULL;const Bytes* b=dynamic_cast<Bytes*>(static_cast<IUnknown*>(p));return b?b->Data(bytes):NULL;}
const D3DVERTEXELEMENT9* KichR4_CPUElements(void* p,uint32_t& count){count=0;if(!p)return NULL;const Declaration* d=dynamic_cast<Declaration*>(static_cast<IDirect3DVertexDeclaration9*>(p));if(!d)return NULL;count=static_cast<uint32_t>(d->elements.size());return d->elements.data();}
const char* KichR4_CPUTextureName(void* p){const Texture* t=p?dynamic_cast<Texture*>(static_cast<IDirect3DBaseTexture9*>(p)):NULL;return t?t->name.c_str():NULL;}
const unsigned char* KichR4_CPUTexturePixels(void* p,uint32_t& w,uint32_t& h,uint64_t& r){const Texture* t=p?dynamic_cast<Texture*>(static_cast<IDirect3DBaseTexture9*>(p)):NULL;return t?t->Pixels(w,h,r):NULL;}
void KichR4_CPUTextureNameSet(void* p,const char* n){Texture* t=p?dynamic_cast<Texture*>(static_cast<IDirect3DBaseTexture9*>(p)):NULL;if(t)t->name=n?n:"";}
IDirect3DStateBlock9* KichR4_CPUStates(const KichR4CPUState* p,uint32_t n){try{return new StateBlock(p,n);}catch(...){return NULL;}}
