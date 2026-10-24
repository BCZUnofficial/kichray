#ifndef KICH_R4_NORMAL_MAP_PROBE_H
#define KICH_R4_NORMAL_MAP_PROBE_H
#include "KichR4RecordControl.h"
#include <stddef.h>
#include <string.h>

// Main render thread, successful opaque/HUD submissions only. This deliberately
// measures neither visible pixels nor unique materials. No queries or resource
// lookups: callers supply the material contract and actual bound shader path.
namespace kich_r4_normal_probe {
enum Category { Static, Model, HUD, Terrain, CategoryCount };
enum Reason { Mapped, NoAuthoredBump, PathDisabled, UnknownMaterial,
    MissingMetadata, MissingBasis, DependencyUnavailable, MapsUnavailable, ReasonCount };
inline const char* Name(Category category){
    static const char* const names[]={"STATIC","MODEL","HUD","TERRAIN"};
    return unsigned(category)<CategoryCount?names[category]:"UNKNOWN";
}
inline const char* Name(Reason reason){
    static const char* const names[]={"MAPPED","NO_AUTHORED_BUMP","PATH_DISABLED","UNKNOWN_MATERIAL",
        "BUMP_METADATA_INCOMPLETE","SOURCE_BASIS_INVALID","TERRAIN_MASK_OR_DIFFUSE_UNAVAILABLE","MAPS_UNAVAILABLE"};
    return unsigned(reason)<ReasonCount?names[reason]:"UNKNOWN";
}
struct Input {
    bool known,authored,basis,metadata,mapped,path_enabled,dependency;
    uint32_t attributes,flags,index_count;
    uint64_t identity;
    const char *resource,*base,*bump,*auxiliary;
    Input():known(false),authored(false),basis(false),metadata(false),mapped(false),path_enabled(true),dependency(true),
        attributes(0),flags(0),index_count(0),identity(0),resource(NULL),base(NULL),bump(NULL),auxiliary(NULL){}
};
inline Reason Classify(const Input& input){
    if(!input.known)return UnknownMaterial;
    if(!input.path_enabled)return PathDisabled;
    if(input.mapped)return Mapped;
    if(!input.authored)return NoAuthoredBump;
    if(!input.dependency)return DependencyUnavailable;
    if(!input.basis)return MissingBasis;
    if(!input.metadata)return MissingMetadata;
    return MapsUnavailable;
}
struct Example {
    bool valid;Reason reason;uint32_t attributes,flags,index_count;uint64_t identity;
    char resource[96],base[96],bump[96],auxiliary[96];bool truncated;
    Example():valid(false),reason(Mapped),attributes(0),flags(0),index_count(0),identity(0),truncated(false){resource[0]=base[0]=bump[0]=auxiliary[0]=0;}
};
struct Counts {
    uint64_t draws,indices,authored,mapped,mapped_indices,reasons[ReasonCount];
    Example example;
    Counts():draws(0),indices(0),authored(0),mapped(0),mapped_indices(0){memset(reasons,0,sizeof(reasons));}
};
inline bool Copy(char* target,size_t capacity,const char* source){
    if(!source){target[0]=0;return false;}
    size_t i=0;for(;i+1<capacity && source[i];++i){const unsigned char c=static_cast<unsigned char>(source[i]);
        // Keep each sample one quoted log line. Never interpret source as format.
        target[i]=c<32 || c==127 || c=='"'?'_':static_cast<char>(c);}
    target[i]=0;return source[i]!=0;
}
struct Frame {
    uint64_t epoch;uint32_t frame;bool active;Counts categories[CategoryCount];
    Frame():epoch(0),frame(0),active(false){}
    void BeginFrame(uint32_t number){
        if(!kich_r4_record::Enabled())return;
        epoch=kich_r4_record::Epoch();frame=number;active=true;
        for(unsigned i=0;i<CategoryCount;++i)categories[i]=Counts();
    }
    bool Current(uint32_t number)const{return kich_r4_record::Enabled() && active && epoch==kich_r4_record::Epoch() && frame==number;}
    void Observe(Category category,const Input& input){
        if(!Current(kich_r4_record::Switch().frame) || unsigned(category)>=CategoryCount)return;
        Counts& count=categories[category];const Reason reason=Classify(input);
        ++count.draws;count.indices+=input.index_count;
        if(input.known && input.authored)++count.authored;
        if(input.mapped && input.path_enabled){++count.mapped;count.mapped_indices+=input.index_count;}
        ++count.reasons[reason];
        // One bounded example per class, prefer an actionable disabled feature
        // over ordinary flat materials. Same-priority later draws do not scan or
        // copy strings. Four examples total, independent of draw/material count.
        Example& e=count.example;if(e.valid && e.reason>=reason)return;
        e.valid=true;e.reason=reason;e.attributes=input.attributes;e.flags=input.flags;e.index_count=input.index_count;e.identity=input.identity;
        e.truncated=Copy(e.resource,sizeof(e.resource),input.resource);
        e.truncated=Copy(e.base,sizeof(e.base),input.base) || e.truncated;
        e.truncated=Copy(e.bump,sizeof(e.bump),input.bump) || e.truncated;
        e.truncated=Copy(e.auxiliary,sizeof(e.auxiliary),input.auxiliary) || e.truncated;
    }
};
inline Frame& Global(){static Frame value;return value;}
template<class Recorder> inline void EmitReport(Recorder& record,uint32_t number,bool world_current){
    if(!kich_r4_record::Enabled())return;
    const Frame& frame=Global();
    if(!world_current || !frame.Current(number)){
        record.Emit("[R4 E1.9 normal_maps] frame=%u current_world=0; no_current_submission_samples",number);return;}
    for(unsigned i=0;i<CategoryCount;++i){const Counts& c=frame.categories[i];
        record.Emit("[R4 E1.9 normal_maps] frame=%u class=%s draws=%llu indices=%llu authored_bump=%llu mapped=%llu mapped_indices=%llu no_authored_bump=%llu unknown_material=%llu invalid_basis=%llu incomplete_metadata=%llu maps_unavailable=%llu terrain_dependencies_unavailable=%llu path_disabled=%llu; current_frame_successful_submissions_not_pixels; split_subsets_are_separate_draws",
            number,Name(Category(i)),static_cast<unsigned long long>(c.draws),static_cast<unsigned long long>(c.indices),
            static_cast<unsigned long long>(c.authored),static_cast<unsigned long long>(c.mapped),static_cast<unsigned long long>(c.mapped_indices),
            static_cast<unsigned long long>(c.reasons[NoAuthoredBump]),static_cast<unsigned long long>(c.reasons[UnknownMaterial]),
            static_cast<unsigned long long>(c.reasons[MissingBasis]),static_cast<unsigned long long>(c.reasons[MissingMetadata]),
            static_cast<unsigned long long>(c.reasons[MapsUnavailable]),static_cast<unsigned long long>(c.reasons[DependencyUnavailable]),static_cast<unsigned long long>(c.reasons[PathDisabled]));
        const Example& e=c.example;if(!e.valid)continue;
        record.Emit("[R4 E1.9 normal_example] frame=%u class=%s reason=%s identity=%llu attributes=0x%X shader_flags=0x%X indices=%u resource=\"%s\" base=\"%s\" bump=\"%s\" auxiliary=\"%s\" truncated=%u; one_priority_example_per_class_not_complete_material_inventory; identity_scoped_to_current_frame",
            number,Name(Category(i)),Name(e.reason),static_cast<unsigned long long>(e.identity),e.attributes,e.flags,e.index_count,
            e.resource,e.base,e.bump,e.auxiliary,unsigned(e.truncated));
    }
}
}
#endif
