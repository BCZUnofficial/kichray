#include "RenderSnapshotD3D9.h"
#include "RenderSnapshot.h"
#if defined(_WIN32) && !defined(_EDITOR) && !defined(DEDICATED_SERVER)
#include <d3d9.h>
#include <stdio.h>
#include <string.h>

namespace {
const char* binding_category="R2_BINDING"; // Diagnostic capture executes on the render thread.
void Pointer(char* out,size_t size,const void* pointer){snprintf(out,size,"r2_%llx",static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(pointer)));}
void Text(const char* id,const char* key,const char* value){RenderSnapshot_Record(binding_category,id,key,value);}
void Number(const char* id,const char* key,uint64_t value){RenderSnapshot_RecordU64(binding_category,id,key,value);}
void Surface(IDirect3DSurface9* surface,const char* binding,const char* slot){
    if(!surface){Text(binding,slot,"UNBOUND");return;}
    char id[64];Pointer(id,sizeof(id),surface);Text(binding,slot,id);
    D3DSURFACE_DESC desc={};const HRESULT hr=surface->GetDesc(&desc);
    RenderSnapshot_Record("R2_RESOURCE",id,"identity_scope","FRAME_OBSERVED_PROCESS_POINTER; NOT_CROSS_RUN_ID");
    RenderSnapshot_Record("R2_RESOURCE",id,"parent_texture_and_subresource","NOT_INSTRUMENTED; NO_HANDLE_ONLY_ALIAS_VERDICT");
    if(FAILED(hr)){RenderSnapshot_Record("R2_RESOURCE",id,"descriptor_status","QUERY_FAILED");return;}
    RenderSnapshot_RecordU64("R2_RESOURCE",id,"width",desc.Width);RenderSnapshot_RecordU64("R2_RESOURCE",id,"height",desc.Height);
    RenderSnapshot_RecordU64("R2_RESOURCE",id,"format_D3DFORMAT",desc.Format);RenderSnapshot_RecordU64("R2_RESOURCE",id,"usage",desc.Usage);
    RenderSnapshot_RecordU64("R2_RESOURCE",id,"pool_D3DPOOL",desc.Pool);RenderSnapshot_RecordU64("R2_RESOURCE",id,"multisample_type",desc.MultiSampleType);
    RenderSnapshot_RecordU64("R2_RESOURCE",id,"multisample_quality",desc.MultiSampleQuality);
    RenderSnapshot_Record("R2_RESOURCE",id,"last_clear_and_producer","NOT_INSTRUMENTED");
}
void Texture(IDirect3DDevice9* device,DWORD stage,const char* binding,const char* slot){
    IDirect3DBaseTexture9* texture=NULL;const HRESULT hr=device->GetTexture(stage,&texture);
    if(FAILED(hr)){Text(binding,slot,"QUERY_FAILED_OR_STAGE_UNSUPPORTED");if(texture)texture->Release();return;}
    if(!texture){Text(binding,slot,"UNBOUND");return;}
    char id[64];Pointer(id,sizeof(id),texture);Text(binding,slot,id);
    const D3DRESOURCETYPE type=texture->GetType();
    RenderSnapshot_Record("R2_RESOURCE",id,"identity_scope","FRAME_OBSERVED_PROCESS_POINTER; NOT_CROSS_RUN_ID");
    RenderSnapshot_RecordU64("R2_RESOURCE",id,"type_D3DRESOURCETYPE",type);RenderSnapshot_RecordU64("R2_RESOURCE",id,"mip_levels",texture->GetLevelCount());
    RenderSnapshot_RecordU64("R2_RESOURCE",id,"requested_LOD",texture->GetLOD());
    D3DSURFACE_DESC desc={};D3DVOLUME_DESC volume={};HRESULT result=E_NOTIMPL;
    if(type==D3DRTYPE_TEXTURE)result=static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0,&desc);
    else if(type==D3DRTYPE_CUBETEXTURE)result=static_cast<IDirect3DCubeTexture9*>(texture)->GetLevelDesc(0,&desc);
    else if(type==D3DRTYPE_VOLUMETEXTURE)result=static_cast<IDirect3DVolumeTexture9*>(texture)->GetLevelDesc(0,&volume);
    if(SUCCEEDED(result)){
        const bool is_volume=type==D3DRTYPE_VOLUMETEXTURE;
        RenderSnapshot_RecordU64("R2_RESOURCE",id,"width",is_volume?volume.Width:desc.Width);RenderSnapshot_RecordU64("R2_RESOURCE",id,"height",is_volume?volume.Height:desc.Height);
        RenderSnapshot_RecordU64("R2_RESOURCE",id,"depth",is_volume?volume.Depth:1);RenderSnapshot_RecordU64("R2_RESOURCE",id,"format_D3DFORMAT",is_volume?volume.Format:desc.Format);
        RenderSnapshot_RecordU64("R2_RESOURCE",id,"pool_D3DPOOL",is_volume?volume.Pool:desc.Pool);
    }else RenderSnapshot_Record("R2_RESOURCE",id,"descriptor_status","QUERY_FAILED");
    RenderSnapshot_Record("R2_RESOURCE",id,"sampled_pixels","NOT_MEASURED; BINDING_IS_NOT_SHADER_EXECUTION");
    texture->Release();
    const D3DSAMPLERSTATETYPE keys[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE,D3DSAMP_MAXANISOTROPY,D3DSAMP_MAXMIPLEVEL};
    char state[320]={};size_t used=0;
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i){DWORD value=0;const HRESULT query=device->GetSamplerState(stage,keys[i],&value);
        if(used<sizeof(state)){const int count=SUCCEEDED(query)?snprintf(state+used,sizeof(state)-used,"%u=%lu;",static_cast<unsigned>(keys[i]),value):snprintf(state+used,sizeof(state)-used,"%u=NOT_AVAILABLE;",static_cast<unsigned>(keys[i]));if(count>0)used+=static_cast<size_t>(count);}}
    state[sizeof(state)-1]=0;char key[64];snprintf(key,sizeof(key),"%s_sampler_D3DSAMP",slot);Text(binding,key,state);
}
}

