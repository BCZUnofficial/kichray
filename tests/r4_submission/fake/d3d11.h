#ifndef KICH_TEST_D3D11_H
#define KICH_TEST_D3D11_H
#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <string>
typedef unsigned UINT;
struct ID3D11InputLayout {};struct ID3D11Buffer {};struct ID3D11VertexShader {};struct ID3D11PixelShader {};
struct ID3D11ShaderResourceView {};struct ID3D11SamplerState {};struct ID3D11RasterizerState {};
struct ID3D11DepthStencilState {};struct ID3D11BlendState {};
enum {D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST=4,DXGI_FORMAT_R16_UINT=57,DXGI_FORMAT_R32_UINT=42};
struct D3D11_VIEWPORT {float TopLeftX,TopLeftY,Width,Height,MinDepth,MaxDepth;};
struct D3D11_RECT {int32_t left,top,right,bottom;};
struct ID3D11DeviceContext {
    struct Call {std::string name;UINT first,count;};std::vector<Call> calls;
    ID3D11InputLayout* layout=NULL;ID3D11Buffer* vb[2]={};UINT stride[2]={};ID3D11Buffer* ib=NULL;UINT index_format=0;
    ID3D11VertexShader* vs=NULL;ID3D11PixelShader* ps=NULL;
    ID3D11Buffer* vcb[2]={};ID3D11Buffer* pcb=NULL;
    ID3D11ShaderResourceView* textures[128]={};ID3D11SamplerState* samplers[16]={};
    ID3D11RasterizerState* raster=NULL;ID3D11DepthStencilState* depth=NULL;ID3D11BlendState* blend=NULL;
    UINT stencil_ref=0;D3D11_VIEWPORT viewport={};D3D11_RECT scissor={};
    void Log(const char* n,UINT first=0,UINT count=1){calls.push_back({n,first,count});}
    void IASetInputLayout(ID3D11InputLayout* p){layout=p;Log("layout");}
    void IASetPrimitiveTopology(UINT){Log("topology");}
    void IASetVertexBuffers(UINT first,UINT count,ID3D11Buffer* const* p,const UINT* s,const UINT*){for(UINT i=0;i<count;++i){vb[first+i]=p[i];stride[first+i]=s[i];}Log("vb",first,count);}
    void IASetIndexBuffer(ID3D11Buffer* p,UINT format,UINT){ib=p;index_format=format;Log("ib");}
    void VSSetShader(ID3D11VertexShader* p,void*,UINT){vs=p;Log("vs");}
    void PSSetShader(ID3D11PixelShader* p,void*,UINT){ps=p;Log("ps");}
    void GSSetShader(void*,void*,UINT){Log("gs");}void HSSetShader(void*,void*,UINT){Log("hs");}void DSSetShader(void*,void*,UINT){Log("ds");}
    void VSSetConstantBuffers(UINT first,UINT count,ID3D11Buffer* const* p){for(UINT i=0;i<count;++i)vcb[first+i]=p[i];Log("vcb",first,count);}
    void PSSetConstantBuffers(UINT first,UINT count,ID3D11Buffer* const* p){if(count)pcb=*p;Log("pcb",first,count);}
    void PSSetShaderResources(UINT first,UINT count,ID3D11ShaderResourceView* const* p){for(UINT i=0;i<count;++i)textures[first+i]=p[i];Log("textures",first,count);}
    void PSSetSamplers(UINT first,UINT count,ID3D11SamplerState* const* p){for(UINT i=0;i<count;++i)samplers[first+i]=p[i];Log("samplers",first,count);}
    void RSSetState(ID3D11RasterizerState* p){raster=p;Log("raster");}
    void OMSetDepthStencilState(ID3D11DepthStencilState* p,UINT ref){depth=p;stencil_ref=ref;Log("depth");}
    void RSSetViewports(UINT,const D3D11_VIEWPORT* p){viewport=*p;Log("viewport");}
    void RSSetScissorRects(UINT,const D3D11_RECT* p){scissor=*p;Log("scissor");}
    void OMSetBlendState(ID3D11BlendState* p,const float*,UINT){blend=p;Log("blend");}
};
#endif
