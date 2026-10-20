#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "R4PipelineDX11.h"
namespace kich_r4 {
namespace {
template<class T>void ReleasePipeline(T*& p){if(p){p->Release();p=NULL;}}
const char* Semantic(uint32_t s){switch(s){case KICH_R4_POSITION:return "POSITION";
case KICH_R4_NORMAL:return "NORMAL";case KICH_R4_TEXCOORD:return "TEXCOORD";case KICH_R4_COLOR:return "COLOR";default:return "";}}
DXGI_FORMAT Format(uint32_t f){switch(f){case KICH_R4_FLOAT2:return DXGI_FORMAT_R32G32_FLOAT;
case KICH_R4_FLOAT3:return DXGI_FORMAT_R32G32B32_FLOAT;case KICH_R4_FLOAT4:return DXGI_FORMAT_R32G32B32A32_FLOAT;
case KICH_R4_UNORM8X4:return DXGI_FORMAT_R8G8B8A8_UNORM;default:return DXGI_FORMAT_UNKNOWN;}}
}
PipelineDX11::PipelineDX11():layout_(NULL),vs_(NULL),ps_(NULL),sampler_(NULL),stride(0),constant_bytes(0),textured(false)
{for(unsigned i=0;i<3;++i)raster_[i]=NULL;for(unsigned i=0;i<2;++i)depth_[i]=NULL;}
PipelineDX11::~PipelineDX11(){
    for(unsigned i=0;i<2;++i){ReleasePipeline(depth_[i]);}
    for(unsigned i=0;i<3;++i){ReleasePipeline(raster_[i]);}
    ReleasePipeline(layout_);ReleasePipeline(vs_);ReleasePipeline(ps_);
    ReleasePipeline(sampler_);
}
HRESULT PipelineDX11::Create(ID3D11Device* hw,const KichR4PipelineDesc& d,
    ID3D11VertexShader* vs,ID3D11PixelShader* ps,ID3DBlob* code,bool with_texture)
{
    D3D11_INPUT_ELEMENT_DESC elements[8]={};
    for(uint32_t i=0;i<d.element_count;++i){const KichR4VertexElement& e=d.elements[i];
        elements[i].SemanticName=Semantic(e.semantic);elements[i].SemanticIndex=e.semantic_index;
        elements[i].Format=Format(e.format);elements[i].AlignedByteOffset=e.byte_offset;
        elements[i].InputSlotClass=D3D11_INPUT_PER_VERTEX_DATA;}
    HRESULT hr=hw->CreateInputLayout(elements,d.element_count,code->GetBufferPointer(),code->GetBufferSize(),&layout_);
    if(FAILED(hr))return hr;
    const D3D11_CULL_MODE modes[]={D3D11_CULL_BACK,D3D11_CULL_FRONT,D3D11_CULL_NONE};
    for(unsigned i=0;i<3;++i){D3D11_RASTERIZER_DESC r={};r.FillMode=D3D11_FILL_SOLID;
        r.CullMode=modes[i];r.FrontCounterClockwise=FALSE;r.DepthClipEnable=TRUE;
        hr=hw->CreateRasterizerState(&r,&raster_[i]);if(FAILED(hr))return hr;}
    for(unsigned i=0;i<2;++i){D3D11_DEPTH_STENCIL_DESC z={};z.DepthEnable=i?TRUE:FALSE;
        z.DepthWriteMask=i?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;z.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        z.StencilReadMask=D3D11_DEFAULT_STENCIL_READ_MASK;z.StencilWriteMask=D3D11_DEFAULT_STENCIL_WRITE_MASK;
        z.FrontFace.StencilFailOp=z.FrontFace.StencilDepthFailOp=z.FrontFace.StencilPassOp=D3D11_STENCIL_OP_KEEP;
        z.FrontFace.StencilFunc=D3D11_COMPARISON_ALWAYS;z.BackFace=z.FrontFace;
        hr=hw->CreateDepthStencilState(&z,&depth_[i]);if(FAILED(hr))return hr;}
    if(with_texture){D3D11_SAMPLER_DESC sampler={};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=D3D11_FLOAT32_MAX;
        hr=hw->CreateSamplerState(&sampler,&sampler_);if(FAILED(hr))return hr;}
    vs_=vs;ps_=ps;vs_->AddRef();ps_->AddRef();stride=d.vertex_stride;textured=with_texture;return S_OK;
}
void PipelineDX11::Draw(ID3D11DeviceContext* ctx,const KichR4DrawIndexedDesc& d,
    ID3D11Buffer* vb,ID3D11Buffer* ib,ID3D11Buffer* cb,uint32_t index_stride,ID3D11ShaderResourceView* texture)
{
    const UINT offset=0;ctx->IASetInputLayout(layout_);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetVertexBuffers(0,1,&vb,&stride,&offset);
    ctx->IASetIndexBuffer(ib,index_stride==2?DXGI_FORMAT_R16_UINT:DXGI_FORMAT_R32_UINT,0);
    ctx->VSSetShader(vs_,NULL,0);ctx->PSSetShader(ps_,NULL,0);
    ctx->GSSetShader(NULL,NULL,0);ctx->HSSetShader(NULL,NULL,0);ctx->DSSetShader(NULL,NULL,0);
    ctx->VSSetConstantBuffers(0,1,&cb);ctx->PSSetConstantBuffers(0,1,&cb);
    ctx->PSSetShaderResources(0,1,&texture);ctx->PSSetSamplers(0,1,&sampler_);
    ctx->RSSetState(raster_[d.cull_mode]);ctx->OMSetDepthStencilState(depth_[d.depth_mode],0);
    ctx->OMSetBlendState(NULL,NULL,0xffffffffu);
    ctx->DrawIndexed(d.index_count,d.start_index,0);
}
}
