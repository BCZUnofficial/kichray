#ifndef RENDER_SNAPSHOT_MATERIAL_DATA_H
#define RENDER_SNAPSHOT_MATERIAL_DATA_H
#include "RenderSnapshot.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
namespace snapshot_material {
inline std::string Text(const char* s,size_t limit=256){return s?std::string(s,std::min(strlen(s),limit)):std::string();}
inline uint64_t Hash(const void* data,size_t n,uint64_t seed=UINT64_C(14695981039346656037)){const unsigned char* p=static_cast<const unsigned char*>(data);for(size_t i=0;i<n;++i){seed^=p[i];seed*=UINT64_C(1099511628211);}return seed;}
inline std::string ID(const char* prefix,const std::string& key){char value[96];snprintf(value,sizeof(value),"%s_%016llx",prefix,static_cast<unsigned long long>(Hash(key.data(),key.size())));return value;}
inline void Number(const char* category,const std::string& id,const char* key,uint64_t value){char n[32];snprintf(n,sizeof(n),"%llu",static_cast<unsigned long long>(value));RenderSnapshot_Record(category,id.c_str(),key,n);}
inline void Field(const char* category,const std::string& id,const char* key,const char* value){RenderSnapshot_Record(category,id.c_str(),key,value && *value?value:"NOT_AVAILABLE");}
struct Source {
    uint32_t element,usage;uint64_t class_id=UINT64_MAX;std::string shader,textures,origin,bump,detail;
    std::string Key()const{return shader+"\n"+textures+"\n"+origin+"\n"+bump+"\n"+detail+"\n"+std::to_string(element)+"/"+std::to_string(usage)+"/"+std::to_string(class_id);}
};
struct Sampler {std::string semantic,texture;uint32_t slot,address,min_filter,mip_filter,mag_filter;
    std::string Key()const{return semantic+"="+texture+"@"+std::to_string(slot)+":"+std::to_string(address)+","+std::to_string(min_filter)+","+std::to_string(mip_filter)+","+std::to_string(mag_filter);}};
struct SourceSet {std::vector<Source> values;uint32_t aliases_omitted=0;};
struct SamplerSet {std::vector<Sampler> values;bool truncated=false;};
struct Variant {std::string name,profile,defines,include_manifest,source_excerpts;uint64_t source_hash,bytecode_hash;uint32_t source_bytes,bytecode_bytes;bool defines_truncated;};
/* Independent caps prevent source metadata from becoming an unbounded trace.
 * An omitted entry has explicit availability, never a guessed requirement. */
struct Registry {
    std::map<const void*,SourceSet> sources;
    std::map<const void*,SamplerSet> pending,passes;
    std::map<std::string,Variant> variants;
    std::string include_manifest,source_excerpts;
    uint64_t dropped_sources=0,dropped_passes=0,dropped_variants=0;
    void AddSource(const void* element,const Source& source){if(!element)return;
        if(!sources.count(element) && sources.size()>=2048){++dropped_sources;return;}
        SourceSet& set=sources[element];const std::string key=source.Key();for(size_t i=0;i<set.values.size();++i)if(set.values[i].Key()==key)return;
        if(set.values.size()>=2){++set.aliases_omitted;return;}set.values.push_back(source);
        std::sort(set.values.begin(),set.values.end(),[](const Source& a,const Source& b){return a.Key()<b.Key();});}
    void Begin(const void* compiler){if(!pending.count(compiler) && pending.size()>=32){++dropped_passes;return;}pending[compiler]=SamplerSet();}
    void AddSampler(const void* compiler,const Sampler& sampler){const auto i=pending.find(compiler);if(i==pending.end()){++dropped_passes;return;}
        if(i->second.values.size()>=32){i->second.truncated=true;return;}i->second.values.push_back(sampler);}
    void End(const void* compiler,const void* pass){const auto i=pending.find(compiler);if(i==pending.end())return;
        if(pass){if(passes.count(pass) || passes.size()<2048)passes[pass]=i->second;else ++dropped_passes;}pending.erase(i);}
    void AddVariant(const Variant& variant){const std::string key=variant.name+"|"+(variant.profile.empty()?"?":variant.profile.substr(0,2));
        if(!variants.count(key) && variants.size()>=4096){++dropped_variants;return;}variants[key]=variant;}
    const Variant* FindVariant(const char* name,bool vertex)const{const auto i=variants.find(Text(name)+"|"+(vertex?"vs":"ps"));return i==variants.end()?NULL:&i->second;}
};
struct View {const void* element;const void* pass;uint32_t pass_index,selected_element,available_mask,flags;bool native;
    const char* vs;const char* ps;const char* reason;
    std::string render_states;uint32_t alpha_test=UINT32_MAX,blend=UINT32_MAX;
    std::vector<std::pair<uint32_t,std::string> > textures;
    View():element(NULL),pass(NULL),pass_index(0),selected_element(UINT32_MAX),available_mask(UINT32_MAX),flags(0),native(false),vs("NONE"),ps("NONE"),reason("ELEMENT_BOUND; REQUEST_SITE_NOT_INSTRUMENTED"){} };
inline void LongField(const char* category,const std::string& id,const char* key,const std::string& text){
    if(text.empty()){Field(category,id,key,"NONE_OBSERVED_OR_NOT_INSTRUMENTED");return;}
    for(size_t at=0;at<text.size() && at<6144;at+=1536){char part[96];snprintf(part,sizeof(part),"%s_part_%u",key,static_cast<unsigned>(at/1536));Field(category,id,part,text.substr(at,1536).c_str());}
}
inline std::string EmitVariant(const Registry& r,const char* name,bool vertex){const Variant* v=r.FindVariant(name,vertex);if(!v)return "NOT_INSTRUMENTED";
    const std::string id=ID("variant",v->name+"\n"+v->profile+"\n"+v->defines+"\n"+std::to_string(v->bytecode_hash));
    Field("SHADER",id,"name",v->name.c_str());Field("SHADER",id,"profile",v->profile.c_str());Field("SHADER",id,"stage",vertex?"VS":"PS");
    Number("SHADER",id,"top_source_fnv1a64",v->source_hash);Number("SHADER",id,"compiled_bytecode_fnv1a64",v->bytecode_hash);
    Number("SHADER",id,"top_source_bytes",v->source_bytes);Number("SHADER",id,"compiled_bytecode_bytes",v->bytecode_bytes);
    LongField("SHADER",id,"compiler_defines",v->defines);Field("SHADER",id,"defines_status",v->defines_truncated?"TRUNCATED":"COMPLETE_ARGUMENT_ARRAY");
    LongField("SHADER",id,"include_content_hashes",v->include_manifest);LongField("SHADER",id,"decode_source_excerpts",v->source_excerpts);Field("SHADER",id,"source_evidence_limits","INCLUDE_MANIFEST_4096_BYTES; ASSIGNMENTS_FIRST_32768_BYTES_PER_INCLUDE_AND_4096_TOTAL; LINE_PREFIX_240_BYTES");Field("SHADER",id,"source_excerpt_scope","BOUNDED_UNPREPROCESSED_INCLUDE_ASSIGNMENTS; DEFINES_AND_BYTECODE_IDENTIFY_CHOSEN_VARIANT; NOT_PIXEL_EXECUTION");Field("SHADER",id,"pixel_branch_execution","NOT_MEASURED");return id;}
inline std::string Observe(const Registry& r,const View& v){
    const auto si=r.sources.find(v.element);const auto pi=r.passes.find(v.pass);
    std::string key=std::string(v.native?"R4_SOURCE":"LEGACY_SOURCE")+"\n"+Text(v.vs)+"\n"+Text(v.ps)+"\n"+std::to_string(v.pass_index)+"/"+std::to_string(v.selected_element)+"/"+std::to_string(v.flags)+"/"+std::to_string(v.available_mask)+"\n"+Text(v.reason);
    key+="\n"+v.render_states;
    if(si!=r.sources.end())for(size_t i=0;i<si->second.values.size();++i)key+="\n"+si->second.values[i].Key();
    if(pi!=r.passes.end())for(size_t i=0;i<pi->second.values.size();++i)key+="\n"+pi->second.values[i].Key();
    for(size_t i=0;i<v.textures.size();++i)key+="\nslot"+std::to_string(v.textures[i].first)+"="+v.textures[i].second;
    const std::string vs_variant=EmitVariant(r,v.vs,true),ps_variant=EmitVariant(r,v.ps,false);
    key+="\n"+vs_variant+"\n"+ps_variant;
    const std::string id=ID("material",key);RenderSnapshot_MaterialRoute(id.c_str());
    Field("MATERIAL",id,"backend",v.native?"R4_NATIVE":"LEGACY_D3D9");Field("MATERIAL",id,"definition_scope","SOURCE_DECLARATION; NOT_PIXEL_CONTRIBUTION");
    Field("MATERIAL",id,"pass_metadata_scope","LATEST_COMPILER_SAMPLER_DECLARATIONS_FOR_SHARED_PASS");
    Field("MATERIAL",id,"selection_reason",v.reason);if(v.pass_index==UINT32_MAX)Field("MATERIAL",id,"pass_index","NOT_EXECUTED");else Number("MATERIAL",id,"pass_index",v.pass_index);
    Field("MATERIAL",id,"selection_result",v.pass?"SELECTED_SOURCE_PASS":"NO_SOURCE_PASS_BOUND");
    if(v.pass_index==UINT32_MAX)Field("MATERIAL",id,"selected_element","NOT_SELECTED");
    else if(v.selected_element!=UINT32_MAX)Number("MATERIAL",id,"selected_element",v.selected_element);else Field("MATERIAL",id,"selected_element","NOT_INSTRUMENTED_AT_REQUEST_SITE");
    if(v.available_mask!=UINT32_MAX){Number("MATERIAL",id,"available_element_mask",v.available_mask);unsigned count=0;for(unsigned i=0;i<6;++i)count+=(v.available_mask>>i)&1;Number("MATERIAL",id,"available_elements",count);}
    else Field("MATERIAL",id,"available_elements","NOT_INSTRUMENTED_AT_REQUEST_SITE");
    Field("MATERIAL",id,"requested_element",v.selected_element==UINT32_MAX?(strncmp(v.reason,"NORMAL_",7)==0?"NORMAL_E0_WITH_E1_FALLBACK":"NOT_INSTRUMENTED_AT_REQUEST_SITE"):(v.flags&1)?"SPECIAL_DISTORTION_E4_E0_E1_PRIORITY":"NORMAL_E0_WITH_E1_FALLBACK");
    Field("MATERIAL",id,"source_vs",v.vs);Field("MATERIAL",id,"source_ps",v.ps);
    Field("MATERIAL",id,"source_vs_variant",vs_variant.c_str());Field("MATERIAL",id,"source_ps_variant",ps_variant.c_str());
    Field("MATERIAL",id,"source_render_state_declarations",v.render_states.empty()?"NOT_AVAILABLE":v.render_states.c_str());
    Field("MATERIAL",id,"actual_backend_shader",v.native?"SEE_DRAW_ACTUAL_VS_PS_AND_RESOURCE_BINDINGS":"SOURCE_PASS_BINDING; SEE_DRAW_FOR_DEVICE_QUERY");
    Field("MATERIAL",id,"hs_ds_gs_cs",v.native?"NOT_USED_BY_CURRENT_PIPELINE":"NOT_SUPPORTED_BY_D3D9");
    Field("MATERIAL",id,"source_semantic_origin",v.native?"NATIVE_R1_FRONTEND_SOURCE; EFFECT_FLAGS_THEN_PS_NAME_HEURISTIC":"LEGACY_COMPILER_SOURCE; FRAME_RENDERER_IDENTIFIES_R1_R2");
    Field("MATERIAL",id,"strict_back_to_front",v.flags&8?"SOURCE_FLAGGED":"NOT_SOURCE_FLAGGED");Number("MATERIAL",id,"source_priority",(v.flags>>8)&3);
    Number("MATERIAL",id,"source_flags",v.flags);Field("MATERIAL",id,"distortion_requirement",v.flags&1?"FLAGGED":"NOT_FLAGGED");Field("MATERIAL",id,"emissive_requirement",v.flags&2?"FLAGGED":"NOT_FLAGGED");Field("MATERIAL",id,"wallmark_requirement",v.flags&4?"FLAGGED":"NOT_FLAGGED");
    if(si==r.sources.end())Field("MATERIAL",id,"source_metadata","NOT_AVAILABLE_OR_REGISTRY_LIMIT");
    else{Number("MATERIAL",id,"source_aliases_retained",si->second.values.size());Number("MATERIAL",id,"source_alias_registration_observations_omitted",si->second.aliases_omitted);
        Field("MATERIAL",id,"source_metadata",si->second.values.size()>1 || si->second.aliases_omitted?"SHARED_ELEMENT_ALIASES; EXACT_REQUEST_UNKNOWN":"REGISTERED_SOURCE");
        for(size_t i=0;i<si->second.values.size();++i){const Source& s=si->second.values[i];char k[64];
            snprintf(k,sizeof(k),"source_%u_material",static_cast<unsigned>(i));Field("MATERIAL",id,k,s.shader.c_str());snprintf(k,sizeof(k),"source_%u_textures",static_cast<unsigned>(i));Field("MATERIAL",id,k,s.textures.c_str());
            snprintf(k,sizeof(k),"source_%u_origin",static_cast<unsigned>(i));Field("MATERIAL",id,k,s.origin.c_str());snprintf(k,sizeof(k),"source_%u_element",static_cast<unsigned>(i));Number("MATERIAL",id,k,s.element);
            snprintf(k,sizeof(k),"source_%u_class_id",static_cast<unsigned>(i));if(s.class_id!=UINT64_MAX)Number("MATERIAL",id,k,s.class_id);else Field("MATERIAL",id,k,"NOT_AVAILABLE_FOR_SCRIPT_OR_UNKNOWN_SOURCE");
            snprintf(k,sizeof(k),"source_%u_bump_assigned",static_cast<unsigned>(i));Field("NORMAL_MAPPING",id,k,s.bump.empty()?"NOT_ASSIGNED":s.bump.c_str());
            snprintf(k,sizeof(k),"source_%u_detail_assigned",static_cast<unsigned>(i));Field("NORMAL_MAPPING",id,k,s.detail.empty()?"NOT_ASSIGNED":s.detail.c_str());
            snprintf(k,sizeof(k),"source_%u_detail_usage_flags",static_cast<unsigned>(i));Number("NORMAL_MAPPING",id,k,s.usage);}}
    bool bump=false,bumpx=false,detail=false;unsigned samplers=0;
    if(pi==r.passes.end())Field("MATERIAL",id,"sampler_declarations","NOT_INSTRUMENTED_R1_OR_REGISTRY_LIMIT");
    else{Field("MATERIAL",id,"sampler_declarations",pi->second.truncated?"TRUNCATED":"R_SAMPLER_CALLS");
        for(size_t i=0;i<pi->second.values.size();++i){const Sampler& p=pi->second.values[i];char k[64],value[640];++samplers;
            snprintf(k,sizeof(k),"sampler_%u",static_cast<unsigned>(i));snprintf(value,sizeof(value),"semantic=%s;resource=%s;slot=%s;r_sampler_address_argument=%u;min=%u;mip=%u;mag=%u;selection=%s",p.semantic.c_str(),p.texture.c_str(),p.slot==UINT32_MAX?"NOT_AVAILABLE":std::to_string(p.slot).c_str(),p.address,p.min_filter,p.mip_filter,p.mag_filter,p.slot==UINT32_MAX?"ABSENT_FROM_COMPILED_CONSTANT_TABLE":"SELECTED_SOURCE_SLOT");Field("MATERIAL",id,k,value);
            if(p.semantic=="s_bump")bump=true;
            if(p.semantic=="s_bumpX")bumpx=true;
            if(p.semantic=="s_detail")detail=true;
            if(p.semantic=="s_bump" || p.semantic=="s_bumpX" || p.semantic=="s_detailBump" || p.semantic=="s_detailBumpX" || p.semantic=="s_bumpD")Field("NORMAL_MAPPING",id,p.semantic.c_str(),p.texture.c_str());}}
    Number("MATERIAL",id,"declared_sampler_count",samplers);
    for(size_t i=0;i<v.textures.size();++i){char k[48];snprintf(k,sizeof(k),"pass_texture_slot_%u",v.textures[i].first);Field("MATERIAL",id,k,v.textures[i].second.c_str());}
    Field("MATERIAL",id,"flat_requirement",pi==r.passes.end()?"NOT_INSTRUMENTED":bump?"BUMP_DECLARATION_PRESENT":"NO_BUMP_SAMPLER_DECLARATION; NOT_PIXEL_EXECUTION_PROOF");
    Field("MATERIAL",id,"cutout_requirement",v.alpha_test==UINT32_MAX?"NOT_DECLARED_IN_PASS; INHERITED_STATE":v.alpha_test?"SOURCE_ALPHA_TEST_ENABLED":"SOURCE_ALPHA_TEST_DISABLED");Field("MATERIAL",id,"blend_requirement",v.blend==UINT32_MAX?"NOT_DECLARED_IN_PASS; INHERITED_STATE":v.blend?"SOURCE_BLEND_ENABLED":"SOURCE_BLEND_DISABLED");
    Field("MATERIAL",id,"parallax_requirement","SEE_COMPILED_SOURCE_AND_DEFINES; NOT_INFERRED_FROM_RESOURCE_NAME");
    Field("NORMAL_MAPPING",id,"metadata_source","TEXTURE_DESCRIPTOR_AND_R_SAMPLER; NOT_DDS_FILENAME_INFERENCE");
    Field("NORMAL_MAPPING",id,"bump_declared",bump?"YES":pi==r.passes.end()?"NOT_INSTRUMENTED":"NO");Field("NORMAL_MAPPING",id,"bump_aux_declared",bumpx?"YES":pi==r.passes.end()?"NOT_INSTRUMENTED":"NO");
    Field("NORMAL_MAPPING",id,"detail_declared",detail?"YES":pi==r.passes.end()?"NOT_INSTRUMENTED":"NO");
    Field("NORMAL_MAPPING",id,"actual_normal_mapping",v.native?"SEE_MATERIAL_EXECUTION_AND_MODEL_MATERIAL_EXECUTION; SOURCE_METADATA_NOT_EXECUTION":"SEE_COMPILED_VARIANT_AND_ACTUAL_BINDINGS; BRANCH_NOT_MEASURED");
    Field("NORMAL_MAPPING",id,"actual_parallax",v.native?"UNSUPPORTED_BY_CURRENT_R4_DIFFUSE_PIPELINE":"NOT_PROVEN_BY_SAMPLER_DECLARATION; SEE_VARIANT");
    Field("NORMAL_MAPPING",id,"detail_uv_scale","NOT_INSTRUMENTED_IN_SOURCE_DESCRIPTOR; SOURCE_AND_ACTUAL_SHADER_EVIDENCE_SEPARATE");
    Field("NORMAL_MAPPING",id,"normal_texture_sampler",v.native?"SEE_ACTUAL_NATIVE_MATERIAL_BINDINGS":"SOURCE_SAMPLER_DECLARATIONS_AND_ACTUAL_D3D9_BINDINGS_RECORDED_SEPARATELY");
    Field("NORMAL_MAPPING",id,"final_gbuffer_normal","NOT_READ_BACK");Field("NORMAL_MAPPING",id,"texture_branch_contribution","NOT_MEASURED");
    Field("NORMAL_MAPPING",id,"decode_equation",v.native?"SEE_NATIVE_MATERIAL_NORMAL_PATH":"NOT_DERIVED_FROM_NAME; TOP_SOURCE_AND_BYTECODE_HASH_IDENTIFY_VARIANT");
    RenderSnapshot_U64("MATERIAL_REGISTRY","source_entries_retained",r.sources.size());RenderSnapshot_U64("MATERIAL_REGISTRY","pass_entries_retained",r.passes.size());RenderSnapshot_U64("MATERIAL_REGISTRY","variant_entries_retained",r.variants.size());
    RenderSnapshot_U64("MATERIAL_REGISTRY","source_entries_omitted",r.dropped_sources);RenderSnapshot_U64("MATERIAL_REGISTRY","pass_entries_omitted",r.dropped_passes);RenderSnapshot_U64("MATERIAL_REGISTRY","variant_entries_omitted",r.dropped_variants);
    return id;
}
}
#endif
