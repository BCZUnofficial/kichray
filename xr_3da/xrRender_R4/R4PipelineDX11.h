#ifndef KICH_R4_PIPELINE_DX11_H
#define KICH_R4_PIPELINE_DX11_H
#include <d3d11.h>
#include "R4GeometryRules.h"
namespace kich_r4 {
class PipelineDX11 {
    ID3D11InputLayout* layout_;
    ID3D11VertexShader* vs_;
    ID3D11PixelShader* ps_;
    ID3D11RasterizerState* raster_[3];
    ID3D11DepthStencilState* depth_[2];
    ID3D11SamplerState* sampler_;
    PipelineDX11(const PipelineDX11&);
    PipelineDX11& operator=(const PipelineDX11&);
public:
    uint32_t stride,constant_bytes;
    bool textured;
    PipelineDX11();
    ~PipelineDX11();
    HRESULT Create(ID3D11Device*,const KichR4PipelineDesc&,ID3D11VertexShader*,ID3D11PixelShader*,ID3DBlob*,bool=false);
    void Draw(ID3D11DeviceContext*,const KichR4DrawIndexedDesc&,ID3D11Buffer*,ID3D11Buffer*,ID3D11Buffer*,uint32_t,ID3D11ShaderResourceView* = NULL);
};
}
#endif
