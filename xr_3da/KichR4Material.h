#ifndef KICH_R4_MATERIAL_H
#define KICH_R4_MATERIAL_H
#include <stdint.h>
#include "KichR4SurfaceState.h"
#include <string>
#include <vector>
#include <map>

// Native metadata is built at source registration, before submission. No texture
// name heuristic or DDS-alpha inspection is allowed to change the surface route.
namespace kich_r4_material {
enum Semantic { UNKNOWN, MATERIAL_OPAQUE, CUTOUT, BLENDED, ADDITIVE, DISTORTION, EMISSIVE };
enum Special { SPECIAL_NONE, SPECIAL_TERRAIN, SPECIAL_WATER };
enum Family { FAMILY_UNKNOWN, FAMILY_OPAQUE, FAMILY_AREF, FAMILY_MODEL, FAMILY_TREE, FAMILY_DETAIL, FAMILY_SPECIAL };
enum Attribute { ATTR_NORMAL=1, ATTR_TANGENT=2, ATTR_BINORMAL=4, ATTR_UV1=8,
    ATTR_HEMI=16, ATTR_SUN=32, ATTR_COLOR=64, ATTR_TREE=128 };
enum Pass { PASS_GEOMETRY=1, PASS_SHADOW=2, PASS_FORWARD=4, PASS_DISTORTION=8 };
struct Descriptor {
    KichR4SurfaceState surface=KichR4DefaultSurface();
    Semantic semantic;
    Special special;
    Family source_family;
    std::string shader,source,base,bump,bump_aux,detail,lightmap,hemi,unsupported_reason;
    uint64_t class_id;
    float detail_scale[2],material,alpha_ref;
    uint32_t alpha_func,cull,src_blend,dst_blend,required_attributes,passes,detail_usage,source_flags,source_alpha_reference;
    uint32_t base_uv,bump_uv,detail_uv,lightmap_uv,hemi_uv;
    bool depth_test,depth_write,tree_wave;
    Descriptor():semantic(UNKNOWN),special(SPECIAL_NONE),source_family(FAMILY_UNKNOWN),class_id(UINT64_MAX),material(1),alpha_ref(0),alpha_func(8),cull(0),
        src_blend(2),dst_blend(1),required_attributes(ATTR_NORMAL),passes(0),detail_usage(0),source_flags(0),source_alpha_reference(0),base_uv(0),bump_uv(0),detail_uv(0),lightmap_uv(1),hemi_uv(1),
        depth_test(true),depth_write(true),tree_wave(false){detail_scale[0]=detail_scale[1]=1;}
};
struct Source {
    Descriptor descriptor;
    Family family;
    bool blend,strict_sorting,not_tree;
    uint32_t alpha_reference;
    Source():family(FAMILY_UNKNOWN),blend(false),strict_sorting(false),not_tree(false),alpha_reference(0){}
};
struct PassState {
    KichR4SurfaceState surface=KichR4DefaultSurface();
    bool blend,alpha_test,depth_test,depth_write,distortion,emissive,wallmark;
    uint32_t src,dst,alpha_func,alpha_reference,cull;
    PassState():blend(false),alpha_test(false),depth_test(true),depth_write(true),distortion(false),emissive(false),wallmark(false),
        src(2),dst(1),alpha_func(8),alpha_reference(0),cull(0){}
};
inline bool Geometry(const Descriptor& d){return (d.passes&PASS_GEOMETRY)!=0;}
inline bool Shadow(const Descriptor& d){return (d.passes&PASS_SHADOW)!=0;}
inline bool Bumped(const Descriptor& d){return Geometry(d) && !d.bump.empty();}
inline const char* Name(Semantic s){switch(s){case MATERIAL_OPAQUE:return "opaque";case CUTOUT:return "cutout";
    case BLENDED:return "blended";case ADDITIVE:return "additive";case DISTORTION:return "distortion";
    case EMISSIVE:return "emissive";default:return "unknown";}}
inline uint64_t Key(const Descriptor& d){
    uint64_t key=UINT64_C(14695981039346656037);
    const std::string* strings[]={&d.shader,&d.source,&d.base,&d.bump,&d.bump_aux,&d.detail,&d.lightmap,&d.hemi,&d.unsupported_reason};
    for(unsigned s=0;s<sizeof(strings)/sizeof(strings[0]);++s){for(size_t i=0;i<strings[s]->size();++i){key^=static_cast<unsigned char>((*strings[s])[i]);key*=UINT64_C(1099511628211);}key^=255;key*=UINT64_C(1099511628211);}
    const uint64_t integers[]={uint64_t(d.semantic),uint64_t(d.special),uint64_t(d.source_family),d.class_id,d.alpha_func,d.cull,d.src_blend,d.dst_blend,d.required_attributes,d.passes,d.detail_usage,d.source_flags,d.source_alpha_reference,
        d.base_uv,d.bump_uv,d.detail_uv,d.lightmap_uv,d.hemi_uv,
        uint64_t(d.depth_test),uint64_t(d.depth_write),uint64_t(d.tree_wave)};
    for(unsigned v=0;v<sizeof(integers)/sizeof(integers[0]);++v)for(unsigned b=0;b<8;++b){key^=(integers[v]>>(b*8))&255;key*=UINT64_C(1099511628211);}
    const float floats[]={d.detail_scale[0],d.detail_scale[1],d.material,d.alpha_ref};
    const unsigned char* bytes=reinterpret_cast<const unsigned char*>(floats);
    for(unsigned b=0;b<sizeof(floats);++b){key^=bytes[b];key*=UINT64_C(1099511628211);}
    bytes=reinterpret_cast<const unsigned char*>(&d.surface);
    for(unsigned b=0;b<sizeof(d.surface);++b){key^=bytes[b];key*=UINT64_C(1099511628211);}return key;
}
inline Descriptor Build(const Source& source,const PassState& state) {
    Descriptor d=source.descriptor;
    d.surface=state.surface;
    d.source_family=source.family;d.source_flags=(source.blend?1u:0u)|(source.strict_sorting?2u:0u)|(source.not_tree?4u:0u);
    d.source_alpha_reference=source.alpha_reference;
    d.alpha_ref=float(state.alpha_reference&255)/255.f;d.alpha_func=state.alpha_test?state.alpha_func:8;
    d.cull=state.cull;d.src_blend=state.src;d.dst_blend=state.dst;
    d.depth_test=state.depth_test;d.depth_write=state.depth_write;
    const bool true_blend=state.blend && !(state.src==2 && state.dst==1);
    // Special families and explicit pass flags retain their source behavior.
    if(state.distortion)d.semantic=DISTORTION;
    else if(state.wallmark || source.family==FAMILY_SPECIAL || source.family==FAMILY_DETAIL) {
        d.semantic=true_blend?(state.dst==2?ADDITIVE:BLENDED):state.emissive?EMISSIVE:UNKNOWN;
        if(d.semantic==UNKNOWN && d.unsupported_reason.empty())d.unsupported_reason="special_family_keeps_source_pass";
    } else if(source.family==FAMILY_MODEL && source.strict_sorting)d.semantic=BLENDED;
    else if(source.family==FAMILY_TREE) {
        // SoC xrRender/Blender_tree.cpp R2 branch: oBlend is uber_deffer aref,
        // not forward alpha blending. Object LOD changes deformation only.
        d.semantic=source.blend?CUTOUT:MATERIAL_OPAQUE;d.tree_wave=!source.not_tree;
        d.alpha_ref=200.f/255.f;d.alpha_func=source.blend?5:8;
    } else if(source.family==FAMILY_MODEL) {
        // SoC blender_deffer_model.cpp, including its genuine forward branch.
        d.semantic=source.blend?(source.alpha_reference<16?BLENDED:CUTOUT):MATERIAL_OPAQUE;
        d.alpha_ref=float(source.alpha_reference&255)/255.f;d.alpha_func=source.blend?5:8;
    } else if(source.family==FAMILY_AREF) {
        d.semantic=source.blend?BLENDED:CUTOUT;
        d.alpha_ref=float(source.alpha_reference&255)/255.f;d.alpha_func=5;
    } else if(source.family==FAMILY_OPAQUE)d.semantic=state.emissive?EMISSIVE:MATERIAL_OPAQUE;
    else {
        d.semantic=true_blend?(state.dst==2?ADDITIVE:BLENDED):state.emissive?EMISSIVE:UNKNOWN;
        d.unsupported_reason="unclassified_source_family_preserve_compiled_pass";
    }
    if(d.semantic==MATERIAL_OPAQUE || d.semantic==CUTOUT) {
        d.passes=PASS_GEOMETRY|PASS_SHADOW;d.depth_test=d.depth_write=true;
        d.src_blend=2;d.dst_blend=1;
        if(d.semantic==MATERIAL_OPAQUE){d.alpha_ref=0;d.alpha_func=8;}
        else {
            // resources/shaders/r2/common.h def_aref, used by flat and bumped
            // deffer_base_aref shaders. clip(alpha-ref) accepts equality.
            d.alpha_ref=200.f/255.f;d.alpha_func=7;
        }
    } else if(d.semantic==DISTORTION){d.passes=PASS_DISTORTION;d.depth_write=false;}
    else {d.passes=PASS_FORWARD;if(d.semantic==BLENDED || d.semantic==ADDITIVE)d.depth_write=false;}
    if(d.semantic==BLENDED && (source.family==FAMILY_MODEL || source.family==FAMILY_AREF)){
        d.src_blend=5;d.dst_blend=6; // The explicit SoC R2 forward branch.
    }
    if(Bumped(d))d.required_attributes|=ATTR_NORMAL|ATTR_TANGENT|ATTR_BINORMAL;
    if(!d.hemi.empty() || !d.lightmap.empty())d.required_attributes|=ATTR_UV1;
    return d;
}
// Preserve the exact alpha comparison and UV0 between geometry and shadows.
template<class Style> inline void Apply(const Descriptor& d,Style& s) {
    if(d.semantic==UNKNOWN)return;
    s.blend=d.semantic==BLENDED || d.semantic==ADDITIVE || d.semantic==DISTORTION;
    s.src=d.src_blend;s.dst=d.dst_blend;s.alpha_func=d.alpha_func;s.alpha_ref=d.alpha_ref;
    s.depth=d.depth_test;s.write=d.depth_write;s.cull=d.cull;s.surface=d.surface;
}
class Cache {
    std::map<const void*,std::vector<Descriptor> > entries;
    std::map<const void*,bool> ambiguous;
    static bool Equivalent(Descriptor a,Descriptor b){
        // A deduplicated compiled element may legitimately have several source
        // names; only differences that change native execution are conflicts.
        a.shader.clear();b.shader.clear();a.source.clear();b.source.clear();a.unsupported_reason.clear();b.unsupported_reason.clear();
        a.class_id=b.class_id=0;return Key(a)==Key(b);
    }
public:
    uint64_t registrations,lookups,misses,conflicts;
    Cache():registrations(0),lookups(0),misses(0),conflicts(0){}
    void Register(const void* element,const std::vector<Descriptor>& passes){
        if(!element)return;
        ++registrations;if(ambiguous.find(element)!=ambiguous.end())return;
        const auto found=entries.find(element);if(found==entries.end()){entries[element]=passes;return;}
        bool same=found->second.size()==passes.size();
        for(size_t i=0;same && i<passes.size();++i)same=Equivalent(found->second[i],passes[i]);
        if(same)return;
        ++conflicts;ambiguous[element]=true;
        for(size_t i=0;i<found->second.size();++i){Descriptor& d=found->second[i];d.semantic=UNKNOWN;d.passes=PASS_FORWARD;
            d.unsupported_reason="shared_compiled_element_conflicting_native_metadata;source_pass_preserved";}
    }
    const Descriptor* Find(const void* element,uint32_t pass) {
        ++lookups;const auto i=entries.find(element);if(i==entries.end() || pass>=i->second.size()){++misses;return NULL;}
        return &i->second[pass];
    }
    void Forget(const void* element){entries.erase(element);ambiguous.erase(element);}
};
}
#endif
