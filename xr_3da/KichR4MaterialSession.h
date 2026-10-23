#ifndef KICH_R4_MATERIAL_SESSION_H
#define KICH_R4_MATERIAL_SESSION_H
#include "KichR4Material.h"
#include "KichR4LightingMath.h"
#include "KichR4LevelSession.h"
#include "KichR4Deferred.h"

// Shared level/model binding contract. A missing optional resource disables its
// feature explicitly; a checker texture must never be decoded as a normal/map.
namespace kich_r4_material {
enum ShaderFlag { SHADER_BUMP=1,SHADER_LIGHTMAP=2,SHADER_HEMI=4,SHADER_DETAIL=8,
    SHADER_VERTEX_LIGHT=16,SHADER_SUN=32,SHADER_TREE_LIGHT=64,SHADER_TREE_WIND=128 };
inline void SnapshotMetadata(const Descriptor& m,const char* category,const char* id) {
    if(!g_render_snapshot_active)return;
    using namespace kich_r4_telemetry;
    Text(category,id,"semantic",Name(m.semantic));Text(category,id,"source",m.source.c_str());
    Text(category,id,"shader",m.shader.c_str());U64(category,id,"source_class_id",m.class_id);
    U64(category,id,"source_family",m.source_family);U64(category,id,"source_flags",m.source_flags);
    U64(category,id,"source_alpha_reference",m.source_alpha_reference);
    U64(category,id,"effective_alpha_func",m.alpha_func);Float(category,id,"effective_alpha_ref",m.alpha_ref);
    U64(category,id,"pass_mask",m.passes);U64(category,id,"required_attributes",m.required_attributes);
    U64(category,id,"cull",m.cull);U64(category,id,"depth_test",m.depth_test);U64(category,id,"depth_write",m.depth_write);
    U64(category,id,"source_blend",m.src_blend);U64(category,id,"destination_blend",m.dst_blend);
    Text(category,id,"base",m.base.c_str());Text(category,id,"bump",m.bump.c_str());Text(category,id,"bump_aux",m.bump_aux.c_str());
    Text(category,id,"detail",m.detail.c_str());Text(category,id,"lightmap",m.lightmap.c_str());Text(category,id,"hemi",m.hemi.c_str());
    U64(category,id,"base_uv",m.base_uv);U64(category,id,"bump_uv",m.bump_uv);U64(category,id,"detail_uv",m.detail_uv);
    U64(category,id,"lightmap_uv",m.lightmap_uv);U64(category,id,"hemi_uv",m.hemi_uv);
    U64(category,id,"detail_usage",m.detail_usage);Vector(category,id,"detail_scale",m.detail_scale,2);
    U64(category,id,"tree_wave",m.tree_wave);Text(category,id,"unsupported_reason",m.unsupported_reason.empty()?"NONE":m.unsupported_reason.c_str());
}
inline void ExecutionID(const Descriptor& m,uint32_t attributes,uint32_t flags,bool shadow,char* out,size_t size) {
    snprintf(out,size,"mx_%016llx_%x_%x_%u",static_cast<unsigned long long>(Key(m)),attributes,flags,shadow?1:0);
}
inline void Bind(KichR4FrameSession& s,KichR4LevelSession& level,const Descriptor& m,
    uint32_t attributes,kich_r4_lighting::Constants& c,KichR4DeferredDrawDesc& draw,
    bool shadow,bool vertex_baked=false,uint32_t extra_flags=0,const char* main_category="MATERIAL_EXECUTION") {
    c.material[0]=0;c.material[1]=m.detail_scale[0];c.material[2]=m.detail_scale[1];c.material[3]=0;
    c.alpha[0]=m.alpha_ref;c.alpha[1]=static_cast<float>(m.alpha_func);
    uint32_t flags=extra_flags;std::string fallback=m.unsupported_reason;
    if(!shadow){
        const uint32_t basis=ATTR_NORMAL|ATTR_TANGENT|ATTR_BINORMAL;
        if(!m.bump.empty()){
            if((attributes&basis)!=basis)fallback+=";bump_missing_valid_source_basis";
            else if(m.bump_aux.empty())fallback+=";bump_missing_auxiliary_metadata";
            else {draw.textures[1]=level.Texture(s,m.bump);draw.textures[2]=level.Texture(s,m.bump_aux);
                if(draw.textures[1] && draw.textures[2] && draw.textures[1]!=level.checker && draw.textures[2]!=level.checker)flags|=SHADER_BUMP;
                else {draw.textures[1]=draw.textures[2]=0;fallback+=";bump_dds_unavailable";}}
        }
        const std::string* names[]={&m.lightmap,&m.hemi};const unsigned bits[]={SHADER_LIGHTMAP,SHADER_HEMI};
        for(unsigned i=0;i<2;++i)if(!names[i]->empty()){
            if(!(attributes&ATTR_UV1))fallback+=";lightmap_missing_source_uv1";
            else {draw.textures[3+i]=level.Texture(s,*names[i]);
                if(draw.textures[3+i] && draw.textures[3+i]!=level.checker)flags|=bits[i];
                else {draw.textures[3+i]=0;fallback+=";lightmap_dds_unavailable";}}
        }
        if(!m.detail.empty() && (m.detail_usage&1)){
            draw.textures[5]=level.Texture(s,m.detail);
            if(draw.textures[5] && draw.textures[5]!=level.checker)flags|=SHADER_DETAIL;
            else {draw.textures[5]=0;fallback+=";detail_dds_unavailable";}
        }
        if(vertex_baked && (attributes&ATTR_COLOR) && !(flags&SHADER_LIGHTMAP))flags|=SHADER_VERTEX_LIGHT;
        if(attributes&ATTR_SUN)flags|=SHADER_SUN;
    }
    c.material[0]=static_cast<float>(flags);
    if(g_render_snapshot_active){
        for(unsigned i=1;i<8;++i)RenderSnapshot_Texture(i,"UNBOUND");
        char id[64];ExecutionID(m,attributes,flags,shadow,id,sizeof(id));
        const char* category=shadow?"MATERIAL_SHADOW_EXECUTION":main_category;
        RenderSnapshot_MaterialRoute(id);SnapshotMetadata(m,category,id);
        kich_r4_telemetry::U64(category,id,"descriptor_key",Key(m));
        kich_r4_telemetry::U64(category,id,"source_attributes",attributes);
        kich_r4_telemetry::U64(category,id,"shader_flags",flags);
        kich_r4_telemetry::Text(category,id,"semantic",Name(m.semantic));
        kich_r4_telemetry::Text(category,id,"normal_path",flags&SHADER_BUMP?"SOC_R2_BUMP_WZY_PLUS_BUMP_AUX_RGB_MINUS_ONE; TBN; GBUFFER_NORMAL":"GEOMETRIC_NORMAL");
        kich_r4_telemetry::Text(category,id,"lightmap_path",flags&SHADER_LIGHTMAP?"UV1; RGB_BAKED_ADDED_BEFORE_DIRECT_LIGHT; ALPHA_SUN":"NO_BAKED_LIGHTMAP");
        kich_r4_telemetry::Text(category,id,"lighting_model","R4_DIFFUSE_COMPATIBILITY; LMAP0_RGB_RAW_ADDITIVE; STATIC_SUN_RAW_MASK_TIMES_2; NO_R2_MATERIAL_LUT_SPECULAR");
        kich_r4_telemetry::Text(category,id,"hemi_path",flags&SHADER_HEMI?"UV1; MEAN_RGB_HEMI; ALPHA_SUN":"SOURCE_VERTEX_HEMI");
        kich_r4_telemetry::Text(category,id,"fallback_reason",fallback.empty()?"NONE":fallback.c_str());
        kich_r4_telemetry::Text(category,id,"pass",shadow?"SHADOW; OPTIONAL_MAPS_INTENTIONALLY_UNUSED":"MAIN_MATERIAL; GBUFFER_OR_FORWARD");
        if(!shadow){const std::string* names[]={&m.bump,&m.bump_aux,&m.lightmap,&m.hemi,&m.detail};
            for(unsigned i=0;i<5;++i)RenderSnapshot_Texture(i+1,draw.textures[i+1]?names[i]->c_str():"UNBOUND");}
    }
}
}
#endif
