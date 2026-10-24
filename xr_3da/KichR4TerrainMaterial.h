#ifndef KICH_R4_TERRAIN_MATERIAL_H
#define KICH_R4_TERRAIN_MATERIAL_H
#include "KichR4MaterialSession.h"

namespace kich_r4_material {
inline KichR4SamplerState TerrainSampler(bool diffuse=false,uint32_t anisotropy=1){
    // SoC BmmD: tiled diffuse layers use anisotropic sampling, normals and
    // the mask use linear wrap. These are explicit material states, not the
    // unrelated shadow/cookie samplers which share these slots in later passes.
    // Respect the captured source filtering budget; terrain must not silently
    // force a higher anisotropy than the rest of the scene.
    const uint32_t level=anisotropy>=1 && anisotropy<=16?anisotropy:1;
    const KichR4SamplerState state={1,1,1,diffuse?3u:2u,diffuse?3u:2u,2,diffuse?level:1u,0,0,0};return state;
}
inline void TerrainSampler(KichR4DeferredDrawDesc& draw,unsigned slot,const KichR4SamplerState& sampler){
    if(slot<8)draw.state.surface.samplers[slot]=sampler;else draw.extra_samplers[slot-8]=sampler;
}
inline bool TerrainTexture(KichR4FrameSession& frame,KichR4LevelSession& level,const std::string& name,KichR4Handle& texture){
    texture=name.empty()?0:level.Texture(frame,name);if(texture==level.checker)texture=0;return texture!=0;
}
inline void BindTerrain(KichR4FrameSession& frame,KichR4LevelSession& level,const Descriptor& material,
    uint32_t attributes,kich_r4_lighting::Constants& constants,KichR4DeferredDrawDesc& draw){
    if(!Terrain(material))return;
    uint32_t flags=uint32_t(constants.material[0])|SHADER_TERRAIN_HEMI;
    const bool layers=TerrainLayers(material);const uint32_t basis=ATTR_NORMAL|ATTR_TANGENT|ATTR_BINORMAL;
    const char* failure="NONE";
    if(layers){
        KichR4Handle maps[9]={};bool diffuse=TerrainTexture(frame,level,material.terrain_mask,maps[0]);
        for(unsigned i=0;i<4;++i)diffuse=TerrainTexture(frame,level,material.terrain_layers[i],maps[i+1]) && diffuse;
        if(diffuse){
            flags|=SHADER_TERRAIN_LAYERS;for(unsigned i=0;i<5;++i){draw.textures[i+1]=maps[i];TerrainSampler(draw,i+1,TerrainSampler(i!=0,draw.state.surface.samplers[0].anisotropy));}
            bool normal=(attributes&basis)==basis;
            if(normal){for(unsigned i=0;i<4;++i)normal=TerrainTexture(frame,level,material.terrain_bumps[i],maps[i+5]) && normal;
                if(!normal)failure="TERRAIN_LAYER_NORMAL_UNAVAILABLE";
            }else failure="TERRAIN_SOURCE_BASIS_INVALID";
            if(normal){flags|=SHADER_TERRAIN_NORMAL;for(unsigned i=0;i<4;++i){draw.textures[i+6]=maps[i+5];TerrainSampler(draw,i+6,TerrainSampler());}}
        }else failure="TERRAIN_MASK_OR_DIFFUSE_LAYER_UNAVAILABLE";
    }
    if(TerrainTexture(frame,level,material.terrain_lmap,draw.textures[10])){flags|=SHADER_TERRAIN_SUN;TerrainSampler(draw,10,TerrainSampler());}
    constants.material[0]=float(flags);
    if(g_render_snapshot_active){
        using namespace kich_r4_telemetry;char id[64];ExecutionID(material,attributes,flags,false,id,sizeof(id));
        RenderSnapshot_MaterialRoute(id);
        SnapshotMetadata(material,"TERRAIN_MATERIAL_EXECUTION",id);
        Text("TERRAIN_MATERIAL_EXECUTION",id,"normal_path",flags&SHADER_TERRAIN_NORMAL?"R2_BMM_MASK_WEIGHTED_WZY_MINUS_HALF_TBN":flags&SHADER_BUMP?"AUTHORED_BASE_BUMP_PAIR":"GEOMETRIC_NORMAL");
        Text("TERRAIN_MATERIAL_EXECUTION",id,"branch",layers?"HQ_FOUR_AUTHORED_LAYERS":material.bump.empty()?"SOURCE_FLAT_NO_DETAIL":"AUTHORED_BASE_BUMP");
        Text("TERRAIN_MATERIAL_EXECUTION",id,"fallback_reason",failure);
        Text("TERRAIN_MATERIAL_EXECUTION",id,"hemi","BASE_ALPHA; SOURCE_LMAP_ALPHA_UV0_SUN_DIAGNOSTIC; NO_LMAP_RGB_IRRADIANCE");
        U64("TERRAIN_MATERIAL_EXECUTION",id,"source_attributes",attributes);U64("TERRAIN_MATERIAL_EXECUTION",id,"shader_flags",flags);
        Vector("TERRAIN_MATERIAL_EXECUTION",id,"detail_scale",material.detail_scale,2);
        const std::string* names[]={&material.base,&material.terrain_mask,&material.terrain_layers[0],&material.terrain_layers[1],&material.terrain_layers[2],&material.terrain_layers[3],
            &material.terrain_bumps[0],&material.terrain_bumps[1],&material.terrain_bumps[2],&material.terrain_bumps[3],&material.terrain_lmap};
        for(unsigned slot=0;slot<11;++slot){if(!layers && slot>0 && slot<10)continue;
            char key[24];snprintf(key,sizeof(key),"texture_%u",slot);Text("TERRAIN_MATERIAL_EXECUTION",id,key,draw.textures[slot]?names[slot]->c_str():"UNBOUND");
            if(slot<8)RenderSnapshot_Texture(slot,draw.textures[slot]?names[slot]->c_str():"UNBOUND");}
    }
}
}
#endif
