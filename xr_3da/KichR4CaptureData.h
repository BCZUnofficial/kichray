#ifndef KICH_R4_CAPTURE_DATA_H
#define KICH_R4_CAPTURE_DATA_H
#include "KichR4LevelData.h"
#include "KichR4Stream.h"
#include "KichR4CaptureInput.h"
#include "KichR4LightingMath.h"
#include "KichR4Visibility.h"
#include "KichR4LightAdmission.h"
#include "KichR4LocalShadowPlan.h"
#include "RenderSnapshot.h"
#include "KichR4TelemetryText.h"
#include "KichR4Material.h"
#include <memory>
#include <set>
#include <map>
#include <unordered_map>
#include <stdio.h>
namespace kich_r4_capture {
using kich_r4_level::Span;using kich_r4_level::Reader;using kich_r4_level::Check;
using kich_r4_level::Chunk;using kich_r4_level::FirstChunk;using kich_r4_level::Finite;using kich_r4_scene::Matrix;
struct Vertex {float p[4],uv[2];uint32_t color;float normal[4],world[3];};
static_assert(sizeof(Vertex)==56,"B4.4 native vertex stride");
struct SourceVertex {
    float p[3],uv[2],weight;uint16_t bone[2];uint32_t normal,tangent,binormal;
    float uv1[2],hemi,sun;uint32_t color,attributes;
};
static_assert(sizeof(SourceVertex)==64,"G3c source model GPU vertex");
/* G3c retains twice the vertex payload (32 -> 64 bytes). Preserve the model
 * population accepted by G3b's 96 MiB registry without dropping TBN/UV1 data:
 * 32V + 2I <= 96 MiB implies 64V + 2I <= 192 MiB. A single mesh can gain at
 * most 65536 * 32 bytes over its former 8 MiB limit. Both caps count payload.
 * P0-S10: modded Cordon preloads 201 MB of models at start; past 192 MiB new
 * NPC bodies were rejected and drawn as weapons only (tungus_1/2/4). The EXE
 * is large-address-aware (4 GB on x64), so the registry holds 384 MiB. */
static const uint64_t ModelRegistryBudgetBytes=384u*1024u*1024u;
static const uint64_t ModelMeshBudgetBytes=10u*1024u*1024u;
struct Bounds {
    float min[3],max[3];bool valid;
    Bounds():valid(false){for(unsigned i=0;i<3;++i)min[i]=max[i]=0;}
    void Add(const float* p){if(!valid){for(unsigned i=0;i<3;++i)min[i]=max[i]=p[i];valid=true;return;}
        for(unsigned i=0;i<3;++i){min[i]=std::min(min[i],p[i]);max[i]=std::max(max[i],p[i]);}}
};
inline uint32_t PackNormal(float x,float y,float z){
    float values[]={x,y,z};if(!kich_r4_level::UnitVector(values))return 0;
    uint32_t bits=0;
    for(unsigned i=0;i<3;++i)bits|=static_cast<uint32_t>(std::max(0.f,std::min(1023.f,(values[i]+1)*511.5f+.5f)))<<(i*10);
    return bits;
}
inline kich_r4_scene::Vec3 UnpackNormal(uint32_t bits){if(!bits)return kich_r4_scene::Vec3{0,0,0};
    return kich_r4_scene::Normalize(kich_r4_scene::Vec3{(bits&1023)*(2.f/1023)-1,((bits>>10)&1023)*(2.f/1023)-1,((bits>>20)&1023)*(2.f/1023)-1});}
struct Mesh {
    std::vector<SourceVertex> vertices;std::vector<uint16_t> indices;std::string texture,shader,texture_list;
    Bounds source_bounds;std::vector<Bounds> bone_bounds;
    uint32_t bones,ignored_weights,outside_weights,diagnostic_modes,available_attributes,basis_vertices,derived_basis_vertices,invalid_uvs;bool skinned,two_weights,hardware_weights_safe;
    std::string first_weight_detail;
    mutable uint64_t snapshot_hash;mutable bool snapshot_hashed;
    struct RangeInfo {uint32_t first,count,maximum,attributes,basis_indices;};
private:
    // Only registry-owned decoded geometry is frozen. Procedural/test meshes
    // remain mutable and are checked on every use. A new Mesh owns a new cache;
    // reusing a visual address never transfers range validation to its replacement.
    bool ranges_frozen=false;
    const SourceVertex* frozen_vertices=NULL;const uint16_t* frozen_indices=NULL;
    size_t frozen_vertex_count=0,frozen_index_count=0;
    mutable std::vector<RangeInfo> range_cache;
    mutable size_t range_next=0;
    mutable uint64_t range_scanned_indices=0,range_cache_hits=0;
public:
    Mesh():bones(0),ignored_weights(0),outside_weights(0),diagnostic_modes(0),available_attributes(0),basis_vertices(0),derived_basis_vertices(0),invalid_uvs(0),skinned(false),two_weights(false),hardware_weights_safe(true),snapshot_hash(0),snapshot_hashed(false){}
    uint64_t Bytes()const{return uint64_t(vertices.size())*sizeof(SourceVertex)+uint64_t(indices.size())*2;}
    void InvalidateRanges(){ranges_frozen=false;range_cache.clear();range_next=0;snapshot_hashed=false;}
    void FreezeRanges(){InvalidateRanges();frozen_vertices=vertices.data();frozen_indices=indices.data();
        frozen_vertex_count=vertices.size();frozen_index_count=indices.size();ranges_frozen=true;}
    uint64_t RangeScannedIndices()const{return range_scanned_indices;}
    uint64_t RangeCacheHits()const{return range_cache_hits;}
    size_t CachedRanges()const{return range_cache.size();}
    bool Range(uint32_t first,uint32_t count,uint32_t vertex_prefix,RangeInfo& result)const {
        if(!vertex_prefix || vertex_prefix>vertices.size() || !count || count%3 || first>indices.size() || count>indices.size()-first)return false;
        const bool frozen=ranges_frozen && frozen_vertices==vertices.data() && frozen_indices==indices.data() &&
            frozen_vertex_count==vertices.size() && frozen_index_count==indices.size();
        if(frozen)for(size_t i=0;i<range_cache.size();++i)if(range_cache[i].first==first && range_cache[i].count==count){
            result=range_cache[i];++range_cache_hits;return result.maximum<vertex_prefix;}
        result={first,count,0,~0u,0};
        const uint32_t basis=kich_r4_level::ATTR_NORMAL|kich_r4_level::ATTR_TANGENT|kich_r4_level::ATTR_BINORMAL;
        range_scanned_indices+=count;
        for(uint32_t i=0;i<count;++i){const uint32_t index=indices[first+i];if(index>=vertices.size())return false;
            result.maximum=(std::max)(result.maximum,index);result.attributes&=vertices[index].attributes;
            if((vertices[index].attributes&basis)==basis)++result.basis_indices;}
        // Cache validity of the immutable index range, never validity of one
        // advertised LOD prefix. A smaller prefix is still checked on a hit.
        if(frozen){if(range_cache.size()<64)range_cache.push_back(result);
            else{range_cache[range_next]=result;range_next=(range_next+1)%64;}}
        return result.maximum<vertex_prefix;
    }
};
inline std::string WeightDetail(uint32_t vertex,uint32_t b0,uint32_t b1,uint32_t bits,const char* why){
    char text[224];float w;memcpy(&w,&bits,4);
    if((bits&0x7f800000u)==0x7f800000u)snprintf(text,sizeof(text),"%s: vertex=%u bones=%u/%u weight=nonfinite bits=0x%08X",why,vertex,b0,b1,bits);
    else snprintf(text,sizeof(text),"%s: vertex=%u bones=%u/%u weight=%.9g bits=0x%08X",why,vertex,b0,b1,static_cast<double>(w),bits);
    return text;
}
inline float SkinWeight(float w,uint32_t mode){
    Check(Finite(w),"nonfinite active 2W skin weight");
    if(mode==KICH_R4_SKIN_SOFTWARE)return w;
    Check(mode==KICH_R4_SKIN_HARDWARE,"2W skin mode handshake missing; rebuild R1/R2");
    /* Match vertHW_2W::set, but reject values beyond its safe signed iFloor
     * range rather than invoking an overflowing float-to-int conversion. */
    Check(w>=-8388607.f && w<=8388607.f,"2W hardware weight exceeds safe quantization range");
    if(w<=0){return 0;}if(w>=1){return 1;}
    return floorf(w*255.f+.5f)/255.f;
}
/* Decode original OGF data, before the DX9 renderer quantizes it. Containers
 * belong to the static level and are already drawn by B4.2. */
inline std::shared_ptr<Mesh> Decode(Span source) {
    Reader header(FirstChunk(source,1));Check(header.Left()>=44,"model OGF header size");const unsigned char* first=header.Take(2);
    Check(first[0]==4,"model OGF version");const unsigned type=first[1];
    if(type!=0 && type!=2 && type!=4 && type!=5)return std::shared_ptr<Mesh>();
    if(FirstChunk(source,21,false).p || FirstChunk(source,7,false).p)return std::shared_ptr<Mesh>();
    std::shared_ptr<Mesh> mesh(new Mesh());mesh->skinned=type==4 || type==5;
    Reader texture(FirstChunk(source,2));mesh->texture=kich_r4_level::Lower(texture.Z());mesh->texture_list=mesh->texture;
    if(texture.Left())mesh->shader=texture.Z();
    const size_t comma=mesh->texture.find(',');if(comma!=std::string::npos)mesh->texture.resize(comma);
    if(mesh->texture.size()>4 && mesh->texture.substr(mesh->texture.size()-4)==".dds")mesh->texture.resize(mesh->texture.size()-4);
    Check(mesh->texture.empty() || (kich_r4_level::SafeName(mesh->texture) && mesh->texture[0]!='$'),"model texture name");
    Reader vertices(FirstChunk(source,3));const uint32_t format=vertices.U32(),count=vertices.U32();
    Check(count && count<=65536,"model vertex limit");uint32_t stride=0,uv=0,uv1=0,pos=0,color=0;
    if(mesh->skinned){Check(format==0x12071980u || format==0x240e3300u,"model bone format");
        stride=format==0x12071980u?60:64;pos=stride==64?4:0;uv=stride==64?56:48;mesh->two_weights=stride==64;
    }else {
        /* SoC inline FVF: XYZ, optional normal/point size/color, float UVs. */
        Check((format&0x400eu)==2 && !(format&0x8000u),"model position FVF");stride=12;
        if(format&0x10u){stride+=12;}if(format&0x20u){stride+=4;}
        if(format&0x40u){color=stride;stride+=4;}if(format&0x80u){stride+=4;}
        uv=stride;const uint32_t sets=(format>>8)&15;Check(sets && sets<=8,"model UV sets");
        for(uint32_t s=0;s<sets;++s){const uint32_t dims=(format>>(16+s*2))&3;
            Check(!s?dims==0:true,"model base UV format");if(s==1 && dims==0)uv1=stride;stride+=dims==0?8:dims==1?12:dims==2?16:4;}
    }
    Check(count<=vertices.Left()/stride,"truncated model vertices");mesh->vertices.resize(count);bool missing_basis=false;
    for(uint32_t i=0;i<count;++i){const unsigned char* p=vertices.Take(stride);SourceVertex& v=mesh->vertices[i];v={};v.hemi=v.sun=1;v.color=0xffffffffu;
        Reader xyz(Span(p+pos,12));for(unsigned a=0;a<3;++a)v.p[a]=xyz.Float();
        /* Modded OGFs ship NaN UVs (wpn_ak74: 42 vertices); DX9 samples them
         * as garbage texels without failing. Zero them, keep the model. */
        memcpy(v.uv,p+uv,8);if(!Finite(v.uv[0]) || !Finite(v.uv[1])){v.uv[0]=v.uv[1]=0;++mesh->invalid_uvs;}
        if(mesh->skinned || (format&0x10u)){float normal[3];memcpy(normal,p+(mesh->skinned?pos+12:12),12);
            v.normal=PackNormal(normal[0],normal[1],normal[2]);if(v.normal)v.attributes|=kich_r4_level::ATTR_NORMAL;
            if(mesh->skinned){float tangent[3],binormal[3];memcpy(tangent,p+pos+24,12);memcpy(binormal,p+pos+36,12);
                if(kich_r4_level::Basis(normal,tangent,binormal)){
                    v.tangent=PackNormal(tangent[0],tangent[1],tangent[2]);v.binormal=PackNormal(binormal[0],binormal[1],binormal[2]);
                    v.attributes|=kich_r4_level::ATTR_TANGENT|kich_r4_level::ATTR_BINORMAL;}}}
        if(uv1){memcpy(v.uv1,p+uv1,8);if(!Finite(v.uv1[0]) || !Finite(v.uv1[1])){v.uv1[0]=v.uv1[1]=0;++mesh->invalid_uvs;}v.attributes|=kich_r4_level::ATTR_UV1;}
        if(color){const unsigned char* c=p+color;v.color=uint32_t(c[2])|(uint32_t(c[1])<<8)|(uint32_t(c[0])<<16)|(uint32_t(c[3])<<24);v.attributes|=kich_r4_level::ATTR_COLOR;}
        if(mesh->skinned){if(stride==60){Reader b(Span(p+56,4));const uint32_t id=b.U32();Check(id<256,"1W model bone limit");v.bone[0]=v.bone[1]=static_cast<uint16_t>(id);}
            else {Reader b(Span(p,4)),w(Span(p+52,4));v.bone[0]=b.U16();v.bone[1]=b.U16();const uint32_t bits=w.U32();memcpy(&v.weight,&bits,4);
                if(v.bone[0]>=256 || v.bone[1]>=256)Check(false,WeightDetail(i,v.bone[0],v.bone[1],bits,"2W bone index outside 0..255").c_str());
                const bool ieee_finite=(bits&0x7f800000u)!=0x7f800000u;
                if(v.bone[0]==v.bone[1]){
                    /* xrSkin2W_x86 ignores w entirely for an identical pair.
                     * In hardware both matrix results are equal as well. */
                    if(!ieee_finite || v.weight<0 || v.weight>1){++mesh->ignored_weights;
                        if(mesh->first_weight_detail.empty())mesh->first_weight_detail=WeightDetail(i,v.bone[0],v.bone[1],bits,"unused equal-bone weight");}
                    v.weight=0;
                }else {
                    if(!ieee_finite || !Finite(v.weight))Check(false,WeightDetail(i,v.bone[0],v.bone[1],bits,"invalid active 2W weight").c_str());
                    if(v.weight<-8388607.f || v.weight>8388607.f)mesh->hardware_weights_safe=false;
                    if(v.weight<0 || v.weight>1){++mesh->outside_weights;
                        if(mesh->first_weight_detail.empty())mesh->first_weight_detail=WeightDetail(i,v.bone[0],v.bone[1],bits,"active weight outside 0..1");}
                }}
            mesh->bones=std::max(mesh->bones,uint32_t(std::max(v.bone[0],v.bone[1]))+1);}
        if(!(v.attributes&kich_r4_level::ATTR_TANGENT))missing_basis=true;
    }
    Reader indices(FirstChunk(source,4));const uint32_t n=indices.U32();Check(n && n%3==0 && n<=3u*1024u*1024u && n<=indices.Left()/2,"model index limit");
    mesh->indices.resize(n);for(uint32_t i=0;i<n;++i){mesh->indices[i]=indices.U16();Check(mesh->indices[i]<count,"model index range");}
    /* Repair only missing T/B, once in the common model bind space. Skeletal
     * render matrices already include inverse bind transforms, so 1W/2W source
     * positions share this space even across different bone IDs. Preserve valid
     * authored bases; conflicting mirrored UVs and degenerate triangles retain
     * their explicit fallback rather than inventing a tangent orientation. */
    if(missing_basis){
        struct Tangents {float t[3],b[3];int sign;bool conflict;Tangents():sign(0),conflict(false){for(unsigned a=0;a<3;++a)t[a]=b[a]=0;}};
        std::vector<Tangents> basis(count);
        for(uint32_t i=0;i<n;i+=3){const uint16_t ids[]={mesh->indices[i],mesh->indices[i+1],mesh->indices[i+2]};
            const SourceVertex& a=mesh->vertices[ids[0]];const SourceVertex& b=mesh->vertices[ids[1]];const SourceVertex& c=mesh->vertices[ids[2]];
            const float du1=b.uv[0]-a.uv[0],dv1=b.uv[1]-a.uv[1],du2=c.uv[0]-a.uv[0],dv2=c.uv[1]-a.uv[1];
            const float determinant=du1*dv2-du2*dv1;if(!Finite(determinant) || fabsf(determinant)<1.e-10f)continue;
            float t[3],bn[3],area[3];const float e1[]={b.p[0]-a.p[0],b.p[1]-a.p[1],b.p[2]-a.p[2]},e2[]={c.p[0]-a.p[0],c.p[1]-a.p[1],c.p[2]-a.p[2]};
            area[0]=e1[1]*e2[2]-e1[2]*e2[1];area[1]=e1[2]*e2[0]-e1[0]*e2[2];area[2]=e1[0]*e2[1]-e1[1]*e2[0];if(!kich_r4_level::UnitVector(area))continue;
            for(unsigned axis=0;axis<3;++axis){t[axis]=(e1[axis]*dv2-e2[axis]*dv1)/determinant;bn[axis]=(e2[axis]*du1-e1[axis]*du2)/determinant;}
            for(unsigned corner=0;corner<3;++corner){Tangents& target=basis[ids[corner]];
                const kich_r4_scene::Vec3 normal=UnpackNormal(mesh->vertices[ids[corner]].normal);
                const float orientation=(normal.y*t[2]-normal.z*t[1])*bn[0]+(normal.z*t[0]-normal.x*t[2])*bn[1]+(normal.x*t[1]-normal.y*t[0])*bn[2];
                if(!Finite(orientation) || fabsf(orientation)<1.e-10f)continue;
                const int sign=orientation<0?-1:1;
                if(target.sign && target.sign!=sign)target.conflict=true;
                target.sign=sign;
                for(unsigned axis=0;axis<3;++axis){target.t[axis]+=t[axis];target.b[axis]+=bn[axis];}}
        }
        for(uint32_t i=0;i<count;++i){SourceVertex& v=mesh->vertices[i];
            if(v.attributes&kich_r4_level::ATTR_TANGENT)continue;
            Tangents& t=basis[i];const kich_r4_scene::Vec3 unpacked=UnpackNormal(v.normal);float normal[]={unpacked.x,unpacked.y,unpacked.z};
            if(!t.conflict && t.sign && kich_r4_level::Basis(normal,t.t,t.b)){
                v.tangent=PackNormal(t.t[0],t.t[1],t.t[2]);v.binormal=PackNormal(t.b[0],t.b[1],t.b[2]);v.attributes|=kich_r4_level::ATTR_TANGENT|kich_r4_level::ATTR_BINORMAL;++mesh->derived_basis_vertices;}}
    }
    mesh->available_attributes=~0u;
    for(uint32_t i=0;i<count;++i){mesh->available_attributes&=mesh->vertices[i].attributes;if(mesh->vertices[i].attributes&kich_r4_level::ATTR_TANGENT)++mesh->basis_vertices;}
    if(mesh->skinned)mesh->bone_bounds.resize(mesh->bones);
    for(uint32_t i=0;i<count;++i){const SourceVertex& v=mesh->vertices[i];mesh->source_bounds.Add(v.p);
        if(mesh->skinned){mesh->bone_bounds[v.bone[0]].Add(v.p);mesh->bone_bounds[v.bone[1]].Add(v.p);}}
    Check(mesh->Bytes()<=ModelMeshBudgetBytes,"single model CPU budget");return mesh;
}
inline void Transform(const float* p,const Matrix& m,float* out) {
    for(unsigned c=0;c<4;++c)out[c]=p[0]*m.m[c]+p[1]*m.m[4+c]+p[2]*m.m[8+c]+m.m[12+c];
}
inline uint32_t RGBA(uint32_t argb){return (argb&0xff00ff00u)|((argb&0xffu)<<16)|((argb>>16)&0xffu);}
enum CaptureSemantic {CAPTURE_UNKNOWN=0,CAPTURE_UI,CAPTURE_PARTICLE,CAPTURE_STATIC_WALLMARK,CAPTURE_SKELETAL_WALLMARK,CAPTURE_SKY,CAPTURE_CLOUDS,CAPTURE_GLOW,CAPTURE_MODEL,CAPTURE_SEMANTIC_COUNT};
inline const char* CaptureSemanticName(unsigned value){const char* names[]={"UNKNOWN","UI","PARTICLE","STATIC_WALLMARK","SKELETAL_WALLMARK","SKY","CLOUDS","GLOW","LEGACY_MODEL"};return value<CAPTURE_SEMANTIC_COUNT?names[value]:names[0];}
struct Command {uint32_t first,count,layer;std::string texture;Style style;uint32_t effect,shader_element,shader_pass;std::string secondary,resource,shader,skin;
    /* Allocated only for a requested diagnostic frame; never crosses the DLL ABI. */
    std::shared_ptr<RenderSnapshotProvenance> provenance;
    uint32_t semantic,captured_vertices;bool console;uint32_t wallmark_color=0;
    Command(uint32_t f,uint32_t n,uint32_t l,const std::string& t,const Style& s):first(f),count(n),layer(l),texture(t),style(s),effect(0),shader_element(UINT32_MAX),shader_pass(0),semantic(CAPTURE_UNKNOWN),captured_vertices(0),console(false){}
};
/* Immutable shared geometry is owned separately from each animated instance.
 * An instance borrows no engine pointer after submission. Only its palette,
 * transforms and material/range descriptor change between frames. */
struct NativeModelCommand {
    std::shared_ptr<const Mesh> mesh;Matrix world,vp;std::vector<Matrix> bones;
    uint32_t vertex_count,first,count,layer,skin_mode,effect,shader_element,shader_pass;Style style;
    std::string resource,shader,texture,secondary;
    kich_r4_material::Descriptor material;bool has_material;
    uint32_t available_attributes=0;bool object_light_valid=false;float object_hemi=1,object_sun=1;
    std::shared_ptr<RenderSnapshotProvenance> provenance;
    Bounds bounds;bool bounds_known;
    uint64_t visual_id,skeleton_id,pose_hash,lifecycle_serial;uint32_t source_frame;bool identity_measured,pose_hash_valid;
    // P0-S11 PERF02 deduplication identity (Batch::NativeModel): the style as submitted.
    Style source_style;
    NativeModelCommand():vertex_count(0),first(0),count(0),layer(0),skin_mode(KICH_R4_SKIN_UNKNOWN),effect(0),shader_element(UINT32_MAX),shader_pass(UINT32_MAX),has_material(false),bounds_known(false),visual_id(0),skeleton_id(0),pose_hash(0),lifecycle_serial(0),source_frame(0),identity_measured(false),pose_hash_valid(false){}
};
// One branch per matrix instead of sixteen.
inline void ValidateModelMatrix(const Matrix& matrix){bool finite=true;for(unsigned i=0;i<16;++i)finite&=Finite(matrix.m[i]);Check(finite,"nonfinite native model matrix");}
// P0-S11 PERF02: the image of a box under an affine map is bounded by the
// image of its centre and the half-extent through |M| (Arvo 1990): two
// transforms instead of eight corners, the same box in exact arithmetic. The
// pad, ~16 float ulps of the summed term magnitudes (the bound on the rounding
// of a four-term sum, cancellation included), keeps it conservative.
inline bool AddTransformedBounds(Bounds& out,const Bounds& source,const Matrix& matrix){
    if(!source.valid)return true;
    const float* m=matrix.m;
    if(m[3]!=0 || m[7]!=0 || m[11]!=0 || m[15]!=1)return false;
    float c[3],e[3],low[3],high[3];
    for(unsigned i=0;i<3;++i){c[i]=(source.min[i]+source.max[i])*.5f;e[i]=fabsf(source.max[i]-source.min[i])*.5f;}
    for(unsigned j=0;j<3;++j){const float center=c[0]*m[j]+c[1]*m[4+j]+c[2]*m[8+j]+m[12+j];
        const float extent=e[0]*fabsf(m[j])+e[1]*fabsf(m[4+j])+e[2]*fabsf(m[8+j]);
        const float pad=(fabsf(c[0]*m[j])+fabsf(c[1]*m[4+j])+fabsf(c[2]*m[8+j])+fabsf(m[12+j])+extent)*1.e-6f;
        low[j]=center-extent-pad;high[j]=center+extent+pad;
        if(!Finite(low[j]) || !Finite(high[j]) || !(extent>=0))return false;}
    out.Add(low);out.Add(high);return true;
}
inline bool NativeModelBounds(NativeModelCommand& cmd){
    const Mesh& mesh=*cmd.mesh;
    /* The convex union of transformed per-bone bounds contains 1W and bounded
     * 2W results. Raw software extrapolation has no such guarantee. */
    if(mesh.two_weights && mesh.outside_weights && cmd.skin_mode==KICH_R4_SKIN_SOFTWARE)return false;
    if(!mesh.skinned)return AddTransformedBounds(cmd.bounds,mesh.source_bounds,cmd.world) && cmd.bounds.valid;
    Bounds local;
    for(size_t i=0;i<mesh.bone_bounds.size();++i)if(!AddTransformedBounds(local,mesh.bone_bounds[i],cmd.bones[i]))return false;
    return AddTransformedBounds(cmd.bounds,local,cmd.world) && cmd.bounds.valid;
}
inline uint64_t SnapshotHash(uint64_t h,const void* bytes,size_t n){
    const unsigned char* p=static_cast<const unsigned char*>(bytes);
    for(size_t i=0;i<n;++i){h^=p[i];h*=UINT64_C(1099511628211);}return h;
}
inline uint64_t SnapshotStringHash(uint64_t h,const std::string& value){return SnapshotHash(h,value.c_str(),value.size()+1);}
inline void SnapshotModelID(const Mesh& mesh,char* out,size_t size,uint32_t first,uint32_t count){
    /* Minimal profiles must not scan immutable source geometry. */
    if(!RenderSnapshot_Detailed() && !mesh.snapshot_hashed){snprintf(out,size,"PROFILE_SOURCE_HANDLE:%llu:first=%u:indices=%u",static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(&mesh)),first,count);return;}
    /* Immutable decoded source geometry, not animated/skinned output or a process pointer. */
    if(!mesh.snapshot_hashed){uint64_t h=UINT64_C(14695981039346656037);
        for(size_t i=0;i<mesh.vertices.size();++i){const SourceVertex& v=mesh.vertices[i];
            h=SnapshotHash(h,v.p,sizeof(v.p));h=SnapshotHash(h,v.uv,sizeof(v.uv));h=SnapshotHash(h,&v.weight,sizeof(v.weight));
            h=SnapshotHash(h,v.bone,sizeof(v.bone));h=SnapshotHash(h,&v.normal,sizeof(v.normal));
            h=SnapshotHash(h,&v.tangent,sizeof(v.tangent));h=SnapshotHash(h,&v.binormal,sizeof(v.binormal));h=SnapshotHash(h,v.uv1,sizeof(v.uv1));
            h=SnapshotHash(h,&v.hemi,sizeof(v.hemi));h=SnapshotHash(h,&v.sun,sizeof(v.sun));h=SnapshotHash(h,&v.color,sizeof(v.color));h=SnapshotHash(h,&v.attributes,sizeof(v.attributes));}
        h=SnapshotHash(h,mesh.indices.data(),mesh.indices.size()*sizeof(uint16_t));mesh.snapshot_hash=h;mesh.snapshot_hashed=true;}
    snprintf(out,size,"OGF_DECODED_FNV64:%016llx:first=%u:indices=%u",static_cast<unsigned long long>(mesh.snapshot_hash),first,count);
}
inline void SnapshotCommand(Command& cmd,const std::vector<Vertex>& vertices,const std::vector<uint32_t>& indices,
    const char* fallback_semantic,const Mesh* mesh=NULL,uint32_t source_first=0,bool have_uv=true){
    if(!g_render_snapshot_active || !RenderSnapshot_Detailed())return;
    try{std::shared_ptr<RenderSnapshotProvenance> p(new RenderSnapshotProvenance());RenderSnapshot_GetProvenance(p.get());
        if(!p->semantic_type[0] || !strcmp(p->semantic_type,"UNKNOWN"))snprintf(p->semantic_type,sizeof(p->semantic_type),"%s",fallback_semantic);
        if(!p->evidence[0])snprintf(p->evidence,sizeof(p->evidence),"%s",mesh?"OGF_DECODED_SOURCE":"CPU_CAPTURE_SOURCE");
        if(mesh)SnapshotModelID(*mesh,p->mesh_id,sizeof(p->mesh_id),source_first,cmd.count);
        /* Descriptor identity includes the actual captured shader, textures and draw state.
         * It is deliberately named CAPTURE, not claimed to equal an R2 material ID. */
        if(!p->material_id[0] || !strcmp(p->material_id,"UNKNOWN")){uint64_t h=SnapshotStringHash(UINT64_C(14695981039346656037),cmd.shader);
            h=SnapshotStringHash(h,cmd.texture);h=SnapshotStringHash(h,cmd.secondary);
            const uint32_t state[]={cmd.effect,cmd.style.blend,cmd.style.src,cmd.style.dst,cmd.style.clamp,cmd.style.cull,cmd.style.depth,cmd.style.write,cmd.style.alpha_func,cmd.style.shadow_alpha_func};
            h=SnapshotHash(h,state,sizeof(state));h=SnapshotHash(h,&cmd.style.alpha_ref,sizeof(cmd.style.alpha_ref));
            h=SnapshotHash(h,&cmd.style.shadow_alpha_ref,sizeof(cmd.style.shadow_alpha_ref));
            const float emission[]={cmd.style.emissive_gain,cmd.style.emissive_specular};h=SnapshotHash(h,emission,sizeof(emission));
            h=SnapshotHash(h,&cmd.style.surface,sizeof(cmd.style.surface));
            snprintf(p->material_id,sizeof(p->material_id),"CAPTURE_DESCRIPTOR_FNV64:%016llx",static_cast<unsigned long long>(h));}
        /* Select at most four distinct referenced vertices, preserving indexed draw order. */
        if(have_uv && !p->uv_count){uint32_t sampled[4]={};
            for(uint32_t i=0;i<cmd.count && i<32 && p->uv_count<4 && uint64_t(cmd.first)+i<indices.size();++i){const uint32_t at=indices[cmd.first+i];
                if(at>=vertices.size())continue;
                bool seen=false;for(uint32_t j=0;j<p->uv_count;++j)if(sampled[j]==at)seen=true;
                if(seen)continue;
                sampled[p->uv_count]=at;p->uv[p->uv_count*2]=vertices[at].uv[0];p->uv[p->uv_count*2+1]=vertices[at].uv[1];++p->uv_count;}
            snprintf(p->uv_source,sizeof(p->uv_source),"CPU_CAPTURE_OUTPUT_BASE_UV");}
        cmd.provenance=p;
    }catch(...){RenderSnapshot_Text("TELEMETRY_LOSS","capture_provenance","ALLOCATION_FAILED; per-command provenance unavailable");}
}
// P0-S11 PERF02: per-draw reuse of generated stream vertices. A dense table
// over the referenced source range (the usual, compact case) or a hash map
// replaces the former std::map node per index.
struct VertexRemap {
    std::vector<uint32_t> dense;std::unordered_map<uint32_t,uint32_t> sparse;uint32_t low=0;bool use_dense=true;
    void Begin(uint32_t lowest,uint32_t highest,uint32_t n){low=lowest;const uint64_t span=uint64_t(highest)-lowest+1;use_dense=span<=uint64_t(n)*4+4096;
        if(use_dense)dense.assign(static_cast<size_t>(span),UINT32_MAX);else{sparse.clear();sparse.reserve(n);}}
    uint32_t Find(uint32_t at)const{if(use_dense)return dense[at-low];const auto f=sparse.find(at);return f==sparse.end()?UINT32_MAX:f->second;}
    void Set(uint32_t at,uint32_t generated){if(use_dense)dense[at-low]=generated;else sparse[at]=generated;}
    // Snapshot geometry checks walk the reused sources in ascending order.
    void Ordered(std::map<uint32_t,uint32_t>& out)const{out.clear();
        if(use_dense){for(size_t i=0;i<dense.size();++i)if(dense[i]!=UINT32_MAX)out[low+static_cast<uint32_t>(i)]=dense[i];}
        else out.insert(sparse.begin(),sparse.end());}
};
// P0-S11 PERF02 native model deduplication. A word-wise FNV-1a bucket hash
// (the hash never decides equality) and the submitted style fields the former
// key string held, compared by their bytes exactly as that string compared.
inline uint64_t NativeModelHash(uint64_t h,const void* data,size_t bytes){
    const unsigned char* p=static_cast<const unsigned char*>(data);
    for(size_t i=0;i+4<=bytes;i+=4){uint32_t w;memcpy(&w,p+i,4);h^=w;h*=UINT64_C(1099511628211);}
    for(size_t i=bytes&~size_t(3);i<bytes;++i){h^=p[i];h*=UINT64_C(1099511628211);}
    return h;}
