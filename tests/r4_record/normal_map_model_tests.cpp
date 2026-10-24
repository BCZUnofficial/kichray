// Run the actual GPU-model planning/binding hook against the recording API.
#define main NormalProbeFixtureMain
#include "../r4_models/session_tests.cpp"
#undef main

namespace {
void ActualBindings(){
    using namespace kich_r4_normal_probe;
    for(unsigned mode=0;mode<10;++mode){Harness h;Require(h.Start()==0,"normal probe model setup");auto mesh=Source(false);
        const uint32_t basis=kich_r4_material::ATTR_NORMAL|kich_r4_material::ATTR_TANGENT|kich_r4_material::ATTR_BINORMAL;
        for(auto& vertex:mesh->vertices){vertex.attributes=basis;vertex.tangent=kich_r4_capture::PackNormal(1,0,0);vertex.binormal=kich_r4_capture::PackNormal(0,1,0);}
        if(mode==1)mesh->vertices[1].attributes&=~kich_r4_material::ATTR_TANGENT;
        kich_r4_material::Descriptor material;material.semantic=kich_r4_material::MATERIAL_OPAQUE;
        material.passes=kich_r4_material::PASS_GEOMETRY|kich_r4_material::PASS_SHADOW;
        material.base="normal_probe_base";material.bump="normal_probe_bump";material.bump_aux="normal_probe_bump#";
        if(mode==4)material.bump.clear();
        if(mode==5)material.bump_aux.clear();
        h.level.white=h.white;h.level.checker=f->Add(KICH_R4_TEXTURE_2D);
        for(const auto* name:{&material.base,&material.bump,&material.bump_aux})if(!name->empty()){
            const KichR4Handle handle=mode==2 && name==&material.bump?h.level.checker:f->Add(KICH_R4_TEXTURE_2D);
            h.level.textures[*name]=handle;h.level.texture_registry.entries[kich_r4_texture::Key(*name)].handle=handle;}
        if(mode==8){for(unsigned i=0;i<3;++i)mesh->vertices.push_back(mesh->vertices[i]);mesh->vertices[4].attributes&=~kich_r4_material::ATTR_TANGENT;mesh->indices={0,1,2,3,4,5};}
        const uint32_t number=100+mode;kich_r4_capture::Batch batch;batch.Begin(640,480,number);
        const auto identity=kich_r4_scene::Identity();kich_r4_capture::Style style;
        batch.NativeModel(mesh.get(),mesh,identity,identity,NULL,0,uint32_t(mesh->vertices.size()),0,mode==8?2:1,style,
            KICH_R4_SKIN_SOFTWARE,"probe_actor",NULL,NULL,NULL,0,0,0,NULL,mode==3?NULL:&material);
        if(mode==6 || mode==7)batch.native_models[0].layer=1;
        if(mode==7)batch.native_models[0].effect=1;
        kich_r4_record::Request(true);kich_r4_record::BeginFrame(number);Global().BeginFrame(number);
        Require(h.models.Prepare(h.frame,h.level,batch)==0,"normal probe source prepares");
        const auto pass=mode==6 || mode==7?KichR4ModelSession::HUD:KichR4ModelSession::Geometry;
        if(mode==9)f->fail=f->calls+2; // constants succeed, actual draw fails.
        const int32_t result=h.Draw(batch,pass);const Counts& counts=Global().categories[mode==6 || mode==7?HUD:Model];
        if(mode==9){Require(result<0 && counts.draws==0,"failed actual submission cannot report a successful mapped draw");continue;}
        Require(result==0,"normal probe actual draw succeeds");
        const Reason expected[]={Mapped,MissingBasis,MapsUnavailable,UnknownMaterial,NoAuthoredBump,MissingMetadata,Mapped,PathDisabled,Mapped};
        Require(counts.draws==(mode==8?2u:1u) && counts.reasons[expected[mode]]==1,"probe cause matches actual material/basis/texture pipeline");
        if(mode==8)Require(counts.mapped==1 && counts.mapped_indices==3 && counts.indices==6 && counts.reasons[MissingBasis]==1,"split good/bad subsets report exact submitted index counts");
        if(mode==3)Require(counts.reasons[NoAuthoredBump]==0,"unknown source is not called an authored flat material");
        const uint64_t before=counts.draws;Require(h.Draw(batch,KichR4ModelSession::Shadow)==0 && counts.draws==before,"shadow repeats do not inflate geometry normal diagnostics");
        kich_r4_record::Request(false);kich_r4_record::BeginFrame(number);Require(h.Draw(batch,pass)==0 && counts.draws==before,"record OFF retains draw behavior and does not collect");
    }
    kich_r4_record::Request(false);kich_r4_record::BeginFrame(111);
}
}
int main(){try{ActualBindings();std::printf("PASS: actual normal-map model/HUD/split/failed draw hooks: %d checks\n",checks);return 0;}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
