#ifndef KICH_R4_DRAW_STATE_DX11_H
#define KICH_R4_DRAW_STATE_DX11_H
#include <d3d11.h>
#include <stdint.h>
#include <string.h>

namespace kich_r4 {
enum {DrawTextureSlots=10,DrawSamplerSlots=11};
// Immediate-context state, shared by ALL pipelines on one device. These are
// borrowed pointers: the owner invalidates before releasing resources, and
// whenever a pass/frame/reset changes context state outside Apply().
struct DrawBindingsDX11 {
    ID3D11InputLayout* layout;
    ID3D11Buffer* vertices;
    ID3D11Buffer* instances;
    ID3D11Buffer* indices;
    ID3D11Buffer* constants;
    ID3D11Buffer* pose;
    UINT vertex_stride,instance_stride,index_stride;
    ID3D11VertexShader* vs;
    ID3D11PixelShader* ps;
    ID3D11ShaderResourceView* textures[DrawTextureSlots];
    ID3D11SamplerState* samplers[DrawSamplerSlots];
    ID3D11RasterizerState* raster;
    ID3D11DepthStencilState* depth;
    ID3D11BlendState* blend;
    UINT stencil_ref;
    D3D11_VIEWPORT viewport;
    D3D11_RECT scissor;
    bool scissor_enabled;
};
class DrawStateDX11 {
    bool valid_;
    DrawBindingsDX11 previous_;
    D3D11_VIEWPORT pass_viewport_;
    template<class T,unsigned N> static bool ChangedRange(T* const (&before)[N],T* const (&after)[N],bool valid,UINT& first,UINT& count){
        first=N;UINT last=0;
        for(UINT i=0;i<N;++i)if(!valid || before[i]!=after[i]){if(first==N)first=i;last=i;}
        count=first==N?0:last-first+1;return count!=0;
    }
public:
    uint64_t draws,state_calls;
    DrawStateDX11():valid_(false),draws(0),state_calls(0){memset(&previous_,0,sizeof(previous_));memset(&pass_viewport_,0,sizeof(pass_viewport_));}
    void Invalidate(){valid_=false;}
    void BeginPass(const D3D11_VIEWPORT& viewport){Invalidate();pass_viewport_=viewport;}
    D3D11_VIEWPORT Viewport(float minimum=0,float maximum=1)const {D3D11_VIEWPORT result=pass_viewport_;result.MinDepth=minimum;result.MaxDepth=maximum;return result;}
    void Apply(ID3D11DeviceContext* context,const DrawBindingsDX11& next){
        ++draws;const UINT offset=0;
        if(!valid_ || previous_.layout!=next.layout){context->IASetInputLayout(next.layout);++state_calls;}
        if(!valid_){context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);++state_calls;}
        if(!valid_ || previous_.vertices!=next.vertices || previous_.vertex_stride!=next.vertex_stride){
            context->IASetVertexBuffers(0,1,&next.vertices,&next.vertex_stride,&offset);++state_calls;}
        if(!valid_ || previous_.instances!=next.instances || previous_.instance_stride!=next.instance_stride){
            context->IASetVertexBuffers(1,1,&next.instances,&next.instance_stride,&offset);++state_calls;}
        if(!valid_ || previous_.indices!=next.indices || previous_.index_stride!=next.index_stride){
            context->IASetIndexBuffer(next.indices,next.index_stride==2?DXGI_FORMAT_R16_UINT:DXGI_FORMAT_R32_UINT,0);++state_calls;}
        if(!valid_ || previous_.vs!=next.vs){context->VSSetShader(next.vs,NULL,0);++state_calls;}
        if(!valid_ || previous_.ps!=next.ps){context->PSSetShader(next.ps,NULL,0);++state_calls;}
        if(!valid_){context->GSSetShader(NULL,NULL,0);context->HSSetShader(NULL,NULL,0);context->DSSetShader(NULL,NULL,0);state_calls+=3;}
        if(!valid_ || previous_.constants!=next.constants){context->VSSetConstantBuffers(0,1,&next.constants);context->PSSetConstantBuffers(0,1,&next.constants);state_calls+=2;}
        if(!valid_ || previous_.pose!=next.pose){context->VSSetConstantBuffers(1,1,&next.pose);++state_calls;}
        UINT first=0,count=0;
        if(ChangedRange(previous_.textures,next.textures,valid_,first,count)){context->PSSetShaderResources(first,count,next.textures+first);++state_calls;}
        if(ChangedRange(previous_.samplers,next.samplers,valid_,first,count)){context->PSSetSamplers(first,count,next.samplers+first);++state_calls;}
        if(!valid_ || previous_.raster!=next.raster){context->RSSetState(next.raster);++state_calls;}
        if(!valid_ || previous_.depth!=next.depth || previous_.stencil_ref!=next.stencil_ref){context->OMSetDepthStencilState(next.depth,next.stencil_ref);++state_calls;}
        if(!valid_ || memcmp(&previous_.viewport,&next.viewport,sizeof(next.viewport))){context->RSSetViewports(1,&next.viewport);++state_calls;}
        if(next.scissor_enabled && (!valid_ || !previous_.scissor_enabled || memcmp(&previous_.scissor,&next.scissor,sizeof(next.scissor)))){
            context->RSSetScissorRects(1,&next.scissor);++state_calls;}
        if(!valid_ || previous_.blend!=next.blend){context->OMSetBlendState(next.blend,NULL,0xffffffffu);++state_calls;}
        previous_=next;valid_=true;
    }
};
}
#endif
