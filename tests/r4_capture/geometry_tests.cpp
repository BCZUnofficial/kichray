#include "fixture.h"
#include "../../xr_3da/KichR4CaptureData.h"
#include "../../xr_3da/KichR4Model.h"
#include <stddef.h>
#include <stdio.h>
#include <limits>
using namespace kich_r4_capture;
namespace {
int checks=0;
void Require(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
bool Near(float a,float b){return fabsf(a-b)<.003f;}
Span View(const fixture::Bytes& b){return Span(b.data(),static_cast<uint32_t>(b.size()));}
void SetFloat(fixture::Bytes& data,unsigned offset,float value){uint32_t bits;memcpy(&bits,&value,4);fixture::Set(data,offset,bits);}
fixture::Bytes Skinned(unsigned weights,bool mirrored,bool invalid){fixture::Bytes raw=capture_fixture::Model(weights);const Span vertices=Chunk(View(raw),3);
    const unsigned stride=weights==1?60:64,pos=weights==1?0:4,start=static_cast<unsigned>(vertices.p-raw.data())+8;
    for(unsigned i=0;i<3;++i){const unsigned p=start+i*stride+pos;SetFloat(raw,p+20,1);SetFloat(raw,p+24,invalid?0:1);SetFloat(raw,p+32,invalid?1:0);SetFloat(raw,p+40,mirrored?-1:1);}
    return raw;
}
fixture::Bytes Rigid(bool mirrored,bool conflicting=false){fixture::Bytes raw,h,t,v,indices;h.push_back(4);h.push_back(0);h.resize(44,0);fixture::Chunk(raw,1,h);
    fixture::Z(t,"base.dds,lmap_1,lmap_1_hemi");fixture::Z(t,"default");fixture::Chunk(raw,2,t);fixture::U32(v,0x252);fixture::U32(v,conflicting?4:3);
    const float p[4][2]={{0,0},{1,0},{0,1},{-1,0}},uv[4][2]={{0,0},{1,0},{0,1},{1,0}};
    for(unsigned i=0;i<(conflicting?4u:3u);++i){fixture::Float(v,p[i][0]);fixture::Float(v,p[i][1]);fixture::Float(v,0);
        fixture::Float(v,0);fixture::Float(v,0);fixture::Float(v,1);fixture::U32(v,0x80402010u);
        fixture::Float(v,uv[i][0]*(mirrored?-1:1));fixture::Float(v,uv[i][1]);fixture::Float(v,.125f+i*.25f);fixture::Float(v,.75f);}
    fixture::Chunk(raw,3,v);fixture::U32(indices,conflicting?6:3);fixture::U16(indices,0);fixture::U16(indices,1);fixture::U16(indices,2);
    if(conflicting){fixture::U16(indices,0);fixture::U16(indices,2);fixture::U16(indices,3);}fixture::Chunk(raw,4,indices);return raw;
}
fixture::Bytes SkeletalGeometry(unsigned weights,bool mirrored,unsigned invalid_mask,bool conflicting=false){
    fixture::Bytes raw,h,t,v,indices;h.push_back(4);h.push_back(5);h.resize(44,0);fixture::Chunk(raw,1,h);
    fixture::Z(t,"act\\test");fixture::Z(t,"models\\model");fixture::Chunk(raw,2,t);
    fixture::U32(v,weights==1?0x12071980u:0x240e3300u);fixture::U32(v,conflicting?4:3);
    const float p[4][2]={{0,0},{1,0},{0,1},{-1,0}},uv[4][2]={{0,0},{1,0},{0,1},{1,0}};
    for(unsigned i=0;i<(conflicting?4u:3u);++i){
        if(weights==2){fixture::U16(v,i%2);fixture::U16(v,(i+1)%2);}
        fixture::Float(v,p[i][0]);fixture::Float(v,p[i][1]);fixture::Float(v,0);
        fixture::Float(v,0);fixture::Float(v,0);fixture::Float(v,1);
        // Authored valid T/B deliberately differs from the UV-derived frame.
        const bool missing=(invalid_mask&(1u<<i))!=0;
        fixture::Float(v,0);fixture::Float(v,missing?0:1);fixture::Float(v,0);
        fixture::Float(v,missing?0:-1);fixture::Float(v,0);fixture::Float(v,0);
        if(weights==2)fixture::Float(v,.2f+.1f*i);
        fixture::Float(v,uv[i][0]*(mirrored?-1:1));fixture::Float(v,uv[i][1]);
        if(weights==1)fixture::U32(v,i%2);
    }
    fixture::Chunk(raw,3,v);fixture::U32(indices,conflicting?6:3);
    fixture::U16(indices,0);fixture::U16(indices,1);fixture::U16(indices,2);
    if(conflicting){fixture::U16(indices,0);fixture::U16(indices,2);fixture::U16(indices,3);}
    fixture::Chunk(raw,4,indices);return raw;
}
void RepairSkeletalBasis(){
    for(unsigned weights=1;weights<=2;++weights)for(unsigned mirrored=0;mirrored<2;++mirrored){
        const auto authored=Decode(View(SkeletalGeometry(weights,mirrored!=0,0)));
        const auto repaired=Decode(View(SkeletalGeometry(weights,mirrored!=0,2)));
        Require(authored->derived_basis_vertices==0 && repaired->derived_basis_vertices==1 && repaired->basis_vertices==3,
            "1W/2W repair affects only the one missing source basis");
        for(unsigned i=0;i<3;++i){const auto& a=authored->vertices[i];const auto& b=repaired->vertices[i];
            Require(!memcmp(a.p,b.p,sizeof(a.p)) && !memcmp(a.uv,b.uv,sizeof(a.uv)) && !memcmp(a.bone,b.bone,sizeof(a.bone)) && a.weight==b.weight && a.normal==b.normal,
                "skeletal repair preserves topology inputs, bind-space positions, UVs, normals and bone influences");
            if(i!=1)Require(!memcmp(&a,&b,sizeof(a)),"valid authored skeletal basis remains byte-identical even when neighboring basis is repaired");
        }
        const auto t=UnpackNormal(repaired->vertices[1].tangent),b=UnpackNormal(repaired->vertices[1].binormal);
        Require(Near(t.x,mirrored?-1:1) && Near(b.y,1) && (repaired->available_attributes&7)==7,
            "recovered source basis follows UV orientation across distinct bone IDs");
        Require(repaired->indices==authored->indices && repaired->vertices.size()==authored->vertices.size() && repaired->Bytes()==authored->Bytes(),
            "repair does not duplicate vertices, alter indices or increase immutable GPU payload");
        const auto complete=Decode(View(SkeletalGeometry(weights,mirrored!=0,7)));
        Require(complete->derived_basis_vertices==3 && complete->basis_vertices==3,"entire missing skeletal basis is reconstructed from bind-space surface");
        auto degenerate=SkeletalGeometry(weights,mirrored!=0,2);const Span vertices=Chunk(View(degenerate),3);
        const unsigned start=static_cast<unsigned>(vertices.p-degenerate.data())+8,stride=weights==1?60:64,pos=weights==1?0:4,uv=weights==1?48:56;
        for(unsigned i=0;i<3;++i){SetFloat(degenerate,start+i*stride+uv,0);SetFloat(degenerate,start+i*stride+uv+4,0);}
        const auto missing=Decode(View(degenerate));
        Require(missing->basis_vertices==2 && !missing->derived_basis_vertices && (missing->available_attributes&7)==1,
            "degenerate UV leaves only the missing vertex invalid and conservatively disables draw-range bump");
        auto nonfinite=SkeletalGeometry(weights,mirrored!=0,0);
        SetFloat(nonfinite,start+stride+pos+24,std::numeric_limits<float>::quiet_NaN());
        SetFloat(nonfinite,start+stride+pos+36,std::numeric_limits<float>::infinity());
        const auto sanitized=Decode(View(nonfinite));
        Require(sanitized->basis_vertices==3 && sanitized->derived_basis_vertices==1 && (sanitized->available_attributes&7)==7,
            "nonfinite authored tangent/binormal recover from finite surface geometry without contaminating valid neighbors");
        auto bad_normal=SkeletalGeometry(weights,mirrored!=0,2);
        SetFloat(bad_normal,start+stride+pos+20,0);const auto no_normal=Decode(View(bad_normal));
        Require(no_normal->vertices[1].normal==0 && no_normal->vertices[1].tangent==0 && !no_normal->derived_basis_vertices,
            "missing normal cannot be fabricated by T/B repair");
        auto bad_position=SkeletalGeometry(weights,mirrored!=0,7);
        for(unsigned i=0;i<3;++i)for(unsigned a=0;a<3;++a)SetFloat(bad_position,start+i*stride+pos+a*4,0);
        const auto no_area=Decode(View(bad_position));
        Require(!no_area->basis_vertices && !no_area->derived_basis_vertices,"zero-area skeletal geometry retains explicit flat fallback");
        const auto seam=Decode(View(SkeletalGeometry(weights,mirrored!=0,15,true)));
        Require(seam->basis_vertices==2 && seam->derived_basis_vertices==2 && (seam->available_attributes&7)==1,
            "shared mirrored UV orientation conflicts cannot silently fabricate skeletal tangent frames");
    }
}
void SourceBasis(){Require(sizeof(SourceVertex)==sizeof(KichR4ModelVertex) && sizeof(SourceVertex)==64 && offsetof(SourceVertex,tangent)==32 && offsetof(SourceVertex,uv1)==40 && offsetof(SourceVertex,attributes)==60,"CPU source and C ABI preserve explicit 64-byte geometry layout");
    for(unsigned weights=1;weights<=2;++weights)for(unsigned mirrored=0;mirrored<2;++mirrored){const auto raw=Skinned(weights,mirrored!=0,false);const auto mesh=Decode(View(raw));
        Require(mesh->basis_vertices==3 && !mesh->derived_basis_vertices && (mesh->available_attributes&7)==7,"original 1W/2W source N/T/B retained without reconstruction");
        const SourceVertex& v=mesh->vertices[0];const auto n=UnpackNormal(v.normal),t=UnpackNormal(v.tangent),b=UnpackNormal(v.binormal);
        Require(Near(n.z,1) && Near(t.x,1) && Near(b.y,mirrored?-1:1),"original bone-source tangent and binormal use exact SkeletonX offsets and retain mirrored sign");
        Require(v.bone[0]==(weights==1?1:0) && v.bone[1]==1 && Near(v.weight,weights==2?.25f:0),"basis decoding never aliases 1W bone ID or raw 2W weight");
        Require(!(v.attributes&(kich_r4_level::ATTR_UV1|kich_r4_level::ATTR_HEMI|kich_r4_level::ATTR_SUN)),"model source does not fabricate static baked-light data");
        const auto bad=Skinned(weights,false,true);const auto flat=Decode(View(bad));
        Require(flat->basis_vertices==3 && flat->derived_basis_vertices==3 && (flat->available_attributes&7)==7,"collapsed bone-source basis is reconstructed from valid indexed geometry and UVs");
    }
    auto bad=Skinned(1,false,false);const Span vertices=Chunk(View(bad),3);SetFloat(bad,static_cast<unsigned>(vertices.p-bad.data())+8+12,std::numeric_limits<float>::quiet_NaN());const auto flat=Decode(View(bad));
    Require(flat->vertices[0].normal==0 && flat->vertices[0].tangent==0 && !(flat->available_attributes&7),"nonfinite basis does not reject or poison the otherwise valid model geometry");
}
void RigidBasis(){for(unsigned mirrored=0;mirrored<2;++mirrored){const auto raw=Rigid(mirrored!=0);const auto mesh=Decode(View(raw));const SourceVertex& v=mesh->vertices[0];
        Require(mesh->vertices.size()==3 && mesh->indices.size()==3 && mesh->derived_basis_vertices==3 && mesh->basis_vertices==3,"rigid FVF computes its indexed tangent basis once at source loading without mesh expansion");
        const auto t=UnpackNormal(v.tangent),b=UnpackNormal(v.binormal);Require(Near(t.x,mirrored?-1:1) && Near(b.y,1),"derived rigid basis respects mirrored base UVs");
        Require(Near(v.uv1[0],.125f) && Near(v.uv1[1],.75f) && (mesh->available_attributes&kich_r4_level::ATTR_UV1),"rigid second FVF float2 remains independent UV1");
        Require(v.color==0x80102040u && !(v.attributes&kich_r4_level::ATTR_SUN),"rigid diffuse color remains color; alpha is not guessed to be baked sun");
        Require(mesh->texture=="base" && mesh->texture_list=="base.dds,lmap_1,lmap_1_hemi","material receives complete original texture assignment");
    }
    const auto conflict=Rigid(false,true);const auto mesh=Decode(View(conflict));Require(mesh->basis_vertices==2 && !(mesh->available_attributes&kich_r4_level::ATTR_TANGENT),"shared vertices across mirrored orientation conflict explicitly fall back instead of cancelling tangent sums");
    auto doubled=Rigid(false);const Span index=Chunk(View(doubled),4);const unsigned offset=static_cast<unsigned>(index.p-doubled.data());
    fixture::Set(doubled,offset-4,index.n+6);fixture::Set(doubled,offset,6);fixture::U16(doubled,0);fixture::U16(doubled,2);fixture::U16(doubled,1);
    const auto reverse=Decode(View(doubled));Require(reverse->basis_vertices==3,"opposite triangle winding with the same UV mapping retains its basis handedness");
    const auto source=capture_fixture::Model(0);const auto absent=Decode(View(source));Require(!absent->basis_vertices,"zero source normal cannot produce a fabricated rigid tangent frame");
}
}
int main(){try{SourceBasis();RigidBasis();RepairSkeletalBasis();printf("G3g rigid / 1W / 2W source geometry and mirrored TBN: %d checks passed\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