inline bool SameSubmittedStyle(const Style& a,const Style& b){
    const uint32_t wa[]={a.blend,a.src,a.dst,a.clamp,a.cull,a.depth,a.write,a.alpha_func,a.shadow_alpha_func},wb[]={b.blend,b.src,b.dst,b.clamp,b.cull,b.depth,b.write,b.alpha_func,b.shadow_alpha_func};
    const float fa[]={a.alpha_ref,a.shadow_alpha_ref,a.emissive_gain,a.emissive_specular,a.min_z,a.max_z},fb[]={b.alpha_ref,b.shadow_alpha_ref,b.emissive_gain,b.emissive_specular,b.min_z,b.max_z};
    return !memcmp(wa,wb,sizeof(wa)) && !memcmp(fa,fb,sizeof(fa)) && !memcmp(&a.surface,&b.surface,sizeof(a.surface)) &&
        a.scissor==b.scissor && (!a.scissor || !memcmp(a.rect,b.rect,sizeof(a.rect)));}
struct Batch {
    uint32_t width,height,engine_frame,models,hands,ui;bool renderer_seen,ui_seen;unsigned ui_scope;
    std::vector<Vertex> vertices;std::vector<uint32_t> indices;std::vector<Command> commands;std::string error;
    std::vector<NativeModelCommand> native_models;uint64_t pose_bytes;
    std::set<std::string> model_keys;
    // P0-S11 PERF02: native model deduplication by hash bucket (first index,
    // chained through native_model_next) with an exact field comparison.
    std::unordered_map<uint64_t,uint32_t> native_model_heads;std::vector<uint32_t> native_model_next;
    uint64_t native_model_duplicates=0;
    Batch():width(0),height(0),engine_frame(0),models(0),hands(0),ui(0),renderer_seen(false),ui_seen(false),ui_scope(0),pose_bytes(0){}
    void Begin(uint32_t w,uint32_t h,uint32_t f){width=w;height=h;engine_frame=f;models=hands=ui=0;renderer_seen=ui_seen=false;ui_scope=0;
        vertices.clear();indices.clear();commands.clear();native_models.clear();pose_bytes=0;error.clear();model_keys.clear();
        native_model_heads.clear();native_model_next.clear();native_model_duplicates=0;}
    bool Ready()const{return width && height && renderer_seen && ui_seen && !ui_scope && error.empty();}
    void Fail(const char* why){if(error.empty())error=why;}
    // source/detail name the request only in the failure text (no per-draw string).
    void Reserve(uint32_t v,uint32_t i,const char* source="<unspecified>",uint32_t layer=UINT32_MAX,const char* detail_name=NULL){
        const uint64_t used=uint64_t(vertices.size())*sizeof(Vertex)+uint64_t(indices.size())*4;
        const uint64_t requested=uint64_t(v)*sizeof(Vertex)+uint64_t(i)*4;
        const uint64_t limit=32u*1024u*1024u;
        const char* why=commands.size()>=16384?"frame draw limit":used+requested>limit?"frame CPU geometry budget":NULL;
        if(!why)return;
        char detail[768];snprintf(detail,sizeof(detail),
            "%s; frame=%u used_bytes=%llu request_bytes=%llu total_bytes=%llu limit_bytes=%llu "
            "vertices=%llu indices=%llu request_vertices=%u request_indices=%u commands=%llu models=%u hands=%u UI=%u layer=%u source=%.180s%s%.180s",
            why,engine_frame,static_cast<unsigned long long>(used),static_cast<unsigned long long>(requested),
            static_cast<unsigned long long>(used+requested),static_cast<unsigned long long>(limit),
            static_cast<unsigned long long>(vertices.size()),static_cast<unsigned long long>(indices.size()),v,i,
            static_cast<unsigned long long>(commands.size()),models,hands,ui,layer,source?source:"<unnamed>",detail_name?" / ":"",detail_name?detail_name:"");
        throw std::runtime_error(detail);
    }
    void Model(const void* identity,const Mesh& mesh,const Matrix& world,const Matrix& vp,const Matrix* bones,uint32_t bone_count,
        uint32_t count,uint32_t first,uint32_t tris,const Style& style,uint32_t skin_mode=KICH_R4_SKIN_SOFTWARE,const char* name="<unnamed model>",bool diagnostic=false) {
        Check(count && count<=mesh.vertices.size() && tris && uint64_t(first)+uint64_t(tris)*3<=mesh.indices.size(),"model draw range");
        Check(!mesh.skinned || (bones && mesh.bones<=bone_count),"model bone pose range");
        Check(!mesh.two_weights || skin_mode==KICH_R4_SKIN_SOFTWARE || skin_mode==KICH_R4_SKIN_HARDWARE,
            "2W skin mode handshake missing; rebuild R1/R2");
        const uint32_t layer=style.max_z<.1f?1:0;
        std::string key(reinterpret_cast<const char*>(&identity),sizeof(identity));
        key.append(reinterpret_cast<const char*>(&world),sizeof(world));key.append(reinterpret_cast<const char*>(&vp),sizeof(vp));
        key.append(reinterpret_cast<const char*>(&first),sizeof(first));key.append(reinterpret_cast<const char*>(&tris),sizeof(tris));
        key.append(reinterpret_cast<const char*>(&count),sizeof(count));
        key.append(reinterpret_cast<const char*>(&skin_mode),sizeof(skin_mode));
        key.append(reinterpret_cast<const char*>(&layer),sizeof(layer));
        if(!model_keys.insert(key).second)return;
        Reserve(count,tris*3,name,layer);const uint32_t base=static_cast<uint32_t>(vertices.size());const Matrix mvp=kich_r4_scene::Multiply(world,vp);
        const Matrix normal_world=kich_r4_lighting::NormalMatrix(world);
        Command cmd={static_cast<uint32_t>(indices.size()),tris*3,layer,mesh.texture,style};cmd.semantic=CAPTURE_MODEL;cmd.captured_vertices=count;
        if(diagnostic){cmd.resource=name;cmd.skin=mesh.skinned?"CPU_CAPTURE_SKIN":"CPU_CAPTURE_RIGID";}
        for(uint32_t i=0;i<count;++i){const SourceVertex& src=mesh.vertices[i];float p[4]={src.p[0],src.p[1],src.p[2],1};
            kich_r4_scene::Vec3 n=UnpackNormal(src.normal);
            if(mesh.skinned){float a[4];Transform(src.p,bones[src.bone[0]],a);
                const kich_r4_scene::Vec3 na=kich_r4_lighting::Direction(n,bones[src.bone[0]]);
                if(src.bone[0]==src.bone[1]){for(unsigned c=0;c<3;++c)p[c]=a[c];}
                else{float b[4];Transform(src.p,bones[src.bone[1]],b);const float weight=SkinWeight(src.weight,skin_mode);
                    const float inverse=1.f-weight;for(unsigned c=0;c<3;++c)p[c]=a[c]*inverse+b[c]*weight;}
                if(src.bone[0]==src.bone[1])n=na;
                else {const kich_r4_scene::Vec3 nb=kich_r4_lighting::Direction(n,bones[src.bone[1]]);const float w=SkinWeight(src.weight,skin_mode);
                    n=kich_r4_scene::Vec3{na.x*(1-w)+nb.x*w,na.y*(1-w)+nb.y*w,na.z*(1-w)+nb.z*w};}}
            Vertex v={};Transform(p,mvp,v.p);for(unsigned c=0;c<4;++c)Check(Finite(v.p[c]),"model clip coordinates");
            float wp[4];Transform(p,world,wp);for(unsigned c=0;c<3;++c){Check(Finite(wp[c]),"model world coordinates");v.world[c]=wp[c];}
            n=kich_r4_scene::Normalize(kich_r4_lighting::Direction(n,normal_world));
            v.normal[0]=n.x;v.normal[1]=n.y;v.normal[2]=n.z;v.normal[3]=1;
            for(unsigned c=0;c<3;++c)Check(Finite(v.normal[c]),"model world normal");
            v.uv[0]=src.uv[0];v.uv[1]=src.uv[1];v.color=0xffffffffu;vertices.push_back(v);}
        for(uint32_t i=0;i<tris*3;++i){const uint32_t at=mesh.indices[first+i];Check(at<count,"progressive model vertex range");indices.push_back(base+at);}
        if(diagnostic)SnapshotCommand(cmd,vertices,indices,mesh.skinned?"SKINNED_MODEL":"RIGID_MODEL",&mesh,first);
        commands.push_back(cmd);if(layer)++hands;else ++models;
    }
    void NativeModel(const void* identity,const std::shared_ptr<Mesh>& mesh,const Matrix& world,const Matrix& vp,
        const Matrix* bones,uint32_t bone_count,uint32_t count,uint32_t first,uint32_t tris,const Style& style,
        uint32_t skin_mode,const char* name,const char* shader=NULL,const char* texture=NULL,const char* secondary=NULL,uint32_t effect=0,
        uint32_t shader_element=UINT32_MAX,uint32_t shader_pass=UINT32_MAX,const void* skeleton=NULL,const kich_r4_material::Descriptor* material=NULL,
        bool light_valid=false,float object_hemi=1,float object_sun=1,uint64_t lifecycle_serial=0){
        Check(mesh.get()!=NULL,"native model geometry missing");
        Check(count && count<=mesh->vertices.size() && tris && uint64_t(first)+uint64_t(tris)*3<=mesh->indices.size(),"native model draw range");
        Check(bone_count<=256 && (!mesh->skinned || (bones && mesh->bones<=bone_count)),"native model bone pose range");
        Check(!mesh->two_weights || skin_mode==KICH_R4_SKIN_SOFTWARE || skin_mode==KICH_R4_SKIN_HARDWARE,"2W skin mode handshake missing; rebuild R1/R2");
        ValidateModelMatrix(world);ValidateModelMatrix(vp);
        if(mesh->skinned)for(uint32_t i=0;i<mesh->bones;++i)ValidateModelMatrix(bones[i]);
        Mesh::RangeInfo range;
        Check(mesh->Range(first,tris*3,count,range),"native progressive model vertex range");
        Check(!mesh->two_weights || skin_mode!=KICH_R4_SKIN_HARDWARE || mesh->hardware_weights_safe,"2W hardware weight exceeds safe quantization range");
        Check(!light_valid || (Finite(object_hemi) && Finite(object_sun) && object_hemi>=0 && object_hemi<=1 && object_sun>=0 && object_sun<=1),"object exposure range");
        const uint32_t layer=style.max_z<.1f?1:0;
        const char* shader_name=shader?shader:mesh->shader.c_str();const char* texture_name=texture?texture:mesh->texture.c_str();const char* secondary_name=secondary?secondary:"";
        // P0-S11 PERF02: the former key string (every field below, floats by
        // their bytes, the palette included) is compared field by field
        // against the commands in its hash bucket, before anything is built.
        uint64_t hash=UINT64_C(14695981039346656037);
        {const uintptr_t pointers[2]={reinterpret_cast<uintptr_t>(identity),reinterpret_cast<uintptr_t>(mesh.get())};
            const uint32_t words[]={first,tris,count,skin_mode,layer,effect,shader_element,shader_pass};
            hash=NativeModelHash(hash,pointers,sizeof(pointers));hash=NativeModelHash(hash,words,sizeof(words));hash=NativeModelHash(hash,world.m,sizeof(world.m));}
        auto same=[&](const NativeModelCommand& o)->bool{
            if(o.visual_id!=uint64_t(reinterpret_cast<uintptr_t>(identity)) || o.mesh.get()!=mesh.get() || o.first!=first || o.count!=tris*3 || o.vertex_count!=count ||
                o.skin_mode!=skin_mode || o.layer!=layer || o.effect!=effect || o.shader_element!=shader_element || o.shader_pass!=shader_pass)return false;
            if(memcmp(o.world.m,world.m,sizeof(world.m)) || memcmp(o.vp.m,vp.m,sizeof(vp.m)) || !SameSubmittedStyle(o.source_style,style))return false;
            const float exposure[]={light_valid?1.f:0.f,object_hemi,object_sun},stored[]={o.object_light_valid?1.f:0.f,o.object_hemi,o.object_sun};
            if(memcmp(exposure,stored,sizeof(exposure)))return false;
            if(mesh->skinned && (o.bones.size()!=mesh->bones || memcmp(o.bones.data(),bones,mesh->bones*sizeof(Matrix))))return false;
            if(o.shader!=shader_name || o.texture!=texture_name || o.secondary!=secondary_name || o.has_material!=(material!=NULL))return false;
            return !material || kich_r4_material::SameKeyFields(o.material,*material);}; // the descriptor as copied, never its address
        const auto bucket=native_model_heads.find(hash);
        if(bucket!=native_model_heads.end())for(uint32_t at=bucket->second;at!=UINT32_MAX;at=native_model_next[at])
            if(same(native_models[at])){++native_model_duplicates;return;}
        NativeModelCommand cmd;cmd.mesh=mesh;cmd.world=world;cmd.vp=vp;cmd.vertex_count=count;cmd.first=first;cmd.count=tris*3;
        cmd.available_attributes=range.attributes;cmd.object_light_valid=light_valid;cmd.object_hemi=object_hemi;cmd.object_sun=object_sun;
        cmd.layer=layer;cmd.style=style;cmd.source_style=style;cmd.skin_mode=skin_mode;cmd.effect=effect;
        if(material){cmd.material=*material;cmd.has_material=true;kich_r4_material::Apply(cmd.material,cmd.style);}
        cmd.shader_element=shader_element;cmd.shader_pass=shader_pass;
        cmd.resource=name?name:"<unnamed model>";cmd.shader=shader_name;cmd.texture=texture_name;cmd.secondary=secondary_name;
        const uint32_t words[]={first,tris,count,skin_mode,cmd.layer,effect,shader_element,shader_pass,style.blend,style.src,style.dst,style.clamp,style.cull,style.depth,style.write,style.alpha_func,style.shadow_alpha_func};
        const float emission[]={style.emissive_gain,style.emissive_specular};
        const uint64_t bytes=mesh->skinned?uint64_t(mesh->bones)*sizeof(Matrix):0;
        Check(native_models.size()+commands.size()<16384,"native model frame draw limit");Check(pose_bytes+bytes<=16u*1024u*1024u,"native model frame palette budget");
        if(mesh->skinned)cmd.bones.assign(bones,bones+mesh->bones);
        cmd.bounds_known=NativeModelBounds(cmd);
        cmd.identity_measured=true;cmd.visual_id=static_cast<uint64_t>(reinterpret_cast<uintptr_t>(identity));cmd.skeleton_id=static_cast<uint64_t>(reinterpret_cast<uintptr_t>(skeleton));cmd.source_frame=engine_frame;cmd.lifecycle_serial=lifecycle_serial;
            if(g_render_snapshot_active && mesh->skinned && RenderSnapshot_Detailed()){cmd.pose_hash=SnapshotHash(UINT64_C(14695981039346656037),cmd.bones.data(),cmd.bones.size()*sizeof(Matrix));cmd.pose_hash_valid=true;}
        if(g_render_snapshot_active && RenderSnapshot_Detailed()){try{cmd.provenance.reset(new RenderSnapshotProvenance());RenderSnapshotProvenance& p=*cmd.provenance;RenderSnapshot_GetProvenance(&p);
            snprintf(p.semantic_type,sizeof(p.semantic_type),"%s",mesh->skinned?"SKINNED_MODEL":"RIGID_MODEL");
            snprintf(p.evidence,sizeof(p.evidence),"OGF_SHARED_GPU_MODEL");SnapshotModelID(*mesh,p.mesh_id,sizeof(p.mesh_id),first,cmd.count);
            uint64_t hash=SnapshotStringHash(UINT64_C(14695981039346656037),cmd.shader);hash=SnapshotStringHash(hash,cmd.texture);hash=SnapshotStringHash(hash,cmd.secondary);hash=SnapshotHash(hash,words+5,sizeof(words)-5*sizeof(uint32_t));hash=SnapshotHash(hash,&style.alpha_ref,sizeof(style.alpha_ref));
            hash=SnapshotHash(hash,&style.shadow_alpha_ref,sizeof(style.shadow_alpha_ref));
            hash=SnapshotHash(hash,emission,sizeof(emission));
            snprintf(p.material_id,sizeof(p.material_id),"MODEL_DESCRIPTOR_FNV64:%016llx",static_cast<unsigned long long>(hash));
            p.flags=0;p.uv_count=0;uint32_t sample[4]={};for(uint32_t i=0;i<cmd.count && i<32 && p.uv_count<4;++i){const uint32_t at=mesh->indices[first+i];bool used=false;
                for(uint32_t j=0;j<p.uv_count;++j)if(sample[j]==at)used=true;
                if(used)continue;
                sample[p.uv_count]=at;p.uv[p.uv_count*2]=mesh->vertices[at].uv[0];p.uv[p.uv_count*2+1]=mesh->vertices[at].uv[1];++p.uv_count;}
            snprintf(p.uv_source,sizeof(p.uv_source),"OGF_SOURCE_BASE_UV");
        }catch(...){RenderSnapshot_Text("TELEMETRY_LOSS","native_model_provenance","ALLOCATION_FAILED");}}
        const uint32_t index=static_cast<uint32_t>(native_models.size());const uint32_t chained=bucket!=native_model_heads.end()?bucket->second:UINT32_MAX;
        native_models.push_back(std::move(cmd));native_model_next.push_back(chained);native_model_heads[hash]=index;
        pose_bytes+=bytes;if(native_models.back().layer)++hands;else ++models;
    }
    Vertex Screen(float x,float y,float z,float rhw,float u,float v,uint32_t argb)const{
        Check(Finite(x) && Finite(y) && Finite(z) && Finite(rhw) && rhw>0 && Finite(u) && Finite(v),"HUD vertex coordinates");
        const float w=1.f/rhw;Check(Finite(w),"HUD reciprocal RHW");Vertex out={};out.p[0]=((x+.5f)*2.f/width-1.f)*w;
        out.p[1]=(1.f-(y+.5f)*2.f/height)*w;out.p[2]=z*w;out.p[3]=w;
        for(unsigned c=0;c<4;++c){Check(Finite(out.p[c]),"HUD clip coordinates");}out.uv[0]=u;out.uv[1]=v;out.color=RGBA(argb);return out;
    }
};
// Registration identity survives pointer reuse and shared geometry copies. It
// performs no per-frame allocation and is intentionally separate from mesh ID.
inline uint64_t NextModelLifetime(){static uint64_t serial=0;return ++serial;}
/* P0-S10 draw_skip_logged: a rejected model's first skipped draw is logged
 * with its load error, so an invisible object names itself in the log. */
