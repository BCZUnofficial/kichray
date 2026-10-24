#include <map>
#include <cstring>
#include <stdexcept>
#include <cstdio>
#include "xr_3da/xrRender_R4/R4DeferredRules.h"
#include "xr_3da/xrRender_R4/R4GeometryRules.h"
#include "xr_3da/xrRender_R4/R4DetailRules.h"
#include "xr_3da/xrRender_R4/R4ModelRules.h"
#include "xr_3da/xrRender_R4/R4DrawStateDX11.h"
using namespace kich_r4;
struct Pipeline {
    bool advanced=true,instanced=false,model=true;uint32_t stride=64,constant_bytes=944,model_capacity=16;
    unsigned calls=0;ID3D11ShaderResourceView* observed[DrawTextureSlots]={};KichR4SamplerState samplers[2]={};
    int32_t Draw(void*,int&,const KichR4DrawIndexedDesc&,void*,void*,void*,uint32_t,void*,const KichR4StreamDrawDesc*,ID3D11ShaderResourceView* const* inputs,void*,uint32_t,uint32_t,void*,const KichR4SamplerState* material){
        ++calls;memcpy(observed,inputs,sizeof(observed));if(material)memcpy(samplers,material,sizeof(samplers));return 0;}
};
struct Resource {KichR4ResourceInfo info={};Pipeline* pipeline=NULL;uint32_t buffer_stride=0,model_capacity=0,instance_count=0;
    bool initialized=true,model_pose=false,dynamic_instances=false;void* buffer=NULL;ID3D11ShaderResourceView* srv=NULL;};
struct Store {std::map<uint64_t,Resource> resources;Resource* Find(uint64_t id){auto found=resources.find(id);return found==resources.end()?NULL:&found->second;}};
struct Device {Store resources;struct Frame{bool CanDraw(){return true;}}frame;bool pass_active=true;KichR4PassDesc pass={};void* context=NULL;int draw_state=0;
    uint64_t bound_textures[DrawTextureSlots]={};KichR4GeometryStats geometry_stats={};}device;
Device* FindDevice(uint64_t handle,int32_t& hr){hr=handle==1?0:KICH_R4_E_HANDLE;return handle==1?&device:NULL;}
template<class F>int32_t Protect(F body){return body();}
void SnapshotDraw(Device*,const KichR4DrawIndexedDesc&,Resource*,Resource*,Resource*,const KichR4StreamDrawDesc*,const KichR4Handle*,const KichR4DetailDrawDesc*,KichR4Handle){}
void Require(bool good,const char* why){if(!good)throw std::runtime_error(why);}
