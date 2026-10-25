#!/usr/bin/env python3
"""Execute the production static material resolver with source shader fixtures."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "xr_3da/KichR4NativeServices.cpp").read_text()
start = source.index("bool KichR4_NativeResolveMaterial(")
brace = source.index("{", start)
depth, end = 1, brace + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
adapter = r'''
#include "KichR4Material.h"
#include "KichR4MaterialRules.h"
#include <cassert>
#include <iostream>
template<class T>struct Ref{T* p;Ref(T* v=NULL):p(v){}operator bool()const{return p!=NULL;}T* operator->()const{return p;}T* _get()const{return p;}};
struct PS{std::string cName;PS(const char* name):cName(name){}};
struct SPass{Ref<PS> ps;SPass(PS* p):ps(p){}};
struct ShaderElement{struct Flags{bool bEmissive=false,bDistort=false;}flags;std::vector<Ref<SPass>> passes;};
struct Shader{Ref<ShaderElement> E[6];};
Shader* source_shader=NULL;
struct ref_shader:Ref<Shader>{void create(const char*,const char*){p=source_shader;}};
kich_r4_material::Cache cache;
const kich_r4_material::Descriptor* KichR4_NativeFindMaterial(const void* e,uint32_t pass){return cache.Find(e,pass);}
const kich_r4_material::Descriptor* KichR4_NativeFindMaterialContract(const void* e,const char* s,const char* t,uint32_t pass){return cache.Find(e,kich_r4_material::Contract(s,t),pass);}
'''
adapter += source[start:end]
adapter += r'''
int main(){
    using namespace kich_r4_material;
    PS base("deffer_base_flat"),strong("accum_emissive"),dim("accum_emissivel"),unrecognized("custom_accumulator");
    SPass base_pass(&base),strong_pass(&strong),dim_pass(&dim),unknown_pass(&unrecognized);
    ShaderElement normal,special;normal.flags.bEmissive=true;normal.passes.push_back(&base_pass);
    // Special flag deliberately absent: source dsgraph consults normal.bEmissive.
    special.passes.push_back(&strong_pass);Shader shader;shader.E[0]=&normal;shader.E[4]=&special;source_shader=&shader;
    Source source;source.family=FAMILY_MODEL;source.descriptor.base="authored_base";source.descriptor.shader="models_selflight";
    PassState state;state.emissive=true;state.alpha_test=true;state.alpha_func=7;state.alpha_reference=91;
    Descriptor original=Build(source,state);cache.Register(&normal,{original});Descriptor out;
    assert(KichR4_NativeResolveMaterial("models_selflight","authored_base",out));
    assert(out.base=="authored_base" && out.emissive_gain==8 && out.emissive_specular==8);
    assert(Geometry(out) && !Shadow(out) && out.alpha_func==7 && out.alpha_ref==91.f/255);
    assert(cache.Find(&normal,0)->emissive_gain==1); // Resolver never mutates a shared element.
    special.passes[0]=&dim_pass;assert(KichR4_NativeResolveMaterial("models_selflightl","authored_base",out));
    assert(out.emissive_gain==.5f && out.emissive_specular==0 && out.base==original.base);
    special.flags.bDistort=true;KichR4_NativeResolveMaterial("models_selflight","authored_base",out);assert(out.emissive_gain==1);
    special.flags.bDistort=false;special.passes[0]=&unknown_pass;
    KichR4_NativeResolveMaterial("models_selflight","authored_base",out);assert(out.emissive_gain==1);
    special.passes[0]=&strong_pass;normal.flags.bEmissive=false;
    KichR4_NativeResolveMaterial("models_selflight","authored_base",out);assert(out.emissive_gain==1);
    normal.flags.bEmissive=true;shader.E[0]=NULL;shader.E[1]=&normal;
    assert(KichR4_NativeResolveMaterial("models_selflight","authored_base",out) && out.emissive_gain==8);
    // Actual native r1/models_selflight normal uses simple.ps, without E[4].
    state.emissive=false;state.unlit=true;normal.flags.bEmissive=false;shader.E[4]=NULL;
    cache.Forget(&normal);cache.Register(&normal,{Build(source,state)});
    assert(KichR4_NativeResolveMaterial("models_selflight","authored_base",out));
    assert(out.semantic==EMISSIVE && out.passes==PASS_FORWARD && out.emissive_gain==1 && out.emissive_specular==0);
    assert(out.depth_test && out.depth_write && out.alpha_ref==91.f/255 && out.base=="authored_base");
    // P0-S11: two source contracts on one shared element resolve to their own descriptors.
    {Descriptor first=Build(source,state),second=first;second.base="second_base";cache.Forget(&normal);
        cache.Register(&normal,Contract("first_shader","shared_textures"),{first});cache.Register(&normal,Contract("second_shader","shared_textures"),{second});
        assert(cache.conflicts>0 && cache.Find(&normal,0)->semantic==UNKNOWN);
        assert(KichR4_NativeResolveMaterial("first_shader","shared_textures",out) && out.base=="authored_base" && out.semantic==EMISSIVE);
        assert(KichR4_NativeResolveMaterial("second_shader","shared_textures",out) && out.base=="second_base" && out.semantic==EMISSIVE);}
    shader.E[1]=NULL;assert(!KichR4_NativeResolveMaterial("models_selflight","authored_base",out));
    assert(out.passes==0 && out.emissive_gain==0);
    std::cout<<"PASS: static E0/E1 base retained; samplerless E4 8/0.5 irradiance/specular folded once; source alpha/depth and absent stock shadow preserved\n";
}
'''
with tempfile.TemporaryDirectory(prefix="kich-material-resolver-") as folder:
    cpp, binary = Path(folder) / "resolver.cpp", Path(folder) / "resolver"
    cpp.write_text(adapter)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(root / "xr_3da"), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
