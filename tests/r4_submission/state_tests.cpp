#include "../../xr_3da/xrRender_R4/R4DrawStateDX11.h"
#include <stdexcept>
#include <stdio.h>
using namespace kich_r4;
static void Require(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
template<class T> static T* Pointer(uintptr_t value){return reinterpret_cast<T*>(value);}
static DrawBindingsDX11 Bindings(DrawStateDX11& cache){
    DrawBindingsDX11 b={};b.layout=Pointer<ID3D11InputLayout>(1);b.vertices=Pointer<ID3D11Buffer>(2);b.instances=Pointer<ID3D11Buffer>(3);
    b.indices=Pointer<ID3D11Buffer>(4);b.constants=Pointer<ID3D11Buffer>(5);b.pose=Pointer<ID3D11Buffer>(6);
    b.vertex_stride=64;b.instance_stride=64;b.index_stride=2;b.vs=Pointer<ID3D11VertexShader>(7);b.ps=Pointer<ID3D11PixelShader>(8);
    b.raster=Pointer<ID3D11RasterizerState>(9);b.depth=Pointer<ID3D11DepthStencilState>(10);b.blend=Pointer<ID3D11BlendState>(11);b.stencil_ref=3;
    for(unsigned i=0;i<8;++i)b.textures[i]=Pointer<ID3D11ShaderResourceView>(20+i);
    for(unsigned i=0;i<9;++i)b.samplers[i]=Pointer<ID3D11SamplerState>(40+i);
    b.viewport=cache.Viewport();return b;
}
int main(){
    DrawStateDX11 cache;ID3D11DeviceContext context;D3D11_VIEWPORT pass={0,0,1920,1080,0,1};cache.BeginPass(pass);
    DrawBindingsDX11 a=Bindings(cache);cache.Apply(&context,a);const size_t first=context.calls.size();
    Require(first>15 && cache.state_calls==first,"initial binding incomplete");
    for(unsigned i=0;i<100;++i)cache.Apply(&context,a);
    Require(context.calls.size()==first && cache.draws==101,"identical draws rebind state");
    DrawBindingsDX11 b=a;b.depth=Pointer<ID3D11DepthStencilState>(12);b.stencil_ref=4;b.blend=NULL;
    cache.Apply(&context,b);Require(context.calls.size()==first+2 && context.stencil_ref==4 && !context.blend,"depth/blend changes lost");
    cache.Apply(&context,a);Require(context.depth==a.depth && context.blend==a.blend,"pipeline switch did not restore context");
    context.calls.clear();b=a;b.textures[3]=NULL;cache.Apply(&context,b);
    Require(context.calls.size()==1 && context.calls[0].first==3 && context.calls[0].count==1 && !context.textures[3],"single SRV dirty range");
    context.calls.clear();b.textures[1]=NULL;b.textures[7]=NULL;cache.Apply(&context,b);
    Require(context.calls.size()==1 && context.calls[0].first==1 && context.calls[0].count==7 && context.textures[5]==a.textures[5],"spanning SRV range lost unchanged slots");
    b.samplers[8]=NULL;context.calls.clear();cache.Apply(&context,b);Require(context.calls.size()==1 && context.calls[0].first==8,"sampler dirty range");
    // GPU model/detail -> ordinary geometry must clear the secondary VB and palette.
    b.pose=NULL;b.instances=NULL;b.vertex_stride=32;b.index_stride=4;cache.Apply(&context,b);
    Require(!context.vcb[1] && !context.vb[1] && context.stride[0]==32 && context.index_format==DXGI_FORMAT_R32_UINT,"geometry transition stale binding");
    b.viewport=cache.Viewport(0,.02f);cache.Apply(&context,b);Require(context.viewport.Width==1920 && context.viewport.MaxDepth==.02f,"HUD viewport depth");
    b.viewport=cache.Viewport();cache.Apply(&context,b);Require(context.viewport.MaxDepth==1,"world viewport not restored");
    b.scissor_enabled=true;b.scissor={10,20,40,60};cache.Apply(&context,b);Require(context.scissor.bottom==60,"scissor bind");
    b.scissor_enabled=false;cache.Apply(&context,b);b.scissor_enabled=true;context.calls.clear();cache.Apply(&context,b);
    Require(context.calls.size()==1 && context.calls[0].name=="scissor","scissor re-enable reused stale rectangle");
    // A pass target alias can clear an SRV while its requested pointer stays the same.
    cache.Apply(&context,a);context.textures[2]=NULL;pass.Width=256;pass.Height=256;cache.BeginPass(pass);
    a.viewport=cache.Viewport();context.calls.clear();cache.Apply(&context,a);
    Require(context.textures[2]==a.textures[2] && context.viewport.Width==256 && context.calls.size()==first,"pass/alias invalidation incomplete");
    // Simulate external ClearState and resource pointer reuse. Invalidation must
    // force binds even when new resources occupy exactly the cached addresses.
    context=ID3D11DeviceContext();cache.Invalidate();cache.Apply(&context,a);
    Require(context.layout==a.layout && context.vcb[0]==a.constants && context.textures[7]==a.textures[7],"release/reset pointer reuse");
    DrawStateDX11 other;ID3D11DeviceContext second;other.BeginPass(pass);other.Apply(&second,a);Require(second.layout==a.layout,"cache leaked across devices");
    puts("PASS: real context cache, 100 identical draws without binds, pipeline/HUD/detail transitions, dirty ranges, alias/reset/resource reuse");
}
