#ifndef KICH_R4_TEXTURE_PREFLIGHT_H
#define KICH_R4_TEXTURE_PREFLIGHT_H
#include "KichR4LevelSession.h"
#include "KichR4CaptureData.h"
#include "KichR4Game.h"
#include "KichR4DetailSession.h"
#include "KichR4Visibility.h"
#include "KichR4LevelVisibility.h"
#include "KichR4PostprocessNoise.h"
#include "KichR4ShadowPoolPolicy.h"
#include <unordered_map>
namespace kich_r4_texture {
// Exact source strings preserve the registry's existing canonicalization and
// first-source rules. A stronger later role must still reach the registry.
// This cache lives only during preflight: never retain a handle or retry state.
struct FrameDemand {
    KichR4LevelSession& level;std::unordered_map<std::string,uint32_t> requested;
    explicit FrameDemand(KichR4LevelSession& owner):level(owner){}
    void NeedTexture(const std::string& name,uint32_t role){
        if(name.empty() || name.compare(0,12,"r4_runtime__")==0)return;
        const auto found=requested.find(name);
        if(found!=requested.end()){
            if(found->second<=role)return;
            found->second=role;
        }else requested.emplace(name,role);
        level.NeedTexture(name,role);
    }
};
inline void MaterialDemand(FrameDemand& demand,const kich_r4_material::Descriptor& m,uint32_t role){
    KichR4LevelSession& level=demand.level;
    demand.NeedTexture(m.base,role);
    demand.NeedTexture(m.lightmap,role<=HUD?role:uint32_t(LIGHTING));
    demand.NeedTexture(m.hemi,role<=HUD?role:uint32_t(LIGHTING));
    demand.NeedTexture(m.bump,NORMAL);demand.NeedTexture(m.bump_aux,NORMAL);
    // The established SoC bump auxiliary fallback is itself a dependency.
    if(!m.bump.empty() && !m.bump_aux.empty()){
        const auto auxiliary=level.texture_registry.entries.find(Key(m.bump_aux));
        if(auxiliary!=level.texture_registry.entries.end() && auxiliary->second.reason==SourceUnavailable)
            demand.NeedTexture("ed\\ed_dummy_bump#",NORMAL);
    }
    demand.NeedTexture(m.detail,DETAIL);
    if(kich_r4_material::Terrain(m)){
        demand.NeedTexture(m.terrain_lmap,LIGHTING);
        if(kich_r4_material::TerrainLayers(m)){
            demand.NeedTexture(m.terrain_mask,LIGHTING);
            for(unsigned i=0;i<4;++i){demand.NeedTexture(m.terrain_layers[i],DETAIL);demand.NeedTexture(m.terrain_bumps[i],NORMAL);}}
    }
}
inline bool AnyView(const kich_r4_capture::NativeModelCommand& model,const std::vector<kich_r4_scene::Matrix>& views,
                    const std::vector<unsigned char>& sun,const kich_r4_visibility::SunCasterVolume& volume){
    if(!model.bounds_known || !model.bounds.valid)return !views.empty();
    for(size_t i=0;i<views.size();++i)if(kich_r4_visibility::Visible(model.bounds.min,model.bounds.max,views[i]) && (!sun[i] || volume.Visible(model.bounds.min,model.bounds.max)))return true;
    return false;
}
// Build the union before any draw or DDS eviction. Shadows request alpha only;
// a visible material reserves every texture slot, including late forward/UI.
inline int32_t Preflight(KichR4FrameSession& frame,KichR4LevelSession& level,
                        const kich_r4_capture::Batch& batch,const KichR4GameFrame& input,
                        KichR4DetailSession& details,bool scene,kich_r4_level::StaticFramePlan* shared_plan=NULL,bool pump_loading=false){
    KichR4LevelSession::PreflightProbeScope probe(level,g_render_snapshot_active);
    const auto traversal_started=kich_r4_metrics::Start(g_render_snapshot_active);
    KichR4LevelSession::PreparedScope prepared(level);
    FrameDemand demand(level);
    kich_r4_level::StaticFramePlan fallback;
    if(!shared_plan){if(scene)fallback.Begin(level.scene,level.revision,input.tree_wind[0],input.tree_wind[2]);shared_plan=&fallback;}
    kich_r4_scene::Matrix camera;memcpy(camera.m,input.view_projection,sizeof(camera.m));
    if(scene && shared_plan==&fallback)shared_plan->sun_casters=kich_r4_visibility::BuildSunCasterVolume(camera,input.lighting.sun_direction);
    if(scene)details.EnsureVisibilityFrame(input.engine_frame,shared_plan->sun_casters);
    std::vector<kich_r4_scene::Matrix> shadows;
    std::vector<unsigned char> shadow_sun,shadow_grass;
    std::vector<unsigned char> shadow_static(level.scene.draws.size(),0);
    auto add_shadow=[&](const kich_r4_scene::Matrix& vp,uint32_t seed,bool sun){
        shadows.push_back(vp);shadow_sun.push_back(sun?1:0);shadow_grass.push_back(1);const kich_r4_level::StaticVisibility& mask=shared_plan->Select(vp,seed,sun);
        for(size_t i=0;i<mask.selected.size();++i)shadow_static[mask.selected[i]]=1;
    };
    if(scene){
        // Selected projectors share the full-frame residency plan; never load
        // their DDS or evict a material later during local accumulation.
        for(unsigned i=0;i<input.lighting.count && i<input.lighting.projectors.size();++i)
            if(input.lighting.lights[i].type==2)demand.NeedTexture(input.lighting.projectors[i].texture,LIGHTING);
        // Combine hmodel() samples both weather env cubemaps every scene frame.
        for(unsigned i=0;i<2;++i)if(!input.lighting.env_textures[i].empty())demand.NeedTexture(input.lighting.env_textures[i],LIGHTING);
        if(kich_r4_visibility::SunEnabled(input.lighting)){
            if(input.lighting.sunmask_valid)demand.NeedTexture("sunmask",LIGHTING); // R2 cloud sun mask
            add_shadow(kich_r4_lighting::SunCascade(input.lighting,input.position,128.f),UINT32_MAX,true);
            add_shadow(kich_r4_lighting::SunFarCascade(input.lighting,input.position),UINT32_MAX,true);
            // P0-S8: as the deferred pass, no grass in the 512 m map within 64 m.
            {const float grass_radius=details.ShadowRadius();if(grass_radius>0 && grass_radius<=64.f)shadow_grass.back()=0;}
            kich_r4_scene::Matrix contact;if(kich_r4_lighting::SunContactShadow(input.lighting,contact))add_shadow(contact,UINT32_MAX,true);}
        std::vector<uint8_t> selected;kich_r4_visibility::SelectShadowLights(input.lighting,true,selected);
        uint32_t faces=0;for(size_t i=0;i<selected.size() && i<input.lighting.lights.size();++i)if(selected[i])faces+=input.lighting.lights[i].type==2?1:6;
        const uint32_t shadow_size=input.lighting.local_shadow_size?input.lighting.local_shadow_size:kich_r4_shadow_pool::BestFit(faces);
        if(!shadow_size)return frame.Record("E1.5 preflight shadow pool budget",KICH_R4_E_ARGUMENT);
        for(size_t i=0;i<selected.size() && i<input.lighting.lights.size();++i)if(selected[i]){
            const KichR4LocalLight& light=input.lighting.lights[i];
            const uint32_t seed=level.files.sector?level.files.sector(light.position):UINT32_MAX;
            // P0-S7 native plan: visible faces at the light's own size.
            const bool planned=input.lighting.ShadowPlanned();const uint32_t mask=input.lighting.ShadowFaceMask(static_cast<unsigned>(i));
            const uint32_t size=planned?input.lighting.ShadowSize(static_cast<unsigned>(i)):shadow_size;
            for(unsigned face=0;face<(light.type==2?1u:6u);++face)if(mask&(1u<<face))add_shadow(kich_r4_lighting::LocalShadow(light,face,size),seed,false);}
        const uint32_t camera_sector=level.files.sector?level.files.sector(input.position):UINT32_MAX;
        const kich_r4_level::StaticVisibility& camera_static=shared_plan->Select(camera,camera_sector);
        // Expand each static material once per role. Unknown camera materials
        // can still name a distinct shadow-alpha source, so retain both bits.
        std::vector<unsigned char> material_demands(level.scene.materials.size(),0);
        for(size_t i=0;i<level.scene.draws.size();++i){const kich_r4_level::Draw& mesh=level.scene.draws[i];
            const kich_r4_level::Material& material=level.scene.materials[mesh.material];
            const kich_r4_material::Descriptor& m=material.descriptor;
            unsigned char& requested=material_demands[mesh.material];
            if(camera_static.draws[i]){
                if(requested&2)continue;
                requested|=2;demand.NeedTexture(material.texture,BASE);
                if(kich_r4_material::Geometry(m))MaterialDemand(demand,m,BASE);
            }else if(!(requested&1) && (m.semantic!=kich_r4_material::UNKNOWN?kich_r4_material::AlphaFunction(m,true)!=8:material.cutout) && shadow_static[i]){
                requested|=1;demand.NeedTexture(material.texture,SHADOW_ALPHA);demand.NeedTexture(m.base,SHADOW_ALPHA);}
        }
    }
    for(size_t i=0;i<batch.commands.size();++i){const kich_r4_capture::Command& c=batch.commands[i];
        if(!c.count || (!scene && c.layer!=2))continue;
        const uint32_t role=c.layer==2?UI:c.layer==1?HUD:c.effect?EFFECT:BASE;
        demand.NeedTexture(c.texture,role);
        if(c.effect==2 || c.effect==3)demand.NeedTexture(c.secondary,role);
    }
    if(scene)for(size_t i=0;i<batch.native_models.size();++i){const kich_r4_capture::NativeModelCommand& c=batch.native_models[i];if(!c.count)continue;
        const bool visible=c.layer || !c.bounds_known || kich_r4_visibility::Visible(c.bounds.min,c.bounds.max,camera);
        if(visible){const uint32_t role=c.layer?HUD:c.effect?EFFECT:BASE;
            demand.NeedTexture(c.texture,role);
            if(c.has_material){demand.NeedTexture(c.material.base,role);if(kich_r4_material::Geometry(c.material))MaterialDemand(demand,c.material,role);}
        }else if((c.has_material?kich_r4_material::AlphaFunction(c.material,true):kich_r4_material::AlphaFunction(c.style,true))!=8 && AnyView(c,shadows,shadow_sun,shared_plan->sun_casters)){
            demand.NeedTexture(c.has_material && !c.material.base.empty()?c.material.base:c.texture,SHADOW_ALPHA);}
    }
    const kich_r4_detail::Frame* source=details.source;
    if(scene && source && source->source_frame_known && source->engine_frame==input.engine_frame){
        // P0-S5: detail atlases ship with one level; request the generated chain first.
        for(size_t i=0;i<source->meshes.size();++i)level.RequestMips(source->meshes[i].texture);
        for(size_t i=0;i<source->groups.size();++i){const kich_r4_detail::Group& g=source->groups[i];
            if(g.count && g.mesh<source->meshes.size())demand.NeedTexture(source->meshes[g.mesh].texture,BASE);}
        std::vector<unsigned char> needed(source->meshes.size(),0);
        for(size_t view=0;view<shadows.size();++view){
            if(!shadow_sun[view] && !input.lighting.local_detail_shadows)continue; // P0-S7: no grass in lamp maps
            if(!shadow_grass[view])continue;
            const KichR4DetailSession::ShadowSelection& selection=details.SelectShadow(shadows[view],shadow_sun[view]!=0);
            for(size_t i=0;i<selection.groups.size();++i)if(selection.groups[i].mesh<needed.size())needed[selection.groups[i].mesh]=1;}
        for(size_t i=0;i<needed.size();++i)if(needed[i])demand.NeedTexture(source->meshes[i].texture,SHADOW_ALPHA);
    }
    // The fullscreen effect shares the frame's residency plan. Never upload or
    // evict world textures late in Process, and never request noise for menus.
    if(scene && kich_r4_noise::Amount(input.postprocess[7])>0 && input.postprocess[9]>0 && std::isfinite(input.postprocess[9]))
        demand.NeedTexture(kich_r4_noise::Texture(),EFFECT);
    level.preflight_probe.traversal_ms=kich_r4_metrics::Elapsed(traversal_started);
    const auto commit_started=kich_r4_metrics::Start(g_render_snapshot_active);
    const int32_t result=level.CommitTexturePlan(frame,pump_loading);
    level.preflight_probe.commit_ms=kich_r4_metrics::Elapsed(commit_started);
    return result;
}
}
#endif