struct Entry {std::shared_ptr<Mesh> mesh;std::string error,name;uint64_t lifecycle_serial;mutable bool draw_skip_logged;Entry():lifecycle_serial(0),draw_skip_logged(false){}};
inline std::string ModelError(const Entry& entry,const char* why){return "model="+entry.name+"; "+why;}
struct Segment {uint32_t offset;std::vector<unsigned char> bytes;};
struct Buffer {
    std::vector<Segment> segments;const void* pending;uint32_t offset,capacity,discard;bool fixed;
    Buffer():pending(NULL),offset(0),capacity(0),discard(0),fixed(false){}
    void Put(const void* data,uint32_t at,uint32_t count){Check(data && count && uint64_t(at)+count<=8u*1024u*1024u,"HUD buffer range");
        Segment segment;segment.offset=at;const unsigned char* p=static_cast<const unsigned char*>(data);segment.bytes.assign(p,p+count);segments.push_back(segment);}
    const unsigned char* Read(uint32_t at,uint32_t count)const {
        for(size_t i=segments.size();i;--i){const Segment& s=segments[i-1];if(at>=s.offset && uint64_t(at)+count<=uint64_t(s.offset)+s.bytes.size())return s.bytes.data()+at-s.offset;}
        throw std::runtime_error("HUD draw has no matching CPU buffer data");
    }
};
struct State {
    Batch frame;std::map<const void*,Entry> models;std::map<void*,Buffer> buffers;
    std::map<const void*,KichR4LocalLight> lights;std::set<const void*> unshadowed_lights;
    std::map<const void*,KichR4LightIdentity> light_sources;
    std::map<const void*,KichR4LightProjector> light_projectors;
    struct LightObservation {KichR4LocalLight value;bool active,valid;uint32_t frame;};
    std::map<const void*,LightObservation> light_observations;
    uint64_t light_observation_overflow=0,light_identity_rejections=0;
    uint64_t stream_bytes;bool native_models;
    // P0-S10: rejections/skips have their own budget; 2W notes on every NPC
    // model used to exhaust the shared 64 lines before a later rejection.
    uint32_t diagnostics,failure_diagnostics;void (*log)(const char*);
    State():stream_bytes(0),native_models(false),diagnostics(0),failure_diagnostics(0),log(NULL){}
    void Light(const void* id,const KichR4LocalLight& light,bool active){
        if(!id)return;
        const bool valid=kich_r4_lighting::ValidLight(light);
        if(!active || !valid)lights.erase(id);
        else if(lights.size()<KICH_R4_LIGHT_REGISTRY_CAPACITY || lights.find(id)!=lights.end())lights[id]=light;
        // Bounded observation metadata is separate from the render registry.
        // Retain inactive/invalid sources so a diagnostic can explain exclusion.
        try{if(light_observations.size()<KICH_R4_LIGHT_REGISTRY_CAPACITY || light_observations.count(id)){
            LightObservation o={light,active,valid,frame.engine_frame};light_observations[id]=o;
        }else ++light_observation_overflow;}catch(...){++light_observation_overflow;}
    }
    bool StaticLightDisabled(const void* id,bool r1_lights_enabled)const {
        const auto source=light_sources.find(id);
        // Match R2 CLight_DB::add_light. Neither position, controller nor
        // numerical equality establishes bStatic for a runtime registration.
        return !r1_lights_enabled && source!=light_sources.end() && (source->second.flags&KICH_R4_LIGHT_STATIC)!=0;
    }
    void SelectLights(const float* camera,KichR4LightingFrame& out,bool r1_lights_enabled=false,const float* camera_vp=NULL,KichR4LightVisibility occlusion=NULL,kich_r4_light_admission::Policy* admission=NULL)const {
        struct Choice {float score;KichR4LocalLight light;bool shadow;const void* source;};std::vector<Choice> choices;
        out.count=0;out.shadow_mask=0;out.shadows.clear();out.shadow_faces.clear();out.shadow_sizes.clear();out.shadow_face_reasons.clear();out.sources.clear();out.projectors.clear();out.lights.clear();out.visibility_valid=camera_vp?1u:0u;
        if(camera_vp)memcpy(out.visibility_vp,camera_vp,sizeof(out.visibility_vp));
        else memset(out.visibility_vp,0,sizeof(out.visibility_vp));
        out.r1_lights_requested=out.r1_lights_applied=r1_lights_enabled?1u:0u;out.lamp_faded=0;
        out.occlusion_enabled=(occlusion && camera_vp)?1u:0u;out.occlusion_tested=0;out.occlusion_rejected.clear();
        if(!camera)return;
        Matrix vp={};if(camera_vp)memcpy(vp.m,camera_vp,sizeof(vp.m));
        for(std::map<const void*,KichR4LocalLight>::const_iterator it=lights.begin();it!=lights.end();++it){
            if(StaticLightDisabled(it->first,r1_lights_enabled))continue;
            const KichR4LocalLight& l=it->second;float distance=0;
            for(unsigned a=0;a<3;++a){const float d=l.position[a]-camera[a];distance+=d*d;}
            const float brightness=std::max(l.color[0],std::max(l.color[1],l.color[2]));
            if(brightness<=0 || (camera_vp && !kich_r4_visibility::VisibleSphere(l.position,l.range,vp)))continue;
            // P0-S10 r4_lamp_fade: shadowed lamps fade with screen size; below
            // LOD 0.001 they are dropped before any occlusion query (R2 order).
            const bool shadow=unshadowed_lights.count(it->first)==0;KichR4LocalLight light=l;
            if(shadow){const float lod=KichR4LampLOD(l,camera,out.lamp_fade);
                if(lod<=.001f){++out.lamp_faded;continue;}
                for(unsigned a=0;a<3;++a)light.color[a]*=lod;}
            if(out.occlusion_enabled){++out.occlusion_tested;bool visible=true;
                try{visible=occlusion(l,camera_vp);}catch(...){visible=true;}
                KichR4LightIdentity identity={};const auto source=light_sources.find(it->first);
                if(source!=light_sources.end())identity=source->second;
                else identity.process_handle=static_cast<uint64_t>(reinterpret_cast<uintptr_t>(it->first));
                if(admission)visible=admission->Observe(identity,visible,unshadowed_lights.count(it->first)?0u:l.type==2?1u:6u);
                if(!visible){out.occlusion_rejected.push_back(identity);continue;}}
            Choice c={brightness*l.range*l.range/(distance+1)+(l.type==2 && distance<4?1.e10f:0),light,shadow,it->first};choices.push_back(c);
        }
        // Sorting is deterministic scheduling only. Every visible source is
        // admitted; the bounded registry is not a global gameplay light quota.
        std::stable_sort(choices.begin(),choices.end(),[](const Choice& a,const Choice& b){return a.score>b.score;});
        out.lights.resize(choices.size());out.sources.resize(choices.size());out.projectors.resize(choices.size());out.shadows.resize(choices.size());
        for(unsigned i=0;i<choices.size();++i){out.lights[i]=choices[i].light;out.shadows[i]=choices[i].shadow?1:0;
            const auto projector=light_projectors.find(choices[i].source);if(projector!=light_projectors.end())out.projectors[i]=projector->second;
            if(choices[i].shadow && i<32)out.shadow_mask|=1u<<i;
            const auto source=light_sources.find(choices[i].source);if(source!=light_sources.end())out.sources[i]=source->second;
            else out.sources[i].process_handle=static_cast<uint64_t>(reinterpret_cast<uintptr_t>(choices[i].source));}
        out.count=static_cast<uint32_t>(choices.size());
    }
    void SnapshotLights(const float* camera,const KichR4LightingFrame& selected)const {
        if(!g_render_snapshot_active || !camera)return;
        using namespace kich_r4_telemetry;
        uint64_t active=0,valid=0,admitted=0,selected_count=0,policy_rejected=0;
        uint64_t rejected_inactive=0,rejected_invalid=0,rejected_capacity=0,rejected_frustum=0,rejected_color=0,rejected_missing=0;
        Matrix selection_vp={};memcpy(selection_vp.m,selected.visibility_vp,sizeof(selection_vp.m));
        uint64_t selected_level=0,selected_runtime=0,selected_unknown=0,selected_shadow=0;
        for(auto it=light_observations.begin();it!=light_observations.end();++it){
            const LightObservation& o=it->second;const KichR4LocalLight& l=o.value;
            const auto source=light_sources.find(it->first);KichR4LightIdentity identity={};
            if(source!=light_sources.end())identity=source->second;
            else identity.process_handle=static_cast<uint64_t>(reinterpret_cast<uintptr_t>(it->first));
            float distance=0;for(unsigned a=0;a<3;++a){const float d=l.position[a]-camera[a];distance+=d*d;}
            const float brightness=std::max(l.color[0],std::max(l.color[1],l.color[2]));
            const bool in_registry=lights.count(it->first)!=0;
            const bool policy_passed=in_registry && !StaticLightDisabled(it->first,selected.r1_lights_applied!=0);
            const bool visible=!selected.visibility_valid || kich_r4_visibility::VisibleSphere(l.position,l.range,selection_vp);
            bool occluded=false;for(const auto& rejected:selected.occlusion_rejected)
                if(rejected.process_handle==identity.process_handle && rejected.serial==identity.serial){occluded=true;break;}
            const bool candidate=policy_passed && visible && brightness>0 && !occluded;
            unsigned slot=selected.count;for(unsigned i=0;i<selected.count && i<selected.sources.size();++i)if(selected.sources[i].process_handle==identity.process_handle && selected.sources[i].serial==identity.serial){slot=i;break;}
            const bool chosen=slot<selected.count;
            const char* reason=!o.active?"INACTIVE":!o.valid?"INVALID_DESCRIPTOR":!in_registry?"REGISTRY_CAPACITY_LIMIT":!policy_passed?"STATIC_LIGHT_DISABLED_R2FLAG_R1LIGHTS":brightness<=0?"NONPOSITIVE_COLOR":!visible?"OUTSIDE_CAMERA_FRUSTUM":occluded?"OCCLUDED_LIGHT_VOLUME":!chosen?"SELECTION_NOT_CAPTURED":"SELECTED_VISIBLE_SOURCE";
            if(o.active)++active;
            if(in_registry)++valid;
            if(candidate)++admitted;
            if(!o.active)++rejected_inactive;else if(!o.valid)++rejected_invalid;else if(!in_registry)++rejected_capacity;
            else if(!policy_passed)++policy_rejected;else if(brightness<=0)++rejected_color;
            else if(!visible)++rejected_frustum;else if(!chosen && !occluded)++rejected_missing;
            if(chosen){++selected_count;if(identity.role==KICH_R4_LIGHT_ROLE_LEVEL)++selected_level;else if(identity.role==KICH_R4_LIGHT_ROLE_RUNTIME)++selected_runtime;else ++selected_unknown;
                if(selected.CastsShadow(slot))++selected_shadow;}
            if(!RenderSnapshot_Detailed())continue;
            char id[64];if(identity.serial)Handle(id,sizeof(id),"source",identity.serial);else Handle(id,sizeof(id),"process_handle",identity.process_handle);
            Text("LIGHT",id,"registry","R4_NATIVE");Text("LIGHT",id,"id_scope",identity.serial?"PROCESS_LIFETIME_SOURCE_SERIAL":"PROCESS_POINTER; REUSE_NOT_RESOLVED");
            U64("LIGHT",id,"source_serial",identity.serial);U64("LIGHT",id,"process_handle",identity.process_handle);
            U64("LIGHT",id,"registration_observed",1);Text("LIGHT",id,"lifetime_status","NO_CAPTURE_FORGET_OBSERVED; ACTIVE_STATE_SEPARATE");U64("LIGHT",id,"active",o.active?1:0);U64("LIGHT",id,"descriptor_valid",o.valid?1:0);
            U64("LIGHT",id,"candidate",in_registry?1:0);U64("LIGHT",id,"policy_passed",policy_passed?1:0);U64("LIGHT",id,"admitted",candidate?1:0);U64("LIGHT",id,"selected",chosen?1:0);
            if(chosen)U64("LIGHT",id,"selected_slot",slot);else Text("LIGHT",id,"selected_slot","NOT_SELECTED");
            U64("LIGHT",id,"static_source",(identity.flags&KICH_R4_LIGHT_STATIC)!=0);
            U64("LIGHT",id,"submitted",0);Text("LIGHT",id,"submission_status","NOT_YET_SUBMITTED");
            Text("LIGHT",id,"role",identity.role==KICH_R4_LIGHT_ROLE_LEVEL?"LEVEL_RECORD":identity.role==KICH_R4_LIGHT_ROLE_RUNTIME?"RUNTIME_REGISTERED":"UNKNOWN");
            const bool level_valid=identity.role==KICH_R4_LIGHT_ROLE_LEVEL && identity.level_record!=UINT32_MAX;
            const bool controller_valid=identity.role==KICH_R4_LIGHT_ROLE_LEVEL && identity.controller!=UINT32_MAX;
            U64("LIGHT",id,"level_record_valid",level_valid);U64("LIGHT",id,"controller_valid",controller_valid);
            if(level_valid)U64("LIGHT",id,"level_record",identity.level_record);else Text("LIGHT",id,"level_record","NOT_AVAILABLE");
            if(controller_valid)U64("LIGHT",id,"controller",identity.controller);else Text("LIGHT",id,"controller","NOT_AVAILABLE");
            Text("LIGHT",id,"type",l.type==2?"SPOT":l.type==1?"POINT":"UNSUPPORTED");
            Vector("LIGHT",id,"position",l.position,3);Vector("LIGHT",id,"direction",l.direction,3);Vector("LIGHT",id,"color",l.color,3);
            Float("LIGHT",id,"range",l.range);Float("LIGHT",id,"cone",l.cone);Float("LIGHT",id,"camera_distance_squared",distance);
            Text("LIGHT",id,"selection_reason",reason);
            Text("LIGHT",id,"policy","BSTATIC_REQUIRES_R2FLAG_R1LIGHTS; ALL_VISIBLE_POSITIVE_SOURCES; NO_NUMERIC_DEDUP");
            U64("LIGHT",id,"shadow_requested",unshadowed_lights.count(it->first)==0);U64("LIGHT",id,"last_update_frame",o.frame);
            // P0-S11: the published plan of each shadow face and why.
            if(chosen && size_t(slot)*6+6<=selected.shadow_face_reasons.size() && selected.CastsShadow(slot)){char faces[160];size_t used=0;faces[0]=0;
                for(unsigned f=0;f<(l.type==2?1u:6u);++f){const int n=snprintf(faces+used,sizeof(faces)-used,"%s%u:%s",f?",":"",f,kich_r4_local_shadow::FaceReasonName(selected.shadow_face_reasons[size_t(slot)*6+f]));
                    if(n<0 || size_t(n)>=sizeof(faces)-used)break;used+=size_t(n);}
                Text("LIGHT",id,"shadow_faces",faces);
                if(slot<selected.shadow_faces.size())U64("LIGHT",id,"shadow_face_mask",selected.shadow_faces[slot]);
                if(slot<selected.shadow_sizes.size())U64("LIGHT",id,"shadow_size",selected.shadow_sizes[slot]);}
        }
        RenderSnapshot_U64("LIGHT_REGISTRY","registered_observed",light_observations.size());RenderSnapshot_U64("LIGHT_REGISTRY","identity_records",light_sources.size());
        RenderSnapshot_U64("LIGHT_POLICY","R2FLAG_R1LIGHTS_requested",selected.r1_lights_requested);RenderSnapshot_U64("LIGHT_POLICY","R2FLAG_R1LIGHTS_applied",selected.r1_lights_applied);
        RenderSnapshot_U64("LIGHT_REGISTRY","active",active);RenderSnapshot_U64("LIGHT_REGISTRY","valid_active",valid);RenderSnapshot_U64("LIGHT_REGISTRY","candidate",valid);
        RenderSnapshot_U64("LIGHT_REGISTRY","policy_passed",valid-policy_rejected);RenderSnapshot_U64("LIGHT_REGISTRY","policy_rejected",policy_rejected);
        RenderSnapshot_U64("LIGHT_REGISTRY","rejected_STATIC_LIGHT_DISABLED_R2FLAG_R1LIGHTS",policy_rejected);
        RenderSnapshot_U64("LIGHT_REGISTRY","rejected_INACTIVE",rejected_inactive);RenderSnapshot_U64("LIGHT_REGISTRY","rejected_INVALID_DESCRIPTOR",rejected_invalid);
        RenderSnapshot_U64("LIGHT_REGISTRY","rejected_REGISTRY_CAPACITY_LIMIT",rejected_capacity);RenderSnapshot_U64("LIGHT_REGISTRY","rejected_OUTSIDE_CAMERA_FRUSTUM",rejected_frustum);
        RenderSnapshot_U64("LIGHT_REGISTRY","rejected_NONPOSITIVE_COLOR",rejected_color);RenderSnapshot_U64("LIGHT_REGISTRY","rejected_SELECTION_NOT_CAPTURED",rejected_missing);
        RenderSnapshot_U64("LIGHT_REGISTRY","occlusion_query_candidates",selected.occlusion_tested);
        RenderSnapshot_U64("LIGHT_REGISTRY","rejected_OCCLUDED_LIGHT_VOLUME",selected.occlusion_rejected.size());
        RenderSnapshot_U64("LIGHT_REGISTRY","rejected_R4_LAMP_FADE",selected.lamp_faded);
        kich_r4_telemetry::Vector("LIGHT_REGISTRY","lamp_fade","fade_ssa_start_ssa_end",selected.lamp_fade,3);
        RenderSnapshot_U64("LIGHT_REGISTRY","admitted",admitted);RenderSnapshot_U64("LIGHT_REGISTRY","selected",selected_count);
        RenderSnapshot_U64("LIGHT_REGISTRY","selected_LEVEL_RECORD",selected_level);RenderSnapshot_U64("LIGHT_REGISTRY","selected_RUNTIME_REGISTERED",selected_runtime);RenderSnapshot_U64("LIGHT_REGISTRY","selected_UNKNOWN",selected_unknown);
        RenderSnapshot_U64("LIGHT_REGISTRY","slots_used",selected.count);RenderSnapshot_U64("LIGHT_REGISTRY","slots_capacity",KICH_R4_LIGHT_REGISTRY_CAPACITY);RenderSnapshot_U64("LIGHT_REGISTRY","selected_shadow_requested",selected_shadow);
        RenderSnapshot_U64("LIGHT_REGISTRY","observation_overflow_events",light_observation_overflow);
        RenderSnapshot_U64("LIGHT_REGISTRY","identity_registration_rejections",light_identity_rejections);
        RenderSnapshot_Text("LIGHT_REGISTRY","counter_scope","BOUNDED_OBSERVATION_REGISTRY_AT_CALCULATE; RETAINS_INACTIVE_UNTIL_FORGET; BEFORE_DICTIONARY_LIMIT");
        RenderSnapshot_Text("LIGHT_REGISTRY","policy","BSTATIC_REQUIRES_R2FLAG_R1LIGHTS; ALL_VISIBLE_POSITIVE_SOURCES; NO_GLOBAL_DRAW_LIGHT_LIMIT; ENVIRONMENT_SUN_SEPARATE; NO_DEDUP");
        RenderSnapshot_Text("LIGHT_REGISTRY","visibility_filter",selected.occlusion_enabled?"SOURCE_RANGE_SPHERE_FRUSTUM; CURRENT_CAMERA_CPU_HOM_FULL_LIGHT_VOLUME; FAIL_OPEN":selected.visibility_valid?"SOURCE_RANGE_SPHERE_INTERSECTS_CAMERA_FRUSTUM":"NO_CAMERA_FRUSTUM_SUPPLIED; CONSERVATIVE_ALL_SOURCES");
    }
    void Diagnostic(const std::string& message,bool failure=false){uint32_t& count=failure?failure_diagnostics:diagnostics;
        if(log && count<(failure?512u:64u)){++count;log(("[R4 bridge B4.3b.1] "+message).c_str());}}
    void Begin(uint32_t w,uint32_t h,uint32_t f){frame.Begin(w,h,f);stream_bytes=0;
        for(std::map<void*,Buffer>::iterator i=buffers.begin();i!=buffers.end();++i)if(!i->second.fixed){i->second.segments.clear();i->second.pending=NULL;}}
    uint64_t ModelBytes()const {uint64_t result=0;std::set<const Mesh*> counted;
        for(std::map<const void*,Entry>::const_iterator i=models.begin();i!=models.end();++i){if(i->second.mesh && counted.insert(i->second.mesh.get()).second)result+=i->second.mesh->Bytes();}return result;}
    void Load(void* visual,Span source,const char* name=NULL){Entry& entry=models[visual];entry=Entry();entry.lifecycle_serial=NextModelLifetime();entry.name=name && *name?std::string(name).substr(0,240):"<unnamed>";
        try{entry.mesh=Decode(source);if(entry.mesh){const uint64_t total=ModelBytes();
            if(total>ModelRegistryBudgetBytes){const uint64_t requested=entry.mesh->Bytes();char reason[192];
                snprintf(reason,sizeof(reason),"model registry CPU budget: used=%llu requested=%llu limit=%llu bytes",
                    static_cast<unsigned long long>(total-requested),static_cast<unsigned long long>(requested),
                    static_cast<unsigned long long>(ModelRegistryBudgetBytes));Check(false,reason);}
            entry.mesh->FreezeRanges();}}
        catch(const std::exception& e){entry.mesh.reset();entry.error=ModelError(entry,e.what());Diagnostic("model rejected: "+entry.error,true);return;}
        if(entry.mesh && entry.mesh->invalid_uvs){char count[96];snprintf(count,sizeof(count),"; non-finite UVs zeroed=%u",entry.mesh->invalid_uvs);
            Diagnostic("model repaired: model="+entry.name+count);}
        if(entry.mesh && (entry.mesh->ignored_weights || entry.mesh->outside_weights)){char counts[128];snprintf(counts,sizeof(counts),"; ignored_unused=%u active_outside_01=%u; ",entry.mesh->ignored_weights,entry.mesh->outside_weights);
            Diagnostic("2W compatibility: model="+entry.name+counts+entry.mesh->first_weight_detail);}}
    void Copy(void* visual,const void* source){const std::map<const void*,Entry>::const_iterator i=models.find(source);
        if(i!=models.end())models[visual]=i->second;else{Entry e;e.error="model copy without source registration";models[visual]=e;Diagnostic("model rejected: "+e.error,true);}models[visual].lifecycle_serial=NextModelLifetime();}
    void Lock(void* buffer,const void* data,uint32_t at,uint32_t capacity,uint32_t discard){if(!frame.ui_scope)return;
        Buffer& b=buffers[buffer];Check(!b.pending && !b.fixed,"HUD nested buffer lock");if(b.discard!=discard)b.segments.clear();
        b.pending=data;b.offset=at;b.capacity=capacity;b.discard=discard;}
    void Unlock(void* buffer,uint32_t bytes){std::map<void*,Buffer>::iterator i=buffers.find(buffer);if(i==buffers.end() || !i->second.pending)return;
        Buffer& b=i->second;Check(bytes<=b.capacity,"HUD unlock range");const void* data=b.pending;b.pending=NULL;
        if(bytes){Check(stream_bytes+bytes<=8u*1024u*1024u,"HUD stream CPU budget");b.Put(data,b.offset,bytes);stream_bytes+=bytes;}}
};
inline const char* VertexShader(){return "struct I{float4 p:POSITION;float2 uv:TEXCOORD0;float4 c:COLOR0;};"
    "struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;float4 c:COLOR0;};O main(I v){O o;o.p=v.p;o.uv=v.uv;o.c=v.c;return o;}";}
inline const char* PixelShader(){return "cbuffer Draw:register(b0){float4 alpha;};Texture2D image:register(t0);SamplerState surface:register(s0);"
    "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0,float4 color:COLOR0):SV_Target{float4 c=image.Sample(surface,uv)*color;"
    "float a=c.a;float r=alpha.x;int f=(int)alpha.y;bool visible=f==8 || (f==2 && a<r) || (f==3 && a==r) || (f==4 && a<=r) ||"
    "(f==5 && a>r) || (f==6 && a!=r) || (f==7 && a>=r);if(!visible)discard;return c;}";}
}
#endif
