#define OPAQUE 2 // Windows GDI macro must not collide with semantic enumerators.
#include "../../xr_3da/KichR4Material.h"
#include "../../xr_3da/KichR4MaterialRules.h"
#include "../../xr_3da/KichR4CaptureInput.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace kich_r4_material;
int main(){
    Source source;PassState pass;
    source.family=FAMILY_TREE;source.blend=true;source.descriptor.shader="not_a_tree_name";
    pass.blend=true;pass.src=5;pass.dst=6;pass.alpha_test=true;pass.alpha_reference=200;
    Descriptor tree=Build(source,pass);assert(tree.semantic==CUTOUT && Geometry(tree) && Shadow(tree) && tree.tree_wave);
    kich_r4_capture::Style geometry,shadow;Apply(tree,geometry);Apply(tree,shadow);
    assert(!geometry.blend && geometry.write && geometry.alpha_ref==shadow.alpha_ref && geometry.alpha_func==shadow.alpha_func);
    source.blend=false;assert(Build(source,pass).semantic==MATERIAL_OPAQUE);
    source.not_tree=true;assert(!Build(source,pass).tree_wave);
    source.family=FAMILY_AREF;source.blend=false;source.alpha_reference=91;
    Descriptor fence=Build(source,pass);assert(fence.semantic==CUTOUT && fence.alpha_ref==200.f/255.f && fence.alpha_func==7 && fence.source_alpha_reference==91);
    source.blend=true;Descriptor glass=Build(source,pass);assert(glass.semantic==BLENDED && !glass.depth_write && !Shadow(glass));
    source.family=FAMILY_MODEL;source.alpha_reference=15;assert(Build(source,pass).semantic==BLENDED);
    source.alpha_reference=16;assert(Build(source,pass).semantic==CUTOUT);
    source.strict_sorting=true;pass.src=2;pass.dst=1;
    const Descriptor sorted=Build(source,pass);assert(sorted.semantic==BLENDED && sorted.src_blend==5 && sorted.dst_blend==6);
    source.strict_sorting=false;pass.src=5;pass.dst=6;
    source.family=FAMILY_DETAIL;assert(Build(source,pass).semantic==BLENDED);
    source.family=FAMILY_UNKNOWN;source.descriptor.shader="tree_aref_opaque";pass.blend=false;
    Descriptor unknown=Build(source,pass);assert(unknown.semantic==UNKNOWN && !unknown.unsupported_reason.empty());
    geometry.blend=1;geometry.write=0;Apply(unknown,geometry);assert(geometry.blend && !geometry.write);
    source.family=FAMILY_SPECIAL;pass.blend=true;pass.dst=2;assert(Build(source,pass).semantic==ADDITIVE);
    pass.distortion=true;assert(Build(source,pass).semantic==DISTORTION);pass.distortion=false;
    source.family=FAMILY_OPAQUE;source.blend=false;pass.blend=false;pass.emissive=true;
    assert(Build(source,pass).semantic==EMISSIVE);
    for(Family family:{FAMILY_OPAQUE,FAMILY_MODEL,FAMILY_TREE,FAMILY_AREF,FAMILY_SPECIAL}){
        source.family=family;pass.blend=true;pass.src=5;pass.dst=2;
        Descriptor glow=Build(source,pass);assert(glow.semantic==EMISSIVE && !Geometry(glow) && !Shadow(glow));
        Apply(glow,geometry);assert(geometry.blend && geometry.src==5 && geometry.dst==2 && !geometry.write);
        pass.blend=false;glow=Build(source,pass);Apply(glow,geometry);
        assert(!geometry.blend && geometry.write && geometry.src==2 && geometry.dst==1 && Geometry(glow) && !Shadow(glow));
        assert(glow.alpha_func==pass.alpha_func && glow.alpha_ref==float(pass.alpha_reference&255)/255.f);
    }
    assert(MaterialClass(-1)==0 && MaterialClass(5)==3 && MaterialClass(1.4f)==1.4f);
    assert(MaterialClass(std::numeric_limits<float>::quiet_NaN())==1);
    assert(EmissiveGain("accum_emissive")==8 && EmissiveGain("accum_emissivel")==.5f);
    assert(EmissiveSpecular("accum_emissive")==8 && EmissiveSpecular("accum_emissivel")==0);
    assert(EmissiveGain(NULL)==0 && EmissiveGain("models_selflight")==0 && EmissiveGain("unknown_accum_emissive")==0);
    source.family=FAMILY_MODEL;pass.emissive=false;pass.unlit=true;pass.alpha_reference=91;pass.alpha_func=7;
    const Descriptor unlit=Build(source,pass);
    assert(unlit.semantic==EMISSIVE && unlit.passes==PASS_FORWARD && unlit.emissive_gain==1 && unlit.emissive_specular==0);
    assert(unlit.depth_test && unlit.depth_write && unlit.alpha_func==7 && unlit.alpha_ref==91.f/255);
    pass.unlit=false;
    source.family=FAMILY_OPAQUE;pass.emissive=false;
    source.descriptor.bump="walls\\stone_bump";source.descriptor.bump_aux=source.descriptor.bump+"#";
    source.descriptor.lightmap="lmap#1";source.descriptor.hemi="lmap#1_1";source.descriptor.detail_scale[0]=source.descriptor.detail_scale[1]=4.f;
    Descriptor bump=Build(source,pass);assert(Bumped(bump));assert((bump.required_attributes&15)==15);
    assert(bump.bump_aux=="walls\\stone_bump#" && bump.detail_scale[0]==4.f);
    const uint64_t key=Key(bump);assert(key==Key(bump));Descriptor changed=bump;changed.alpha_ref=.5f;assert(key!=Key(changed));
    Cache cache;int element=0;
    cache.Register(&element,std::vector<Descriptor>(1,bump));
    for(unsigned i=0;i<10000;++i){const Descriptor* hit=cache.Find(&element,0);assert(hit && Key(*hit)==key);}
    assert(cache.registrations==1 && cache.lookups==10000 && cache.misses==0);
    changed=bump;changed.shader="equivalent_source_alias";changed.class_id=4;
    cache.Register(&element,std::vector<Descriptor>(1,changed));assert(cache.conflicts==0 && Geometry(*cache.Find(&element,0)));
    changed.bump="different_bump";cache.Register(&element,std::vector<Descriptor>(1,changed));
    assert(cache.conflicts==1 && cache.Find(&element,0)->semantic==UNKNOWN && !Geometry(*cache.Find(&element,0)));
    cache.Forget(&element);assert(!cache.Find(&element,0));cache.Register(&element,std::vector<Descriptor>(1,bump));
    assert(Geometry(*cache.Find(&element,0)));assert(!cache.Find(&element,1));
    std::cout<<"PASS: source class+parameters, cutout/forward separation, identical shadow cutoff, cached source maps/UV scale and alias-safe lifecycle\n";
}
