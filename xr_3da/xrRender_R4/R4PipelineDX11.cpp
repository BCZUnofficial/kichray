#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "R4PipelineDX11.h"
#include "../KichR4Detail.h"
namespace kich_r4 {
namespace {
template<class T>void ReleasePipeline(T*& p){if(p){p->Release();p=NULL;}}
const char* Semantic(uint32_t s){switch(s){case KICH_R4_POSITION:return "POSITION";
case KICH_R4_NORMAL:return "NORMAL";case KICH_R4_TEXCOORD:return "TEXCOORD";case KICH_R4_COLOR:return "COLOR";default:return "";}}
DXGI_FORMAT Format(uint32_t f){switch(f){case KICH_R4_FLOAT2:return DXGI_FORMAT_R32G32_FLOAT;
case KICH_R4_FLOAT3:return DXGI_FORMAT_R32G32B32_FLOAT;case KICH_R4_FLOAT4:return DXGI_FORMAT_R32G32B32A32_FLOAT;
case KICH_R4_UNORM8X4:return DXGI_FORMAT_R8G8B8A8_UNORM;default:return DXGI_FORMAT_UNKNOWN;}}
}
PipelineDX11::PipelineDX11():layout_(NULL),vs_(NULL),ps_(NULL),shadow_sampler_(NULL),last_blend_(NULL),last_depth_(NULL),last_sampler_(NULL),stride(0),constant_bytes(0),model_capacity(0),textured(false),stream(false),advanced(false),instanced(false),model(false)
{for(unsigned i=0;i<6;++i)raster_[i]=NULL;for(unsigned i=0;i<3;++i)depth_[i]=NULL;
 for(unsigned i=0;i<2;++i){sampler_[i]=NULL;}for(unsigned i=0;i<100;++i){blends_[i]=NULL;}}
PipelineDX11::~PipelineDX11(){
    for(auto& i:surface_blends_)ReleasePipeline(i.second);
    for(auto& i:surface_depths_)ReleasePipeline(i.second);
    for(auto& i:surface_samplers_)ReleasePipeline(i.second);
    for(unsigned i=0;i<3;++i){ReleasePipeline(depth_[i]);}
    for(unsigned i=0;i<6;++i){ReleasePipeline(raster_[i]);}
    ReleasePipeline(layout_);ReleasePipeline(vs_);ReleasePipeline(ps_);
    for(unsigned i=0;i<2;++i)ReleasePipeline(sampler_[i]);
    ReleasePipeline(shadow_sampler_);
    for(unsigned i=0;i<100;++i)ReleasePipeline(blends_[i]);
}
HRESULT PipelineDX11::Create(ID3D11Device* hw,const KichR4PipelineDesc& d,
    ID3D11VertexShader* vs,ID3D11PixelShader* ps,ID3DBlob* code,bool with_texture,bool with_stream,bool with_advanced,bool with_instances,bool with_model)
{
    D3D11_INPUT_ELEMENT_DESC elements[16]={};
    for(uint32_t i=0;i<d.element_count;++i){const KichR4VertexElement& e=d.elements[i];
        elements[i].SemanticName=Semantic(e.semantic);elements[i].SemanticIndex=e.semantic_index;
        elements[i].Format=Format(e.format);elements[i].AlignedByteOffset=e.byte_offset;
        elements[i].InputSlotClass=D3D11_INPUT_PER_VERTEX_DATA;}
    uint32_t element_count=d.element_count;
    if(with_instances){
        element_count=6;
        elements[0]={"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        elements[1]={"TEXCOORD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0};
        for(unsigned i=0;i<4;++i)elements[i+2]={"TEXCOORD",i+1,DXGI_FORMAT_R32G32B32A32_FLOAT,1,i*16,D3D11_INPUT_PER_INSTANCE_DATA,1};
    }
    if(with_model){
        element_count=10;
        elements[0]={"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        elements[1]={"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0};
        elements[2]={"BLENDWEIGHT",0,DXGI_FORMAT_R32_FLOAT,0,20,D3D11_INPUT_PER_VERTEX_DATA,0};
        elements[3]={"BLENDINDICES",0,DXGI_FORMAT_R16G16_UINT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0};
        elements[4]={"NORMAL",0,DXGI_FORMAT_R32_UINT,0,28,D3D11_INPUT_PER_VERTEX_DATA,0};
        elements[5]={"TANGENT",0,DXGI_FORMAT_R32_UINT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0};
        elements[6]={"BINORMAL",0,DXGI_FORMAT_R32_UINT,0,36,D3D11_INPUT_PER_VERTEX_DATA,0};
        elements[7]={"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0};
        elements[8]={"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,56,D3D11_INPUT_PER_VERTEX_DATA,0};
        elements[9]={"TEXCOORD",2,DXGI_FORMAT_R32_UINT,0,60,D3D11_INPUT_PER_VERTEX_DATA,0};
    }
    HRESULT hr=hw->CreateInputLayout(elements,element_count,code->GetBufferPointer(),code->GetBufferSize(),&layout_);
    if(FAILED(hr))return hr;
    const D3D11_CULL_MODE modes[]={D3D11_CULL_BACK,D3D11_CULL_FRONT,D3D11_CULL_NONE};
    for(unsigned i=0;i<(with_stream?6u:3u);++i){D3D11_RASTERIZER_DESC r={};r.FillMode=D3D11_FILL_SOLID;
        r.CullMode=modes[i%3];r.FrontCounterClockwise=FALSE;r.DepthClipEnable=TRUE;r.ScissorEnable=i>=3;
        hr=hw->CreateRasterizerState(&r,&raster_[i]);if(FAILED(hr))return hr;}
    for(unsigned i=0;i<(with_stream?3u:2u);++i){D3D11_DEPTH_STENCIL_DESC z={};z.DepthEnable=i?TRUE:FALSE;
        z.DepthWriteMask=i==1?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;z.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        z.StencilReadMask=D3D11_DEFAULT_STENCIL_READ_MASK;z.StencilWriteMask=D3D11_DEFAULT_STENCIL_WRITE_MASK;
        z.FrontFace.StencilFailOp=z.FrontFace.StencilDepthFailOp=z.FrontFace.StencilPassOp=D3D11_STENCIL_OP_KEEP;
        z.FrontFace.StencilFunc=D3D11_COMPARISON_ALWAYS;z.BackFace=z.FrontFace;
        hr=hw->CreateDepthStencilState(&z,&depth_[i]);if(FAILED(hr))return hr;}
    if(with_texture)for(unsigned i=0;i<(with_stream?2u:1u);++i){D3D11_SAMPLER_DESC sampler={};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=i?D3D11_TEXTURE_ADDRESS_CLAMP:D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=D3D11_FLOAT32_MAX;
        hr=hw->CreateSamplerState(&sampler,&sampler_[i]);if(FAILED(hr))return hr;}
    // HLSL applies a continuous tent to exact texel-centre comparisons and
    // reconstructs the receiver depth per tap. Hidden bilinear comparisons
    // would use that depth at four different locations and reintroduce acne.
    if(with_advanced){D3D11_SAMPLER_DESC s={};s.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_BORDER;
        s.BorderColor[0]=s.BorderColor[1]=s.BorderColor[2]=s.BorderColor[3]=1;
        s.MaxAnisotropy=1;s.ComparisonFunc=D3D11_COMPARISON_LESS_EQUAL;s.MaxLOD=D3D11_FLOAT32_MAX;
        hr=hw->CreateSamplerState(&s,&shadow_sampler_);if(FAILED(hr))return hr;}
    vs_=vs;ps_=ps;vs_->AddRef();ps_->AddRef();stride=d.vertex_stride;textured=with_texture;stream=with_stream;advanced=with_advanced;instanced=with_instances;model=with_model;return S_OK;
}
HRESULT PipelineDX11::Blend(ID3D11DeviceContext* ctx,const KichR4StreamDrawDesc& d,ID3D11BlendState** out)
{
    *out=NULL;if(!d.blend_enable)return S_OK;
    const unsigned at=(d.source_blend-1)*10+d.dest_blend-1;
    if(!blends_[at]) {
        const D3D11_BLEND factors[]={D3D11_BLEND_ZERO,D3D11_BLEND_ONE,D3D11_BLEND_SRC_COLOR,D3D11_BLEND_INV_SRC_COLOR,
            D3D11_BLEND_SRC_ALPHA,D3D11_BLEND_INV_SRC_ALPHA,D3D11_BLEND_DEST_ALPHA,D3D11_BLEND_INV_DEST_ALPHA,
            D3D11_BLEND_DEST_COLOR,D3D11_BLEND_INV_DEST_COLOR};
        D3D11_BLEND_DESC b={};D3D11_RENDER_TARGET_BLEND_DESC& r=b.RenderTarget[0];r.BlendEnable=TRUE;
        r.SrcBlend=factors[d.source_blend-1];r.DestBlend=factors[d.dest_blend-1];r.BlendOp=D3D11_BLEND_OP_ADD;
        const unsigned sa=d.source_blend==3?5:d.source_blend==4?6:d.source_blend==9?7:d.source_blend==10?8:d.source_blend;
        const unsigned da=d.dest_blend==3?5:d.dest_blend==4?6:d.dest_blend==9?7:d.dest_blend==10?8:d.dest_blend;
        r.SrcBlendAlpha=factors[sa-1];r.DestBlendAlpha=factors[da-1];r.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        r.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        ID3D11Device* hw=NULL;ctx->GetDevice(&hw);if(!hw)return E_FAIL;
        const HRESULT hr=hw->CreateBlendState(&b,&blends_[at]);hw->Release();if(FAILED(hr))return hr;
    }*out=blends_[at];return S_OK;
}
HRESULT PipelineDX11::Sampler(ID3D11DeviceContext* ctx,KichR4SamplerState s,ID3D11SamplerState** out){
    const std::array<uint32_t,10> key={{s.u,s.v,s.w,s.min_filter,s.mag_filter,s.mip_filter,s.anisotropy,s.border,s.lod_bias_bits,s.max_mip}};
    if(last_sampler_ && key==last_sampler_key_){*out=last_sampler_;return S_OK;}
    ID3D11SamplerState*& sampler=surface_samplers_[key];
    if(!sampler){D3D11_SAMPLER_DESC d={};d.AddressU=static_cast<D3D11_TEXTURE_ADDRESS_MODE>(s.u);d.AddressV=static_cast<D3D11_TEXTURE_ADDRESS_MODE>(s.v);d.AddressW=static_cast<D3D11_TEXTURE_ADDRESS_MODE>(s.w);
        d.Filter=s.min_filter==3 || s.mag_filter==3?D3D11_FILTER_ANISOTROPIC:static_cast<D3D11_FILTER>((s.min_filter==2?16:0)|(s.mag_filter==2?4:0)|(s.mip_filter==2?1:0));
        d.MaxAnisotropy=s.anisotropy;d.ComparisonFunc=D3D11_COMPARISON_ALWAYS;memcpy(&d.MipLODBias,&s.lod_bias_bits,4);d.MinLOD=static_cast<float>(s.max_mip);d.MaxLOD=s.mip_filter?D3D11_FLOAT32_MAX:d.MinLOD;
        d.BorderColor[0]=float((s.border>>16)&255)/255;d.BorderColor[1]=float((s.border>>8)&255)/255;d.BorderColor[2]=float(s.border&255)/255;d.BorderColor[3]=float(s.border>>24)/255;
        ID3D11Device* hw=NULL;ctx->GetDevice(&hw);if(!hw)return E_FAIL;const HRESULT hr=hw->CreateSamplerState(&d,&sampler);hw->Release();if(FAILED(hr))return hr;
    }last_sampler_key_=key;last_sampler_=sampler;*out=sampler;return S_OK;
}
HRESULT PipelineDX11::Surface(ID3D11DeviceContext* ctx,const KichR4StreamDrawDesc& d,ID3D11BlendState** blend,ID3D11DepthStencilState** depth){
    const KichR4SurfaceState& s=d.surface;
    const std::array<uint32_t,9> bk={{d.blend_enable,d.source_blend,d.dest_blend,s.blend_op,s.color_mask,s.separate_alpha,s.source_alpha,s.dest_alpha,s.alpha_op}};
    const std::array<uint32_t,10> zk={{d.draw.depth_mode,d.depth_write,s.depth_func,s.stencil,s.stencil_func,s.stencil_read,s.stencil_write,s.stencil_fail,s.stencil_zfail,s.stencil_pass}};
    if(last_blend_ && last_depth_ && bk==last_blend_key_ && zk==last_depth_key_){*blend=last_blend_;*depth=last_depth_;return S_OK;}
    ID3D11BlendState*& b=surface_blends_[bk];ID3D11DepthStencilState*& z=surface_depths_[zk];
    if(b && z){last_blend_key_=bk;last_depth_key_=zk;last_blend_=b;last_depth_=z;*blend=b;*depth=z;return S_OK;}
    ID3D11Device* hw=NULL;ctx->GetDevice(&hw);if(!hw)return E_FAIL;
    struct DeviceGuard{ID3D11Device* value;~DeviceGuard(){value->Release();}} guard={hw};
    const D3D11_BLEND factors[]={D3D11_BLEND_ZERO,D3D11_BLEND_ONE,D3D11_BLEND_SRC_COLOR,D3D11_BLEND_INV_SRC_COLOR,D3D11_BLEND_SRC_ALPHA,D3D11_BLEND_INV_SRC_ALPHA,D3D11_BLEND_DEST_ALPHA,D3D11_BLEND_INV_DEST_ALPHA,D3D11_BLEND_DEST_COLOR,D3D11_BLEND_INV_DEST_COLOR};
    const auto alpha_factor=[&](unsigned v){v=v==3?5:v==4?6:v==9?7:v==10?8:v;return factors[v-1];};
    if(!b){D3D11_BLEND_DESC desc={};auto& r=desc.RenderTarget[0];r.BlendEnable=d.blend_enable!=0;
        r.SrcBlend=factors[d.source_blend-1];r.DestBlend=factors[d.dest_blend-1];r.BlendOp=static_cast<D3D11_BLEND_OP>(s.blend_op);
        r.SrcBlendAlpha=alpha_factor(s.separate_alpha?s.source_alpha:d.source_blend);r.DestBlendAlpha=alpha_factor(s.separate_alpha?s.dest_alpha:d.dest_blend);
        r.BlendOpAlpha=static_cast<D3D11_BLEND_OP>(s.separate_alpha?s.alpha_op:s.blend_op);r.RenderTargetWriteMask=static_cast<UINT8>(s.color_mask);
        const HRESULT hr=hw->CreateBlendState(&desc,&b);if(FAILED(hr))return hr;}
    if(!z){D3D11_DEPTH_STENCIL_DESC desc={};desc.DepthEnable=d.draw.depth_mode!=0;desc.DepthWriteMask=d.depth_write?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;
        desc.DepthFunc=static_cast<D3D11_COMPARISON_FUNC>(s.depth_func);desc.StencilEnable=s.stencil!=0;
        desc.StencilReadMask=static_cast<UINT8>(s.stencil_read);desc.StencilWriteMask=static_cast<UINT8>(s.stencil_write);
        desc.FrontFace.StencilFunc=static_cast<D3D11_COMPARISON_FUNC>(s.stencil_func);
        desc.FrontFace.StencilFailOp=static_cast<D3D11_STENCIL_OP>(s.stencil_fail);desc.FrontFace.StencilDepthFailOp=static_cast<D3D11_STENCIL_OP>(s.stencil_zfail);
        desc.FrontFace.StencilPassOp=static_cast<D3D11_STENCIL_OP>(s.stencil_pass);desc.BackFace=desc.FrontFace;
        const HRESULT hr=hw->CreateDepthStencilState(&desc,&z);if(FAILED(hr))return hr;}
    last_blend_key_=bk;last_depth_key_=zk;last_blend_=b;last_depth_=z;*blend=b;*depth=z;return S_OK;
}
HRESULT PipelineDX11::Draw(ID3D11DeviceContext* ctx,DrawStateDX11& state,const KichR4DrawIndexedDesc& d,
    ID3D11Buffer* vb,ID3D11Buffer* ib,ID3D11Buffer* cb,uint32_t index_stride,ID3D11ShaderResourceView* texture,const KichR4StreamDrawDesc* extra,ID3D11ShaderResourceView* const* inputs,ID3D11Buffer* instances,uint32_t count,uint32_t first,ID3D11Buffer* pose,const KichR4SamplerState* extra_samplers)
{
    ID3D11BlendState* blend=NULL;
    ID3D11DepthStencilState* depth=depth_[extra && d.depth_mode && !extra->depth_write?2:d.depth_mode];
    if(extra){const HRESULT hr=extra->surface.valid?Surface(ctx,*extra,&blend,&depth):Blend(ctx,*extra,&blend);if(FAILED(hr))return hr;}
    DrawBindingsDX11 bindings={};bindings.layout=layout_;bindings.vertices=vb;bindings.instances=instances;
    bindings.indices=ib;bindings.constants=cb;bindings.pose=pose;
    bindings.vertex_stride=stride;bindings.instance_stride=sizeof(KichR4DetailInstance);bindings.index_stride=index_stride;
    bindings.vs=vs_;bindings.ps=ps_;bindings.samplers[1]=advanced?shadow_sampler_:NULL;
    for(unsigned i=0;i<(advanced?8u:1u);++i){const unsigned slot=i?i+1:0;bindings.samplers[slot]=sampler_[extra?extra->clamp_texture:0];
        if(extra && extra->surface.valid && extra->surface.samplers[i].u){KichR4SamplerState s=extra->surface.samplers[i];
            if(extra->clamp_texture)s.u=s.v=s.w=3;
            const HRESULT hr=Sampler(ctx,s,&bindings.samplers[slot]);if(FAILED(hr))return hr;}}
    if(advanced && extra_samplers)for(unsigned i=0;i<KICH_R4_DEFERRED_EXTRA_SAMPLERS;++i){
        if(inputs[8+i] || extra_samplers[i].u)bindings.samplers[9+i]=sampler_[0];
        if(extra_samplers[i].u){const HRESULT hr=Sampler(ctx,extra_samplers[i],&bindings.samplers[9+i]);if(FAILED(hr))return hr;}}
    if(advanced)memcpy(bindings.textures,inputs,sizeof(bindings.textures));else bindings.textures[0]=texture;
    bindings.raster=raster_[d.cull_mode+(extra && extra->scissor_enable?3:0)];bindings.depth=depth;bindings.blend=blend;
    bindings.stencil_ref=extra && extra->surface.valid?extra->surface.stencil_ref:0;
    bindings.viewport=state.Viewport(extra?extra->min_depth:0,extra?extra->max_depth:1);
    bindings.scissor_enabled=extra && extra->scissor_enable;
    if(bindings.scissor_enabled){const D3D11_RECT rect={extra->scissor[0],extra->scissor[1],extra->scissor[2],extra->scissor[3]};bindings.scissor=rect;}
    state.Apply(ctx,bindings);
    if(instanced)ctx->DrawIndexedInstanced(d.index_count,count,d.start_index,0,first);
    else ctx->DrawIndexed(d.index_count,d.start_index,0);
    return S_OK;
}
}
