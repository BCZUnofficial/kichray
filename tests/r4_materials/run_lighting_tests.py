#!/usr/bin/env python3
"""Execute production HLSL material math on CPU; shader compilation is separate."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "resources/shaders/r4/bridge_deferred.hlsl").read_text()


def function(name):
    start = re.search(r"float[234]? " + name + r"\(", source).start()
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


adapter = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
using std::min;using std::max;using std::abs;using std::floor;using std::fmod;
struct float2 {float x,y;float2(float a,float b):x(a),y(b){}};
struct float3 {float x,y,z;float3(float a,float b,float c):x(a),y(b),z(c){}
    float3(float2 xy,float a):x(xy.x),y(xy.y),z(a){}
    void operator/=(float a){x/=a;y/=a;z/=a;}};
struct float4 {float r,g,b,a;float4(float x=0,float y=0,float z=0,float w=0):r(x),g(y),b(z),a(w){}};
float4 operator+(float4 x,float4 y){return float4(x.r+y.r,x.g+y.g,x.b+y.b,x.a+y.a);}
float4 operator*(float4 x,float y){return float4(x.r*y,x.g*y,x.b*y,x.a*y);}
float4 operator/(float4 x,float y){return x*(1/y);}
float3 operator-(float3 x,float y){return float3(x.x-y,x.y-y,x.z-y);}
float dot(float4 x,float y){return (x.r+x.g+x.b+x.a)*y;}
float clamp(float a,float lo,float hi){return max(lo,min(hi,a));}
float saturate(float a){return clamp(a,0,1);}
float frac(float a){return a-std::floor(a);}
float2 operator-(float a,float2 b){return float2(a-b.x,a-b.y);}
float2 operator*(float2 a,float2 b){return float2(a.x*b.x,a.y*b.y);}
float2 abs(float2 a){return float2(abs(a.x),abs(a.y));}
float3 abs(float3 a){return float3(abs(a.x),abs(a.y),abs(a.z));}
float dot(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float3 normalize(float3 a){a/=sqrt(dot(a,a));return a;}
float2 lerp(float2 a,float2 b,float t){return float2(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t);}
void SetXY(float3& n,float2 v){n.x=v.x;n.y=v.y;}
typedef unsigned uint;
struct Varying{float2 uv;};
struct MaterialConstants{float x;float2 yz;MaterialConstants():x(0),yz(1,1){}}material;
float4 env_ambient(0,0,0,1); // .w r2_gloss_factor

struct SampleTexture{float4 value;float2 last=float2(0,0);unsigned reads=0;float4 Sample(int,float2 uv){last=uv;++reads;return value;}}image1,shadow3,shadow4,shadow5,model_bump,model_bump_aux;
int surface1=0,surface5=0,surface6=0,surface7=0,model_bump_surface=0,model_bump_aux_surface=0;
'''
for name in ("NormalSign", "EncodeNormal", "DecodeNormal", "PackMaterial", "MaterialGloss",
             "MaterialClass", "SpecularIntensity", "LocalAttenuation", "TerrainWeights", "TerrainChannels", "TerrainBump", "TerrainNormalSample", "GlossWithTerrain", "Gloss"):
    body = function(name)
    # Translate HLSL swizzle syntax only; all numerical expressions are production code.
    body = re.sub(r"n\.xy=([^;]+);", r"SetXY(n,\1);", body)
    body = body.replace("n.xy", "float2(n.x,n.y)").replace("n.yx", "float2(n.y,n.x)")
    body = body.replace("max(dot(abs(n),float3(1,1,1)),1e-8)", "max(dot(abs(n),float3(1,1,1)),1e-8f)")
    body = body.replace("max(shortened?range*.95:range,.0001)", "max(shortened?range*.95f:range,.0001f)")
    body = body.replace("max(0,dot(color", "max(0.f,dot(color").replace("env_ambient.w", "env_ambient.a")
    body = body.replace("[branch]", "")
    body = body.replace("max(dot(mask,1),.0001)", "max(dot(mask,1),.0001f)")
    body = body.replace("channels.wzy", "float3(channels.a,channels.b,channels.g)")
    adapter += body + "\n"

adapter += r'''
int main(){
    Varying sample={float2(.2f,.4f)};image1.value={.4f,0,0,1};shadow3.value={1,0,0,.25f};
    material.x=0;assert(abs(Gloss(sample)-2.f/255)<1e-6);
    material.x=1;assert(abs(Gloss(sample)-.16f)<1e-6);
    material.x=9;assert(abs(Gloss(sample)-.08f)<1e-6); // Bumped sload detail alpha*2.
    material.x=8;assert(abs(Gloss(sample)-2.f/255)<1e-6); // Flat deffer keeps def_gloss.
    material.x=9;image1.value.r=1;shadow3.value.a=1;assert(Gloss(sample)==1);
    model_bump.value={.6f,0,0,1};material.x=1025;assert(abs(Gloss(sample)-.36f)<1e-6); // HUD samples t8, not any shadow/legacy slot.
    // P0-S11 gloss without normal mapping: same bump R^2 / detail / HUD slot.
    image1.value={.4f,0,0,1};shadow3.value={1,0,0,.25f};
    material.x=65536;assert(abs(Gloss(sample)-.16f)<1e-6);material.x=65536+8;assert(abs(Gloss(sample)-.08f)<1e-6);
    material.x=65536+1024;assert(abs(Gloss(sample)-.36f)<1e-6);
    // Independent SoC BMM oracle: four authored mask weights, WZY normal
    // channels centered at .5, gloss linear (not the ordinary bump R squared).
    image1.value={1,2,3,4};shadow4.value={.1f,.2f,.3f,.4f};shadow5.value={.2f,.4f,.6f,.8f};
    model_bump.value={.3f,.6f,.9f,.2f};model_bump_aux.value={.4f,.8f,.2f,.6f};material.yz=float2(7,11);material.x=8192;
    const auto weights=TerrainWeights(sample);assert(abs(weights.a-.4f)<1e-6 && abs(dot(weights,1)-1)<1e-6);
    const auto mixed=TerrainBump(sample);assert(abs(mixed.r-.3f)<1e-6 && abs(Gloss(sample)-.3f)<1e-6);
    material.x=131072;assert(abs(Gloss(sample)-.3f)<1e-6);material.x=8192; // P0-S11 layer gloss without layer normals
    const auto normal=TerrainNormalSample(mixed,1);assert(abs(normal.x-0.f)<1e-6 && abs(normal.y-0.f)<1e-6 && abs(normal.z-.1f)<1e-6);
    assert(abs(shadow4.last.x-1.4f)<1e-6 && abs(model_bump_aux.last.y-4.4f)<1e-6); // Authored scale, no fixed 50.
    image1.reads=shadow4.reads=shadow5.reads=model_bump.reads=model_bump_aux.reads=0;
    const auto shared_weights=TerrainWeights(sample);const auto shared_channels=TerrainChannels(sample,shared_weights);
    const auto shared_normal=TerrainNormalSample(shared_channels,dot(shared_weights,1));
    assert(abs(shared_normal.z-normal.z)<1e-6 && abs(GlossWithTerrain(sample,shared_channels,dot(shared_weights,1))-.3f)<1e-6);
    assert(image1.reads==1 && shadow4.reads==1 && shadow5.reads==1 && model_bump.reads==1 && model_bump_aux.reads==1);
    image1.value={0,0,0,0};assert(dot(TerrainWeights(sample),1)==0 && abs(Gloss(sample)-2.f/255)<1e-6);
    for(int gloss=0;gloss<128;++gloss)for(int material=0;material<16;++material){
        float packed=PackMaterial(float(gloss)/127,float(material)/5);
        assert(packed==gloss*16+material);
        // RGBA16F storage must preserve every payload without cross-field carry.
        packed=float(_Float16(packed));assert(packed==gloss*16+material);
        assert(abs(MaterialGloss(packed)-float(gloss)/127)<1e-6);
        assert(abs(MaterialClass(packed)-float(material)/5)<1e-6);
    }
    assert(PackMaterial(-1,-1)==0 && PackMaterial(2,4)==2047);
    double worst=0;
    for(int i=0;i<100000;++i){
        float z=1-2*(i+.5f)/100000,r=sqrt(max(0.f,1-z*z)),a=i*2.399963229728653;
        float3 n(r*cos(a),r*sin(a),z);float2 oct=EncodeNormal(n);
        oct.x=float(_Float16(oct.x));oct.y=float(_Float16(oct.y));float3 out=DecodeNormal(oct);
        double error=acos(clamp(dot(n,out),-1,1))*180/3.141592653589793;
        worst=max(worst,error);assert(error<.12);
    }
    for(float3 n:{float3(0,0,1),float3(0,0,-1),float3(0,1,0),float3(1,0,0)})
        assert(dot(n,DecodeNormal(EncodeNormal(n)))>.99999f);
    // MaterialResponse samples the R2 s_material table: run_material_lut_tests.py.
    // q-linear falloff: R2 accum_spot (every spot, every shadowed point's OMNIPART
    // faces) uses range*.95, accum_point (an unshadowed point) the full range.
    assert(LocalAttenuation(0,10,true)==1 && LocalAttenuation(100,10,true)==0);
    assert(abs(LocalAttenuation(9.5f*9.5f*.25f,10,true)-.75f)<1e-6);
    assert(abs(LocalAttenuation(9.5f*9.5f,10,true))<1e-6);
    assert(LocalAttenuation(0,10,false)==1 && LocalAttenuation(100,10,false)==0);
    assert(abs(LocalAttenuation(25,10,false)-.75f)<1e-6);
    assert(abs(LocalAttenuation(9.5f*9.5f,10,false)-(1-.9025f))<1e-6); // lit where SPOT is already dark
    for(float intensity:{0.f,.125f,.5f,1.f,4.f,100.f}){
        float spec=SpecularIntensity(float3(intensity,intensity,intensity));
        assert(std::isfinite(spec) && spec>=0 && spec<=max(1.f,intensity)*(1+1e-6f));
        env_ambient.a=2;assert(abs(SpecularIntensity(float3(intensity,intensity,intensity))-2*spec)<1e-5f*max(1.f,spec));env_ambient.a=1;
    }
    std::cout<<"PASS: production HLSL material packing, R2 falloff/gloss factor, 2048 exact half payloads; oct half max error="<<worst<<" deg\n";
}
'''
with tempfile.TemporaryDirectory(prefix="kich-material-lighting-") as folder:
    cpp, binary = Path(folder) / "lighting.cpp", Path(folder) / "lighting"
    cpp.write_text(adapter)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-Wall", "-Wextra",
                    "-Werror", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
