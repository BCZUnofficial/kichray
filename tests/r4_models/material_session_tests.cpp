// Reuse the production-session recording API, not a second material simulator.
#define main G3cPaletteSuiteMain
#include "session_tests.cpp"
#undef main

namespace {
kich_r4_material::Descriptor CutoutMaterial(){
    kich_r4_material::Source source;source.family=kich_r4_material::FAMILY_MODEL;source.blend=true;source.alpha_reference=173;
    source.descriptor.shader="models/source_cutout";source.descriptor.source="SOC_MODEL_SOURCE_TEST";
    source.descriptor.base="native/base";source.descriptor.bump="native/bump";source.descriptor.bump_aux="native/bump#";
    source.descriptor.lightmap="level/lmap";source.descriptor.hemi="level/hemi";source.descriptor.detail="native/detail";
    source.descriptor.detail_usage=1;source.descriptor.detail_scale[0]=3;source.descriptor.detail_scale[1]=5;
    kich_r4_material::PassState state;state.blend=true;state.src=5;state.dst=6;state.cull=KICH_R4_CULL_FRONT;
    return kich_r4_material::Build(source,state);
}
std::shared_ptr<kich_r4_capture::Mesh> MaterialMesh(){auto mesh=Source();
    mesh->available_attributes=kich_r4_material::ATTR_NORMAL|kich_r4_material::ATTR_TANGENT|kich_r4_material::ATTR_BINORMAL|kich_r4_material::ATTR_UV1;
    for(auto& v:mesh->vertices){v.tangent=kich_r4_capture::PackNormal(1,0,0);v.binormal=kich_r4_capture::PackNormal(0,1,0);v.attributes=mesh->available_attributes;v.color=0xffffffffu;v.hemi=v.sun=1;}
    mesh->basis_vertices=static_cast<uint32_t>(mesh->vertices.size());return mesh;
}
void Capture(kich_r4_capture::Batch& batch,const std::shared_ptr<kich_r4_capture::Mesh>& mesh,const kich_r4_material::Descriptor& material){
    const kich_r4_scene::Matrix identity=kich_r4_scene::Identity(),bones[]={identity,identity};kich_r4_capture::Style legacy;
    legacy.blend=1;legacy.src=5;legacy.dst=6;legacy.write=0;legacy.cull=KICH_R4_CULL_BACK;
    batch.NativeModel(mesh.get(),mesh,identity,identity,bones,2,3,0,1,legacy,KICH_R4_SKIN_HARDWARE,"source_actor",
        "legacy_pass","legacy/stage0","legacy/stage1",0,0,0,mesh.get(),&material);
}
void SeedTextures(Harness& h,const kich_r4_material::Descriptor& material){h.level.white=h.white;h.level.checker=f->Add(KICH_R4_TEXTURE_2D);
    const std::string* names[]={&material.base,&material.bump,&material.bump_aux,&material.lightmap,&material.hemi,&material.detail};
    for(unsigned i=0;i<6;++i)h.level.textures[*names[i]]=f->Add(KICH_R4_TEXTURE_2D);
    h.level.textures["legacy/stage0"]=f->Add(KICH_R4_TEXTURE_2D);
}
void BeginMaterialSnapshot(const char* directory){RenderSnapshotInfo info={};strcpy(info.renderer,"R4");strcpy(info.api,"D3D11");strncpy(info.directory,directory,sizeof(info.directory)-1);
    Require(RenderSnapshot_Request(),"material snapshot request");RenderSnapshot_Begin(info);}
std::string MaterialReport(){std::ifstream file(RenderSnapshot_LastPath());std::ostringstream text;text<<file.rdbuf();return text.str();}
void MaterialCaptureOwnership(){auto mesh=MaterialMesh();kich_r4_capture::Batch batch;batch.Begin(640,480,42);
    auto material=CutoutMaterial();Capture(batch,mesh,material);Capture(batch,mesh,material);
    Require(batch.native_models.size()==1,"same source submission and material coalesce before native rendering");
    const uint64_t first_key=kich_r4_material::Key(batch.native_models[0].material);material.base="native/other_base";material.alpha_ref=.25f;Capture(batch,mesh,material);
    Require(batch.native_models.size()==2 && kich_r4_material::Key(batch.native_models[1].material)!=first_key,"different source material descriptors cannot alias capture commands");
    material.base.clear();material.source.clear();
    Require(batch.native_models[0].material.base=="native/base" && batch.native_models[1].material.base=="native/other_base" && batch.native_models[0].material.source=="SOC_MODEL_SOURCE_TEST","captured commands own source descriptor strings independently of caller lifetime");
    Require(batch.vertices.empty() && batch.indices.empty() && batch.commands.empty(),"descriptor routing keeps native immutable geometry without CPU expansion");
}
void MaterialBindingsAndRouting(const char* directory){Harness h;Require(h.Start()==0,"material native model setup");auto mesh=MaterialMesh();const auto material=CutoutMaterial();SeedTextures(h,material);
    kich_r4_capture::Batch batch;batch.Begin(640,480,42);Capture(batch,mesh,material);
    Require(batch.native_models[0].has_material && batch.native_models[0].style.blend==0 && batch.native_models[0].style.write==1,"source cutout overrides legacy forward style at capture");
    Require(h.models.Prepare(h.frame,h.level,batch)==0,"prepare source cutout model");const unsigned uploaded=uploads;const auto source_bytes=f->Find(h.models.meshes.at(mesh.get()).vb).bytes;
    BeginMaterialSnapshot(directory);RenderSnapshot_Pass("GBUFFER");Require(h.Draw(batch,KichR4ModelSession::Geometry)==0,"cutout geometry draw");
    Require(model_draws.size()==1,"one source model produces one geometry draw");const auto geometry=model_draws.back();const auto constants=constants_drawn.back();
    const std::string* names[]={&material.base,&material.bump,&material.bump_aux,&material.lightmap,&material.hemi,&material.detail};
    for(unsigned i=0;i<6;++i)Require(geometry.surface.textures[i]==h.level.textures.at(*names[i]),"actual t0 base t1 bump t2 auxiliary t3 lightmap t4 hemi t5 detail bind exact source resources");
    Require(geometry.surface.textures[0]!=h.level.textures.at("legacy/stage0"),"known native base takes precedence over R1 stage zero");
    Require(constants.material[0]==15 && constants.material[1]==3 && constants.material[2]==5,"bound optional resources select all four material branches with exact detail scale");
    Require(material.source_alpha_reference==173 && constants.alpha[0]==200.f/255.f && constants.alpha[1]==7 && constants.alpha[2]==0,"R2 effective cutout comparison and disabled blending reach b0 while original source reference remains metadata");
    Require(!geometry.surface.state.blend_enable && geometry.surface.state.source_blend==2 && geometry.surface.state.dest_blend==1 && geometry.surface.state.depth_write==1 && geometry.surface.state.draw.depth_mode==1 && geometry.surface.state.draw.cull_mode==KICH_R4_CULL_FRONT,"cutout geometry uses source cull and opaque depth/blend state");
    RenderSnapshot_Pass("SUN_SHADOW");Require(h.Draw(batch,KichR4ModelSession::Shadow)==0 && model_draws.size()==2,"cutout enters shadow pass");
    const auto shadow=model_draws.back();const auto shadow_constants=constants_drawn.back();
    Require(shadow.surface.textures[0]==geometry.surface.textures[0] && shadow_constants.alpha[0]==constants.alpha[0] && shadow_constants.alpha[1]==constants.alpha[1],"geometry and shadow use identical source alpha texture UV stream and threshold");
    Require(shadow.surface.state.draw.vertex_buffer==geometry.surface.state.draw.vertex_buffer && shadow.surface.state.draw.start_index==geometry.surface.state.draw.start_index && !shadow.surface.state.blend_enable && shadow.surface.state.depth_write && shadow.surface.state.draw.cull_mode==KICH_R4_CULL_FRONT,"shadow preserves immutable geometry source and cull while writing unblended depth");
    for(unsigned i=1;i<8;++i)Require(shadow.surface.textures[i]==0,"shadow never binds optional material maps");
    Require(shadow_constants.material[0]==0,"shadow has no bump lightmap hemi or detail feature flags");
    for(unsigned i=KichR4ModelSession::Forward;i<KichR4ModelSession::PassCount;++i)Require(h.Draw(batch,static_cast<KichR4ModelSession::Pass>(i))==0,"unselected model passes remain valid");
    Require(model_draws.size()==2 && !h.models.draw_calls[KichR4ModelSession::Forward] && !h.models.draw_calls[KichR4ModelSession::ForwardAdd],"source cutout produces no duplicate forward submissions");
    Require(uploads==uploaded && f->Find(h.models.meshes.at(mesh.get()).vb).bytes==source_bytes && batch.vertices.empty() && batch.indices.empty(),"material passes reuse immutable geometry and prepared palette");
    RenderSnapshot_End("COMPLETE");const std::string report=MaterialReport();
    Require(report.find("source=SOC_MODEL_SOURCE_TEST")!=std::string::npos && report.find("texture1=native/bump")!=std::string::npos && report.find("texture4=level/hemi")!=std::string::npos,"snapshot exposes descriptor source and actual auxiliary texture bindings");
}
void MaterialFallbacks(const char* directory){for(unsigned mode=0;mode<3;++mode){Harness h;Require(h.Start()==0,"material fallback setup");auto mesh=MaterialMesh();const auto material=CutoutMaterial();SeedTextures(h,material);
    if(mode==0)for(auto& v:mesh->vertices)v.attributes&=~(kich_r4_material::ATTR_TANGENT|kich_r4_material::ATTR_BINORMAL);
    if(mode==1)h.level.textures[material.bump_aux]=h.level.checker;
    if(mode==2)for(auto& v:mesh->vertices)v.attributes&=~kich_r4_material::ATTR_UV1;
    kich_r4_capture::Batch batch;batch.Begin(640,480,43+mode);Capture(batch,mesh,material);Require(h.models.Prepare(h.frame,h.level,batch)==0,"fallback prepares native source model");
    BeginMaterialSnapshot(directory);RenderSnapshot_Pass("GBUFFER");Require(h.Draw(batch,KichR4ModelSession::Geometry)==0 && model_draws.size()==1,"missing optional material evidence retains geometry submission");
    const auto& draw=model_draws.back();const auto& constants=constants_drawn.back();
    if(mode<2)Require(draw.surface.textures[1]==0 && draw.surface.textures[2]==0 && constants.material[0]==14,"invalid basis or missing auxiliary DDS disables both normal textures and bump flag");
    else Require(draw.surface.textures[3]==0 && draw.surface.textures[4]==0 && constants.material[0]==9,"missing UV1 disables lightmap and hemi while valid bump and detail survive");
    Require(draw.surface.textures[0]==h.level.textures.at(material.base) && constants.alpha[0]==material.alpha_ref,"optional fallback does not change cutout base or comparison");
    RenderSnapshot_End("COMPLETE");const char* reason=mode==0?"bump_missing_valid_source_basis":mode==1?"bump_dds_unavailable":"lightmap_missing_source_uv1";
    Require(MaterialReport().find(reason)!=std::string::npos,"snapshot explicitly explains optional material fallback");
}}
void DrawBasisAndExposure(){Harness h;Require(h.Start()==0,"object exposure setup");auto mesh=MaterialMesh();const auto material=CutoutMaterial();SeedTextures(h,material);
    mesh->vertices.push_back({});mesh->available_attributes=0; // unrelated unused vertex cannot poison this range
    kich_r4_capture::Batch batch;batch.Begin(640,480,50);Capture(batch,mesh,material);
    Require(batch.native_models[0].available_attributes==15,"model attributes come from referenced draw indices");
    auto& cmd=batch.native_models[0];cmd.object_light_valid=true;cmd.object_hemi=.2f;cmd.object_sun=0;
    Require(h.models.Prepare(h.frame,h.level,batch)==0 && h.Draw(batch,KichR4ModelSession::Geometry)==0,"object exposure draw");
    const auto& c=constants_drawn.back();Require(c.color_bias[3]==1 && c.color_scale[3]==.2f && c.sun_scale_bias[2]==0,"raw object visibility reaches model shader without weather multiplication");
    Require((uint32_t(c.material[0])&33)==33,"valid bump and object sun mask coexist");
}
}
int main(int argc,char** argv){try{Require(argc==2,"snapshot output directory");MaterialCaptureOwnership();MaterialBindingsAndRouting(argv[1]);MaterialFallbacks(argv[1]);DrawBasisAndExposure();
    printf("G3c native model material integration: %d checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
