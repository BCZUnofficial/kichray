#include <stdint.h>
#include <memory>
#include <vector>
#include <map>
#include <stdexcept>
#include <stdio.h>
#include "../../xr_3da/KichR4NativeTools.h"
#include "../../xr_3da/KichR4Model.h"
enum {D3D11_USAGE_DYNAMIC=2,DXGI_FORMAT_R8G8B8A8_UNORM=28,D3D11_BIND_SHADER_RESOURCE=8,D3D11_CPU_ACCESS_WRITE=65536,D3D11_MAP_WRITE_DISCARD=4};
#define FAILED(x) ((x)<0)
struct D3D11_TEXTURE2D_DESC {uint32_t Width,Height,MipLevels,ArraySize,Format;struct {uint32_t Count,Quality;} SampleDesc;uint32_t Usage,BindFlags,CPUAccessFlags;};
struct D3D11_MAPPED_SUBRESOURCE {void* pData;uint32_t RowPitch;};
struct Texture {D3D11_TEXTURE2D_DESC desc;std::vector<unsigned char> data;void GetDesc(D3D11_TEXTURE2D_DESC* d){*d=desc;}};
struct Resource {Texture* texture=NULL;int* srv=NULL;KichR4ResourceInfo info={};std::vector<KichR4ShaderBinding> bindings;bool initialized=false;~Resource(){delete texture;delete srv;}};
struct Registry {std::map<uint64_t,std::unique_ptr<Resource>> data;Resource* Find(uint64_t h){auto i=data.find(h);return i==data.end()?NULL:i->second.get();}};
struct Hardware {unsigned creates=0;int32_t failure=0;
    int32_t CreateTexture2D(const D3D11_TEXTURE2D_DESC* desc,void*,Texture** out){if(failure)return failure;++creates;*out=new Texture;(*out)->desc=*desc;(*out)->data.resize(size_t(desc->Width*4+16)*desc->Height);return 0;}
    int32_t CreateShaderResourceView(Texture*,void*,int** out){if(failure)return failure;*out=new int(1);return 0;}};
struct Context {unsigned maps=0,unmaps=0;int32_t failure=0;int32_t Map(Texture* t,uint32_t,uint32_t mode,uint32_t,D3D11_MAPPED_SUBRESOURCE* out){if(failure)return failure;if(mode!=D3D11_MAP_WRITE_DISCARD)throw std::runtime_error("not discard");++maps;out->RowPitch=t->desc.Width*4+16;out->pData=t->data.data();return 0;}void Unmap(Texture*,uint32_t){++unmaps;}};
struct Device {Registry resources;Hardware hardware;Context ctx;Hardware* hw=&hardware;Context* context=&ctx;uint64_t serial=0;unsigned writes=0;
    uint64_t Add(std::unique_ptr<Resource> r){resources.data[++serial]=std::move(r);return serial;}void Write(Resource*,unsigned){++writes;}} device;
Device* FindDevice(uint64_t h,int32_t& hr){hr=h==1?0:KICH_R4_E_HANDLE;return h==1?&device:NULL;}
template<class F> int32_t Protect(F f){try{return f();}catch(const std::bad_alloc&){return KICH_R4_E_MEMORY;}}
void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
