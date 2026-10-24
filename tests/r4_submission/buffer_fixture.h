#include <string.h>
#include <stdio.h>
#include <vector>
#include <memory>
#include <stdexcept>
#include "xr_3da/xrRender_R4/R4ResourceRules.h"
#include "xr_3da/KichR4Geometry.h"
#include "xr_3da/xrRender_R4/R4DeferredRules.h"
using namespace kich_r4;
enum {D3D11_USAGE_DEFAULT=0,D3D11_USAGE_DYNAMIC=2,D3D11_BIND_VERTEX_BUFFER=1,D3D11_BIND_INDEX_BUFFER=2,D3D11_BIND_CONSTANT_BUFFER=4,D3D11_CPU_ACCESS_WRITE=65536,D3D11_MAP_WRITE_DISCARD=4};
#define FAILED(value) ((value)<0)
struct D3D11_BUFFER_DESC {uint32_t ByteWidth,Usage,BindFlags,CPUAccessFlags;};
struct D3D11_SUBRESOURCE_DATA {const void* pSysMem;};
struct D3D11_MAPPED_SUBRESOURCE {void* pData;};
struct Buffer {D3D11_BUFFER_DESC desc;std::vector<unsigned char> bytes;};
struct Resource {Buffer* buffer=NULL;bool dynamic_instances=false,model_pose=false,initialized=false;std::vector<unsigned char> constant_contents;
    KichR4ResourceInfo info={};uint32_t buffer_stride=0;float alpha[4]={};uint32_t generation=0;~Resource(){delete buffer;}};
struct Hardware {int failure=0;int CreateBuffer(const D3D11_BUFFER_DESC* d,const D3D11_SUBRESOURCE_DATA* data,Buffer** out){if(failure)return failure;
    *out=new Buffer;(*out)->desc=*d;(*out)->bytes.resize(d->ByteWidth);if(data)memcpy((*out)->bytes.data(),data->pSysMem,d->ByteWidth);return 0;}};
struct Context {int failure=0;uint32_t maps=0,unmaps=0,updates=0;
    int Map(Buffer* buffer,unsigned,unsigned mode,unsigned,D3D11_MAPPED_SUBRESOURCE* mapped){if(failure)return failure;
        if(mode!=D3D11_MAP_WRITE_DISCARD || buffer->desc.Usage!=D3D11_USAGE_DYNAMIC || buffer->desc.CPUAccessFlags!=D3D11_CPU_ACCESS_WRITE)throw std::runtime_error("invalid dynamic buffer contract");
        ++maps;mapped->pData=buffer->bytes.data();return 0;}
    void Unmap(Buffer*,unsigned){++unmaps;}
    void UpdateSubresource(Buffer* buffer,unsigned,void*,const void* data,unsigned,unsigned){if(buffer->desc.Usage!=D3D11_USAGE_DEFAULT)throw std::runtime_error("UpdateSubresource on dynamic buffer");++updates;memcpy(buffer->bytes.data(),data,buffer->bytes.size());}};
struct Device {IdSource ids;ObjectStore<Resource> resources;Hardware hardware;Context ctx;Hardware* hw=&hardware;Context* context=&ctx;
    uint64_t unchanged_constant_updates=0,uploads=0,uploaded_bytes=0;KichR4GeometryStats geometry_stats={};Device():resources(ids){}
    uint64_t Add(std::unique_ptr<Resource> r){return resources.Insert(std::move(r));}
    void Upload(Resource* r,uint32_t bytes){++uploads;uploaded_bytes+=bytes;++r->generation;}} device;
Device* FindDevice(uint64_t handle,int32_t& hr){hr=handle==1?0:KICH_R4_E_HANDLE;return handle==1?&device:NULL;}
template<class F> int32_t Protect(F f){try{return f();}catch(const std::bad_alloc&){return KICH_R4_E_MEMORY;}}
void Require(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
