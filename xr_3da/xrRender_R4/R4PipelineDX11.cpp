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
PipelineDX11::PipelineDX11():layout_(NULL),vs_(NULL),ps_(NULL),shadow_sampler_(NULL),stride(0),constant_bytes(0),model_capacity(0),textured(false),stream(false),advanced(false),instanced(false),model(false)
{for(unsigned i=0;i<6;++i)raster_[i]=NULL;for(unsigned i=0;i<3;++i)depth_[i]=NULL;
 for(unsigned i=0;i<2;++i){sampler_[i]=NULL;}for(unsigned i=0;i<100;++i){blends_[i]=NULL;}}
PipelineDX11::~PipelineDX11(){
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
    if(with_advanced){D3D11_SAMPLER_DESC s={};s.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
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
HRESULT PipelineDX11::Draw(ID3D11DeviceContext* ctx,const KichR4DrawIndexedDesc& d,
    ID3D11Buffer* vb,ID3D11Buffer* ib,ID3D11Buffer* cb,uint32_t index_stride,ID3D11ShaderResourceView* texture,const KichR4StreamDrawDesc* extra,ID3D11ShaderResourceView* const* inputs,ID3D11Buffer* instances,uint32_t count,uint32_t first,ID3D11Buffer* pose)
{
    ID3D11BlendState* blend=NULL;
    if(extra){const HRESULT hr=Blend(ctx,*extra,&blend);if(FAILED(hr))return hr;}
    const UINT offset=0;ctx->IASetInputLayout(layout_);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetVertexBuffers(0,1,&vb,&stride,&offset);
    const UINT instance_stride=sizeof(KichR4DetailInstance);
    ctx->IASetVertexBuffers(1,1,&instances,&instance_stride,&offset);
    ctx->IASetIndexBuffer(ib,index_stride==2?DXGI_FORMAT_R16_UINT:DXGI_FORMAT_R32_UINT,0);
    ctx->VSSetShader(vs_,NULL,0);ctx->PSSetShader(ps_,NULL,0);
    ctx->GSSetShader(NULL,NULL,0);ctx->HSSetShader(NULL,NULL,0);ctx->DSSetShader(NULL,NULL,0);
    ctx->VSSetConstantBuffers(0,1,&cb);ctx->PSSetConstantBuffers(0,1,&cb);
    ctx->VSSetConstantBuffers(1,1,&pose); // NULL explicitly clears the model palette for ordinary pipelines.
    if(advanced){ctx->PSSetShaderResources(0,8,inputs);ID3D11SamplerState* samplers[]={sampler_[extra?extra->clamp_texture:0],shadow_sampler_};ctx->PSSetSamplers(0,2,samplers);}
    else {ctx->PSSetShaderResources(0,1,&texture);ctx->PSSetSamplers(0,1,&sampler_[extra?extra->clamp_texture:0]);}
    ctx->RSSetState(raster_[d.cull_mode+(extra && extra->scissor_enable?3:0)]);
    ctx->OMSetDepthStencilState(depth_[extra && d.depth_mode && !extra->depth_write?2:d.depth_mode],0);
    if(extra){D3D11_VIEWPORT view={};UINT n=1;ctx->RSGetViewports(&n,&view);view.MinDepth=extra->min_depth;view.MaxDepth=extra->max_depth;
        ctx->RSSetViewports(1,&view);if(extra->scissor_enable){const D3D11_RECT rect={extra->scissor[0],extra->scissor[1],extra->scissor[2],extra->scissor[3]};ctx->RSSetScissorRects(1,&rect);}}
    ctx->OMSetBlendState(blend,NULL,0xffffffffu);
    if(instanced)ctx->DrawIndexedInstanced(d.index_count,count,d.start_index,0,first);
    else ctx->DrawIndexed(d.index_count,d.start_index,0);
    return S_OK;
}
}
