#ifndef KICH_R4_PIPELINE_DX11_H
#define KICH_R4_PIPELINE_DX11_H
#include <d3d11.h>
#include <map>
#include <array>
#include "R4GeometryRules.h"
#include "R4StreamRules.h"
#include "R4DrawStateDX11.h"
namespace kich_r4 {
class PipelineDX11 {
    ID3D11InputLayout* layout_;
    ID3D11VertexShader* vs_;
    ID3D11PixelShader* ps_;
    ID3D11RasterizerState* raster_[6];
    ID3D11DepthStencilState* depth_[3];
    ID3D11SamplerState* sampler_[2];
    ID3D11SamplerState* shadow_sampler_;
    ID3D11BlendState* blends_[100];
    std::map<std::array<uint32_t,9>,ID3D11BlendState*> surface_blends_;
    std::map<std::array<uint32_t,10>,ID3D11DepthStencilState*> surface_depths_;
    std::map<std::array<uint32_t,10>,ID3D11SamplerState*> surface_samplers_;
    std::array<uint32_t,9> last_blend_key_;
    std::array<uint32_t,10> last_depth_key_,last_sampler_key_;
    ID3D11BlendState* last_blend_;
    ID3D11DepthStencilState* last_depth_;
    ID3D11SamplerState* last_sampler_;
    HRESULT Sampler(ID3D11DeviceContext*,KichR4SamplerState,ID3D11SamplerState**);
    HRESULT Surface(ID3D11DeviceContext*,const KichR4StreamDrawDesc&,ID3D11BlendState**,ID3D11DepthStencilState**);
    HRESULT Blend(ID3D11DeviceContext*,const KichR4StreamDrawDesc&,ID3D11BlendState**);
    PipelineDX11(const PipelineDX11&);
    PipelineDX11& operator=(const PipelineDX11&);
public:
    uint32_t stride,constant_bytes,model_capacity;
    bool textured,stream,advanced,instanced,model;
    PipelineDX11();
    ~PipelineDX11();
    HRESULT Create(ID3D11Device*,const KichR4PipelineDesc&,ID3D11VertexShader*,ID3D11PixelShader*,ID3DBlob*,bool=false,bool=false,bool=false,bool=false,bool=false);
    HRESULT Draw(ID3D11DeviceContext*,DrawStateDX11&,const KichR4DrawIndexedDesc&,ID3D11Buffer*,ID3D11Buffer*,ID3D11Buffer*,uint32_t,ID3D11ShaderResourceView* = NULL,const KichR4StreamDrawDesc* = NULL,ID3D11ShaderResourceView* const* = NULL,ID3D11Buffer* = NULL,uint32_t=0,uint32_t=0,ID3D11Buffer* = NULL);
};
}
#endif
