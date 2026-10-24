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
    start = re.search(r"float[23]? " + name + r"\(", source).start()
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
struct SampleValue{float r,a;};
struct SampleTexture{SampleValue value;SampleValue Sample(int,float2){return value;}}image1,shadow3,model_bump;
int surface1=0,surface5=0,model_bump_surface=0;
'''
for name in ("NormalSign", "EncodeNormal", "DecodeNormal", "PackMaterial", "MaterialGloss",
             "MaterialClass", "MaterialSlice", "MaterialResponse", "SpecularIntensity", "LocalAttenuation", "Gloss"):
    body = function(name)
    # Translate HLSL swizzle syntax only; all numerical expressions are production code.
    body = re.sub(r"n\.xy=([^;]+);", r"SetXY(n,\1);", body)
    body = body.replace("n.xy", "float2(n.x,n.y)").replace("n.yx", "float2(n.y,n.x)")
    body = body.replace("max(dot(abs(n),float3(1,1,1)),1e-8)", "max(dot(abs(n),float3(1,1,1)),1e-8f)")
    body = body.replace("max(range*.95,.0001)", "max(range*.95f,.0001f)")
    body = body.replace("max(0,dot(color", "max(0.f,dot(color")
    body = body.replace("[branch]", "")
    adapter += body + "\n"

adapter += r'''
int main(){
    Varying sample={float2(.2f,.4f)};image1.value={.4f,1};shadow3.value={1,.25f};
    material.x=0;assert(abs(Gloss(sample)-2.f/255)<1e-6);
    material.x=1;assert(abs(Gloss(sample)-.16f)<1e-6);
    material.x=9;assert(abs(Gloss(sample)-.08f)<1e-6); // Bumped sload detail alpha*2.
    material.x=8;assert(abs(Gloss(sample)-2.f/255)<1e-6); // Flat deffer keeps def_gloss.
    material.x=9;image1.value.r=1;shadow3.value.a=1;assert(Gloss(sample)==1);
    model_bump.value={.6f,1};material.x=1025;assert(abs(Gloss(sample)-.36f)<1e-6); // HUD samples t8, not any shadow/legacy slot.
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
    for(int slice=0;slice<4;++slice){
        float2 dark=MaterialResponse(0,1,float(slice));assert(dark.x==0 && dark.y==0);
        for(int ld=0;ld<=100;++ld)for(int lh=0;lh<=100;++lh){
            float2 response=MaterialResponse(ld*.01f,lh*.01f,float(slice));
            assert(std::isfinite(response.x) && std::isfinite(response.y));
            assert(response.x>=0 && response.x<=1 && response.y>=0 && response.y<=1);
        }
    }
    // Independent SoC LUT checkpoints: material response cannot invent gain.
    assert(abs(MaterialResponse(.5f,0,0).x-pow(.5f,.75f))<1e-6);
    assert(abs(MaterialResponse(.5f,0,1).x-pow(.5f,.90f))<1e-6);
    assert(abs(MaterialResponse(.5f,0,2).x-.5f)<1e-6);
    assert(abs(MaterialResponse(1,1,0).y-.5f)<1e-6);
    assert(MaterialResponse(1,1,1).y==1);
    assert(abs(MaterialResponse(.5f,0,.5f).x-
        (MaterialResponse(.5f,0,0).x+MaterialResponse(.5f,0,1).x)*.5f)<1e-6);
    // range*.95 and q-linear falloff, including no light beyond effective range.
    assert(LocalAttenuation(0,10)==1 && LocalAttenuation(100,10)==0);
    assert(abs(LocalAttenuation(9.5f*9.5f*.25f,10)-.75f)<1e-6);
    assert(abs(LocalAttenuation(9.5f*9.5f,10))<1e-6);
    for(float intensity:{0.f,.125f,.5f,1.f,4.f,100.f}){
        float spec=SpecularIntensity(float3(intensity,intensity,intensity));
        assert(std::isfinite(spec) && spec>=0 && spec<=max(1.f,intensity)*(1+1e-6f));
    }
    std::cout<<"PASS: production HLSL material bounds, R2 reference responses/falloff, 2048 exact half payloads; oct half max error="<<worst<<" deg\n";
}
'''
with tempfile.TemporaryDirectory(prefix="kich-material-lighting-") as folder:
    cpp, binary = Path(folder) / "lighting.cpp", Path(folder) / "lighting"
    cpp.write_text(adapter)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-Wall", "-Wextra",
                    "-Werror", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
