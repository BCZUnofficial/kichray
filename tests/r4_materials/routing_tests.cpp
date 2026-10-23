#include "fake_engine.h"
#include <cassert>
#include <iostream>
std::string active_shader,bound_texture;
uint32_t effect=0,source_element=UINT32_MAX,source_pass=0;
bool distortion_enabled=true,bound_clamp=false;
kich_r4_material::Cache native_materials;
const kich_r4_material::Descriptor* active_material=NULL;
Backend RCache;
std::vector<Draw> draws;
std::vector<SelectionObservation> selections;

int main(){
    CRender renderer;
    PixelShader particle("particle"),distort("particle_distort"),custom("opaque_named_source");
    SPass fire_pass(&particle,"pfx\\pfx_ani-fire01"),distort_pass(&distort,"pfx\\pfx_distortion"),
        second_distort(&custom,"pfx\\distortion_variant",true);
    ShaderElement fire,warp;fire.passes.push_back(&fire_pass);warp.flags.bDistort=true;warp.passes.push_back(&distort_pass);
    Shader fire_shader,xdistort;fire_shader.E[0]=&fire;xdistort.E[4]=&warp;
    IRender_Visual flame,fire_distort;flame.shader=&fire_shader;fire_distort.shader=&xdistort;
    renderer.Submit(&flame,NULL);assert(draws.size()==1 && draws.back().texture=="pfx\\pfx_ani-fire01");
    renderer.Submit(&fire_distort,NULL);
    assert(draws.size()==2);const Draw& distortion=draws.back();
    assert(distortion.texture=="pfx\\pfx_distortion" && distortion.shader=="particle_distort");
    assert(distortion.effect==5 && distortion.element==4 && distortion.pass==0);
    assert(selections.back().element==4 && selections.back().pass==0 && selections.back().available==16 && selections.back().reason=="SPECIAL_E4_FLAGGED_DISTORTION_ENABLED");
    assert(!distortion.clamp); // No material-wide clamp workaround for valid source UVs.
    for(unsigned i=0;i<8;++i)assert(distortion.uv[i]==fire_distort.uv[i]);

    // E[4] supplements a real normal element; it does not replace or duplicate it.
    xdistort.E[0]=&fire;draws.clear();renderer.Submit(&fire_distort,NULL);
    assert(draws.size()==2 && draws[0].effect==1 && draws[0].element==0);
    assert(draws[1].effect==5 && draws[1].element==4);
    distortion_enabled=false;draws.clear();renderer.Submit(&fire_distort,NULL);
    assert(draws.size()==1 && draws[0].element==0);
    xdistort.E[0]=NULL;draws.clear();renderer.Submit(&fire_distort,NULL);assert(draws.empty());
    distortion_enabled=true;

    // Both passes bind their own resources; bDistort wins over shader-name inference.
    warp.passes.push_back(&second_distort);draws.clear();renderer.Submit(&fire_distort,NULL);
    assert(draws.size()==2 && draws[0].pass==0 && draws[1].pass==1);
    assert(draws[1].shader=="opaque_named_source" && draws[1].texture=="pfx\\distortion_variant");
    assert(draws[1].effect==5 && draws[1].clamp);
    assert(kich_r4_material::Effect("particle_distort",false,false)==5);
    assert(kich_r4_material::Effect("water",true,false)==5);
    assert(kich_r4_material::Effect("particle",false,true)==6);

    // A normal LQ-only material remains usable, while empty materials emit nothing.
    Shader low,empty;low.E[1]=&fire;IRender_Visual lod;lod.shader=&low;
    draws.clear();renderer.Submit(&lod,NULL);assert(draws.size()==1 && draws[0].element==1);
    assert(selections.back().reason=="NORMAL_E0_UNAVAILABLE_OR_DISTORT_FALLBACK_E1");
    lod.shader=&empty;draws.clear();renderer.Submit(&lod,NULL);assert(draws.empty());

    // Distortion flags on a normal slot still emit a single special draw.
    Shader unusual;unusual.E[0]=&warp;lod.shader=&unusual;draws.clear();renderer.Submit(&lod,NULL);
    assert(selections.back().reason=="SPECIAL_E4_UNAVAILABLE_FLAGGED_E0_FALLBACK");
    assert(draws.size()==2);for(const Draw& d:draws)assert(d.effect==5 && d.element==0);

    // A generated particle outside the camera never enters the shadow union path;
    // decoded models remain available to the later per-pass shadow culling.
    flame.vis.sphere.P.x=100;draws.clear();renderer.Submit(&flame,NULL);assert(draws.empty());
    flame.native_model=true;renderer.Submit(&flame,NULL);assert(draws.size()==1);
    // The native geometry descriptor replaces R1's multipass modulation with a
    // single GBuffer draw. Forward/distortion above retain their original passes.
    flame.vis.sphere.P.x=0;fire.passes.push_back(&fire_pass);
    kich_r4_material::Source source;source.family=kich_r4_material::FAMILY_AREF;
    source.alpha_reference=200;kich_r4_material::PassState state;
    const kich_r4_material::Descriptor cutout=kich_r4_material::Build(source,state);
    native_materials.Register(&fire,std::vector<kich_r4_material::Descriptor>(2,cutout));
    draws.clear();renderer.Submit(&flame,NULL);assert(draws.size()==1 && active_material && active_material->semantic==kich_r4_material::CUTOUT);
    native_materials.Forget(&fire);draws.clear();renderer.Submit(&flame,NULL);assert(draws.size()==2);
    std::cout<<"PASS: production Submit/NativeElementPass route xdistort to E[4], selected textures/passes and effect 5; source UV/sampler preserved\n";
}