void RenderSnapshot_D3D9Resources(void* opaque,uint32_t stride){
    if(!RenderSnapshot_Detailed() || !opaque)return;
    static uint64_t capture=0,observed=0,kept[4]={};
    if(capture!=RenderSnapshot_CaptureID()){capture=RenderSnapshot_CaptureID();observed=0;memset(kept,0,sizeof(kept));}
    ++observed;RenderSnapshot_U64("R2_BACKEND","binding_observations",observed);
    RenderSnapshot_Text("R2_BACKEND","binding_sample_policy","FIRST_128_DRAWS_PER_UI_DECAL_MODEL_OTHER_CLASS; NOT_COMPLETE_TRACE");
    RenderSnapshotProvenance provenance={};RenderSnapshot_GetProvenance(&provenance);
    const unsigned bucket=provenance.ui_widget[0] || !strcmp(provenance.semantic_type,"UI")?0:
        strstr(provenance.semantic_type,"WALLMARK") || strstr(provenance.semantic_type,"DECAL")?1:
        strstr(provenance.semantic_type,"MODEL")?2:3;
    if(kept[bucket]>=128){RenderSnapshot_U64("R2_BACKEND","binding_observations_unsampled",observed-kept[0]-kept[1]-kept[2]-kept[3]);return;}
    const char* const categories[]={"R2_UI_BINDING","R2_DECAL_BINDING","R2_MODEL_BINDING","R2_BINDING"};
    binding_category=categories[bucket];
    ++kept[bucket];RenderSnapshot_U64("R2_BACKEND","binding_observations_sampled",kept[0]+kept[1]+kept[2]+kept[3]);
    IDirect3DDevice9* device=static_cast<IDirect3DDevice9*>(opaque);char id[64];snprintf(id,sizeof(id),"binding_%llu",static_cast<unsigned long long>(observed));
    Text(id,"material_route_id",provenance.material_route_id);Text(id,"ui_widget",provenance.ui_widget);Number(id,"binding_observation",observed);Text(id,"correlation_scope","PRE_DRAW_OBSERVATION; SAME_CAPTURE_AND_PROVENANCE; NOT_GLOBAL_DRAW_INDEX");Number(id,"vertex_stride",stride);
    D3DVIEWPORT9 viewport={};RECT scissor={};
    if(SUCCEEDED(device->GetViewport(&viewport))){char text[160];snprintf(text,sizeof(text),"%lu,%lu,%lu,%lu;depth=%.9g,%.9g",viewport.X,viewport.Y,viewport.Width,viewport.Height,viewport.MinZ,viewport.MaxZ);Text(id,"viewport",text);}
    else Text(id,"viewport","QUERY_FAILED");
    if(SUCCEEDED(device->GetScissorRect(&scissor))){char text[96];snprintf(text,sizeof(text),"%ld,%ld,%ld,%ld",scissor.left,scissor.top,scissor.right,scissor.bottom);Text(id,"scissor_rect",text);}
    else Text(id,"scissor_rect","QUERY_FAILED");
    DWORD srgb=0;if(SUCCEEDED(device->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb)))Number(id,"srgb_write_enabled",srgb);else Text(id,"srgb_write_enabled","QUERY_FAILED");
    for(unsigned i=0;i<4;++i){IDirect3DSurface9* surface=NULL;char slot[24];snprintf(slot,sizeof(slot),"render_target%u",i);
        if(SUCCEEDED(device->GetRenderTarget(i,&surface)))Surface(surface,id,slot);else Text(id,slot,"UNBOUND_OR_QUERY_UNSUPPORTED");if(surface)surface->Release();}
    IDirect3DSurface9* depth=NULL;if(SUCCEEDED(device->GetDepthStencilSurface(&depth)))Surface(depth,id,"depth_stencil");else Text(id,"depth_stencil","UNBOUND_OR_QUERY_FAILED");if(depth)depth->Release();
    for(unsigned i=0;i<16;++i){char slot[24];snprintf(slot,sizeof(slot),"ps_texture%u",i);Texture(device,i,id,slot);}
    for(unsigned i=0;i<4;++i){char slot[24];snprintf(slot,sizeof(slot),"vs_texture%u",i);Texture(device,D3DVERTEXTEXTURESAMPLER0+i,id,slot);}
    IDirect3DVertexDeclaration9* declaration=NULL;
    if(SUCCEEDED(device->GetVertexDeclaration(&declaration)) && declaration){D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1];UINT count=MAXD3DDECLLENGTH+1;
        if(SUCCEEDED(declaration->GetDeclaration(elements,&count)) && count<=MAXD3DDECLLENGTH+1){bool normal=false,tangent=false,binormal=false;
            for(UINT i=0;i<count && elements[i].Stream!=0xff;++i){const D3DVERTEXELEMENT9& e=elements[i];char key[32],value[96];snprintf(key,sizeof(key),"layout_%u",i);
                snprintf(value,sizeof(value),"stream=%u;offset=%u;type=%u;usage=%u;index=%u",e.Stream,e.Offset,e.Type,e.Usage,e.UsageIndex);Text(id,key,value);
                normal|=e.Usage==D3DDECLUSAGE_NORMAL;tangent|=e.Usage==D3DDECLUSAGE_TANGENT;binormal|=e.Usage==D3DDECLUSAGE_BINORMAL;}
            Number(id,"layout_normal",normal);Number(id,"layout_tangent",tangent);Number(id,"layout_binormal",binormal);
            Text(id,"basis_values_validation","NOT_MEASURED; DECLARATION_PRESENCE_ONLY");
        }else Text(id,"vertex_layout","QUERY_FAILED");
    }else{DWORD fvf=0;if(SUCCEEDED(device->GetFVF(&fvf)))Number(id,"vertex_FVF",fvf);else Text(id,"vertex_layout","QUERY_FAILED");}
    if(declaration)declaration->Release();
}
#else
void RenderSnapshot_D3D9Resources(void*,uint32_t){}
#endif
