#ifndef KICH_R4_TEXTURE_PREFLIGHT_H
#define KICH_R4_TEXTURE_PREFLIGHT_H
#include "KichR4LevelSession.h"
#include "KichR4CaptureData.h"
#include "KichR4Game.h"
#include "KichR4DetailSession.h"
#include "KichR4Visibility.h"
#include "KichR4LevelVisibility.h"
namespace kich_r4_texture {
inline void MaterialDemand(KichR4LevelSession& level,const kich_r4_material::Descriptor& m,uint32_t role){
    level.NeedTexture(m.base,role);
    level.NeedTexture(m.lightmap,role<=HUD?role:uint32_t(LIGHTING));
    level.NeedTexture(m.hemi,role<=HUD?role:uint32_t(LIGHTING));
    level.NeedTexture(m.bump,NORMAL);level.NeedTexture(m.bump_aux,NORMAL);
    // The established SoC bump auxiliary fallback is itself a dependency.
    if(!m.bump.empty() && !m.bump_aux.empty()){
        const auto auxiliary=level.texture_registry.entries.find(Key(m.bump_aux));
        if(auxiliary!=level.texture_registry.entries.end() && auxiliary->second.reason==SourceUnavailable)
            level.NeedTexture("ed\\ed_dummy_bump#",NORMAL);
    }
    level.NeedTexture(m.detail,DETAIL);
}
inline bool AnyView(const kich_r4_capture::NativeModelCommand& model,const std::vector<kich_r4_scene::Matrix>& views){
    if(!model.bounds_known)return !views.empty();
    for(size_t i=0;i<views.size();++i)if(kich_r4_visibility::Visible(model.bounds.min,model.bounds.max,views[i]))return true;
    return false;
}
// Build the union before any draw or DDS eviction. Shadows request alpha only;
// a visible material reserves every texture slot, including late forward/UI.
inline int32_t Preflight(KichR4FrameSession& frame,KichR4LevelSession& level,
                        const kich_r4_capture::Batch& batch,const KichR4GameFrame& input,
                        const KichR4DetailSession& details,bool scene){
    kich_r4_scene::Matrix camera;memcpy(camera.m,input.view_projection,sizeof(camera.m));
    std::vector<kich_r4_scene::Matrix> shadows;
    std::vector<unsigned char> shadow_static(level.scene.draws.size(),0);
    auto add_shadow=[&](const kich_r4_scene::Matrix& vp,uint32_t seed){
        shadows.push_back(vp);kich_r4_level::StaticVisibility mask;
        kich_r4_level::SelectStatic(level.scene,vp,seed,input.tree_wind[0],input.tree_wind[2],mask);
        for(size_t i=0;i<mask.draws.size();++i)shadow_static[i]|=mask.draws[i];
    };
    if(scene){
        if(kich_r4_visibility::SunEnabled(input.lighting)){
            add_shadow(kich_r4_lighting::SunShadow(input.position,input.lighting.sun_direction,128.f),UINT32_MAX);
            add_shadow(kich_r4_lighting::SunShadow(input.position,input.lighting.sun_direction,512.f),UINT32_MAX);}
        std::vector<uint8_t> selected;kich_r4_visibility::SelectShadowLights(input.lighting,true,selected);
        for(size_t i=0;i<selected.size() && i<input.lighting.lights.size();++i)if(selected[i]){
            const KichR4LocalLight& light=input.lighting.lights[i];
            const uint32_t seed=level.files.sector?level.files.sector(light.position):UINT32_MAX;
            for(unsigned face=0;face<(light.type==2?1u:6u);++face)add_shadow(kich_r4_lighting::LocalShadow(light,face),seed);}
        kich_r4_level::StaticVisibility camera_static;
        const uint32_t camera_sector=level.files.sector?level.files.sector(input.position):UINT32_MAX;
        kich_r4_level::SelectStatic(level.scene,camera,camera_sector,input.tree_wind[0],input.tree_wind[2],camera_static);
        for(size_t i=0;i<level.scene.draws.size();++i){const kich_r4_level::Draw& mesh=level.scene.draws[i];
            const kich_r4_level::Material& material=level.scene.materials[mesh.material];
            const kich_r4_material::Descriptor& m=material.descriptor;
            if(camera_static.draws[i]){
                level.NeedTexture(material.texture,BASE);
                if(kich_r4_material::Geometry(m))MaterialDemand(level,m,BASE);
                if(material.kind==1){level.NeedTexture(material.texture+"_mask",LIGHTING);for(unsigned j=0;j<4;++j)level.NeedTexture(material.layers[j],DETAIL);}
            }else if((m.semantic!=kich_r4_material::UNKNOWN?m.alpha_func!=8:material.cutout) && shadow_static[i]){
                level.NeedTexture(material.texture,SHADOW_ALPHA);level.NeedTexture(m.base,SHADOW_ALPHA);}
        }
    }
    for(size_t i=0;i<batch.commands.size();++i){const kich_r4_capture::Command& c=batch.commands[i];
        if(!c.count || (!scene && c.layer!=2))continue;
        const uint32_t role=c.layer==2?UI:c.layer==1?HUD:c.effect?EFFECT:BASE;
        level.NeedTexture(c.texture,role);
        if(c.effect==2 || c.effect==3)level.NeedTexture(c.secondary,role);
    }
    if(scene)for(size_t i=0;i<batch.native_models.size();++i){const kich_r4_capture::NativeModelCommand& c=batch.native_models[i];if(!c.count)continue;
        const bool visible=c.layer || !c.bounds_known || kich_r4_visibility::Visible(c.bounds.min,c.bounds.max,camera);
        if(visible){const uint32_t role=c.layer?HUD:c.effect?EFFECT:BASE;
            level.NeedTexture(c.texture,role);
            if(c.has_material){level.NeedTexture(c.material.base,role);if(kich_r4_material::Geometry(c.material))MaterialDemand(level,c.material,role);}
        }else if((c.has_material?c.material.alpha_func:c.style.alpha_func)!=8 && AnyView(c,shadows)){
            level.NeedTexture(c.has_material && !c.material.base.empty()?c.material.base:c.texture,SHADOW_ALPHA);}
    }
    const kich_r4_detail::Frame* source=details.source;
    if(scene && source && source->source_frame_known && source->engine_frame==input.engine_frame){
        for(size_t i=0;i<source->groups.size();++i){const kich_r4_detail::Group& g=source->groups[i];
            if(g.count && g.mesh<source->meshes.size())level.NeedTexture(source->meshes[g.mesh].texture,BASE);}
        for(size_t i=0;i<source->shadow_groups.size();++i){const kich_r4_detail::Group& g=source->shadow_groups[i];if(!g.count || g.mesh>=source->meshes.size())continue;
            bool visible=false;
            for(size_t view=0;view<shadows.size() && !visible;++view)for(uint32_t j=0;j<g.count && uint64_t(g.first)+j<source->shadow_instances.size();++j)
                if(details.ShadowVisible(source->meshes[g.mesh],source->shadow_instances[g.first+j],g.variant,shadows[view])){visible=true;break;}
            if(visible)level.NeedTexture(source->meshes[g.mesh].texture,SHADOW_ALPHA);
        }
    }
    return level.CommitTexturePlan(frame);
}
}
#endif
