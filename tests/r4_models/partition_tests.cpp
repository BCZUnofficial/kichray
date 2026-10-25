// Execute the production model session and inspect actual submitted index spans.
#define main PartitionBaseSuiteMain
#include "session_tests.cpp"
#undef main

namespace {
void PartitionRanges(){
    Harness h;Require(h.Start()==0,"partition fixture starts");auto mesh=Source(false);
    const uint32_t basis=kich_r4_material::ATTR_NORMAL|kich_r4_material::ATTR_TANGENT|kich_r4_material::ATTR_BINORMAL;
    for(auto& v:mesh->vertices){v.attributes=basis;v.tangent=kich_r4_capture::PackNormal(1,0,0);v.binormal=kich_r4_capture::PackNormal(0,1,0);}
    for(unsigned i=0;i<3;++i)mesh->vertices.push_back(mesh->vertices[i]);
    // P0-S11: a triangle is geometric only when none of its vertices has a basis.
    for(unsigned i=3;i<6;++i)mesh->vertices[i].attributes&=~kich_r4_material::ATTR_TANGENT;
    mesh->indices={0,1,2,0,1,2,3,4,5,0,1,2,3,4,5,3,4,5,0,1,2,0,1,2};
    kich_r4_material::Descriptor material;material.semantic=kich_r4_material::MATERIAL_OPAQUE;
    material.passes=kich_r4_material::PASS_GEOMETRY|kich_r4_material::PASS_SHADOW;
    material.bump="partition_bump";material.bump_aux="partition_bump_aux";
    h.level.white=h.white;h.level.checker=f->Add(KICH_R4_TEXTURE_2D);
    for(const auto* name:{&material.bump,&material.bump_aux}){const auto handle=f->Add(KICH_R4_TEXTURE_2D);h.level.textures[*name]=handle;
        auto& entry=h.level.texture_registry.entries[kich_r4_texture::Key(*name)];entry.handle=handle;}
    kich_r4_capture::Batch batch;const auto identity=kich_r4_scene::Identity();kich_r4_capture::Style style;
    std::map<unsigned,KichR4Handle> handles;
    // Includes all three index alignments: exceptional ranges remain exact,
    // with at most three source buffers rather than a buffer per range.
    for(unsigned repeat=0;repeat<2;++repeat)for(uint32_t first=0;first+3<=mesh->indices.size();++first)
      for(uint32_t count=3;first+count<=mesh->indices.size();count+=3){
        batch.Begin(640,480,1000+first*32+count);batch.NativeModel(mesh.get(),mesh,identity,identity,NULL,0,6,first,count/3,style,
            KICH_R4_SKIN_SOFTWARE,"partition_ranges",NULL,NULL,NULL,0,0,0,NULL,&material);
        Require(h.models.Prepare(h.frame,h.level,batch)==0,"LOD range prepares");
        if(repeat)Require(h.models.basis_creations==0,"second traversal creates no partition buffer");
        const size_t before=model_draws.size();Require(h.Draw(batch,KichR4ModelSession::Geometry)==0,"LOD range submits");
        std::vector<uint16_t> expected_good,expected_bad;
        for(uint32_t at=first;at<first+count;at+=3){bool valid=false; // P0-S11: any basis vertex maps the triangle
            for(unsigned k=0;k<3;++k)valid=valid || (mesh->vertices[mesh->indices[at+k]].attributes&basis)==basis;
            auto& out=valid?expected_good:expected_bad;out.insert(out.end(),mesh->indices.begin()+at,mesh->indices.begin()+at+3);}
        const bool mixed=!expected_good.empty() && !expected_bad.empty();
        Require(model_draws.size()-before==(mixed?2u:1u),"mixed range remains exactly two draws");
        for(size_t d=before;d<model_draws.size();++d){const auto& draw=model_draws[d].surface.state.draw;const auto& bytes=f->Find(draw.index_buffer).bytes;
            std::vector<uint16_t> actual(draw.index_count);memcpy(actual.data(),bytes.data()+draw.start_index*2,draw.index_count*2);
            const auto& expected=mixed?(d==before?expected_good:expected_bad):expected_good.empty()?expected_bad:expected_good;
            Require(actual==expected,"actual immutable index spans match independent source-triangle oracle");
            const bool mapped=mixed?d==before:!expected_good.empty();
            Require(bool(uint32_t(constants_drawn[d].material[0])&kich_r4_material::SHADER_BUMP)==mapped,"normal fallback follows the actual triangle subset");
            Require(mapped || (uint32_t(constants_drawn[d].material[0])&kich_r4_material::SHADER_GLOSS_MAP),"geometric subset keeps the authored bump gloss");
            if(mixed){auto& known=handles[first%3];if(known)Require(known==draw.index_buffer,"overlapping LOD ranges reuse one source buffer");else known=draw.index_buffer;}}
        const size_t shadow_before=model_draws.size();Require(h.Draw(batch,KichR4ModelSession::Shadow)==0,"source shadow range submits");
        Require(model_draws.size()==shadow_before+1 && model_draws.back().surface.state.draw.start_index==first && model_draws.back().surface.state.draw.index_count==count &&
            model_draws.back().surface.state.draw.index_buffer==h.models.meshes.at(mesh.get()).ib,"shadow keeps original unsplit range");
      }
    for(unsigned mode=0;mode<2;++mode){auto plain=material;plain.bump.clear();plain.bump_aux.clear();
        batch.Begin(640,480,9000+mode);batch.NativeModel(mesh.get(),mesh,identity,identity,NULL,0,6,3,4,style,
            KICH_R4_SKIN_SOFTWARE,"unmapped_range",NULL,NULL,NULL,0,0,0,NULL,mode?NULL:&plain);
        Require(h.models.Prepare(h.frame,h.level,batch)==0,"unmapped range prepares after mapped LODs");
        const size_t before=model_draws.size();Require(h.Draw(batch,KichR4ModelSession::Geometry)==0,"unmapped range draws");
        Require(model_draws.size()==before+1 && model_draws.back().surface.state.draw.index_buffer==h.models.meshes.at(mesh.get()).ib &&
            model_draws.back().surface.state.draw.start_index==3 && model_draws.back().surface.state.draw.index_count==12,
            "cached mapped partition does not split a bumpless or unknown material");}
    Require(h.models.basis_buffers==3 && handles.size()==3,"arbitrary valid range cache has a fixed three-alignment bound");
    uint64_t expected_bytes=0;for(unsigned alignment=0;alignment<3;++alignment)expected_bytes+=((mesh->indices.size()-alignment)/3)*3*2;
    Require(h.models.basis_bytes==expected_bytes && h.models.index_bytes==expected_bytes+mesh->indices.size()*2,"all shared partition bytes are charged exactly once");
    const auto old_handles=handles;batch.Begin(640,480,9999);mesh.reset();Require(h.models.Prepare(h.frame,h.level,batch)==0,"expired source retires shared partitions");
    for(const auto& entry:old_handles)Require(!f->objects.count(entry.second),"partition handle released with source");
    Require(!h.models.basis_buffers && !h.models.basis_bytes && !h.models.index_bytes,"retirement clears class byte and resource counters");
}
}
int main(){try{PartitionRanges();printf("E1.8 immutable model partition ranges: %d checks passed\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
