// Reuse the production-session recording API, not a second material simulator.
#define main G3cPaletteSuiteMain
#include "session_tests.cpp"
#undef main
#include "../r4_capture/fixture.h"

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
    for(unsigned i=0;i<6;++i){const auto handle=f->Add(KICH_R4_TEXTURE_2D);h.level.textures[*names[i]]=handle;h.level.texture_registry.entries[kich_r4_texture::Key(*names[i])].handle=handle;}
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
    batch.Begin(640,480,43);material=CutoutMaterial();Capture(batch,mesh,material);
    material.shadow_alpha_ref=200.f/255.f;Capture(batch,mesh,material);
    material.shadow_alpha_func=7;Capture(batch,mesh,material);
    Require(batch.native_models.size()==3,"independent shadow reference and comparison each survive material deduplication");
    batch.Begin(640,480,44);const auto identity=kich_r4_scene::Identity();const kich_r4_scene::Matrix bones[]={identity,identity};kich_r4_capture::Style style;
    for(unsigned i=0;i<3;++i){if(i==1){style.shadow_alpha_func=5;style.shadow_alpha_ref=220.f/255.f;}if(i==2)style.shadow_alpha_ref=200.f/255.f;
        batch.NativeModel(mesh.get(),mesh,identity,identity,bones,2,3,0,1,style,KICH_R4_SKIN_HARDWARE,"unresolved_source");}
    Require(batch.native_models.size()==3 && batch.native_models[1].style.shadow_alpha_func==5 && batch.native_models[2].style.shadow_alpha_ref==200.f/255.f,"captured shadow policy survives unresolved-model style deduplication");
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
    Require(shadow.surface.textures[0]==geometry.surface.textures[0] && shadow_constants.alpha[0]==220.f/255.f && shadow_constants.alpha[1]==5,"model shadow preserves source texture/UV with independent SoC R2 GREATER 220 threshold");
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
    if(mode==1){h.level.textures[material.bump_aux]=h.level.checker;h.level.texture_registry.entries[kich_r4_texture::Key(material.bump_aux)].handle=h.level.checker;}
    if(mode==2)for(auto& v:mesh->vertices)v.attributes&=~kich_r4_material::ATTR_UV1;
    kich_r4_capture::Batch batch;batch.Begin(640,480,43+mode);Capture(batch,mesh,material);Require(h.models.Prepare(h.frame,h.level,batch)==0,"fallback prepares native source model");
    BeginMaterialSnapshot(directory);RenderSnapshot_Pass("GBUFFER");Require(h.Draw(batch,KichR4ModelSession::Geometry)==0 && model_draws.size()==1,"missing optional material evidence retains geometry submission");
    const auto& draw=model_draws.back();const auto& constants=constants_drawn.back();
    // P0-S11: without normal mapping the bump stays bound for its gloss (bump.R^2).
    if(mode<2)Require(draw.surface.textures[1]==h.level.textures.at(material.bump) && draw.surface.textures[2]==0 && constants.material[0]==14+kich_r4_material::SHADER_GLOSS_MAP,"invalid basis or missing auxiliary DDS withdraws normal mapping and keeps bump gloss");
    else Require(draw.surface.textures[3]==0 && draw.surface.textures[4]==0 && constants.material[0]==9,"missing UV1 disables lightmap and hemi while valid bump and detail survive");
    Require(draw.surface.textures[0]==h.level.textures.at(material.base) && constants.alpha[0]==material.alpha_ref,"optional fallback does not change cutout base or comparison");
    RenderSnapshot_End("COMPLETE");const char* reason=mode==0?"bump_missing_valid_source_basis":mode==1?"bump_dds_unavailable":"lightmap_missing_source_uv1";
    Require(MaterialReport().find(reason)!=std::string::npos,"snapshot explicitly explains optional material fallback");
}}
void RepairedModelBindsBump(const char* directory){
    for(unsigned weights=1;weights<=2;++weights){
        auto raw=capture_fixture::Model(weights);const kich_r4_level::Span source(raw.data(),static_cast<uint32_t>(raw.size()));
        const auto vb=kich_r4_level::Chunk(source,3);const unsigned start=static_cast<unsigned>(vb.p-raw.data())+8,stride=weights==1?60:64,pos=weights==1?0:4;
        for(unsigned i=0;i<3;++i){const float p[]={i==1?.3f:0.f,i==2?.3f:0.f,.5f};
            for(unsigned a=0;a<3;++a){uint32_t bits;memcpy(&bits,p+a,4);fixture::Set(raw,start+i*stride+pos+a*4,bits);}
            fixture::Set(raw,start+i*stride+pos+20,0x3f800000u); // Valid N, zero T/B in the actual OGF payload.
        }
        const auto mesh=kich_r4_capture::Decode(source);
        Require(mesh->derived_basis_vertices==3 && mesh->basis_vertices==3,"actual skeletal OGF recovers all referenced tangent frames");
        Harness h;Require(h.Start()==0,"repaired material integration setup");const auto material=CutoutMaterial();SeedTextures(h,material);
        kich_r4_capture::Batch batch;batch.Begin(640,480,70+weights);const auto identity=kich_r4_scene::Identity();const kich_r4_scene::Matrix bones[]={identity,identity};kich_r4_capture::Style style;
        batch.NativeModel(mesh.get(),mesh,identity,identity,bones,2,3,3,1,style,KICH_R4_SKIN_HARDWARE,"repaired_actor",NULL,NULL,NULL,0,0,0,mesh.get(),&material);
        Require(h.models.Prepare(h.frame,h.level,batch)==0,"repaired source uploads through normal GPU model path");
        BeginMaterialSnapshot(directory);RenderSnapshot_Pass("GBUFFER");Require(h.Draw(batch,KichR4ModelSession::Geometry)==0 && model_draws.size()==1,"repaired source reaches actual model material draw");
        Require((uint32_t(constants_drawn.back().material[0])&kich_r4_material::SHADER_BUMP)!=0 && model_draws.back().surface.textures[1]==h.level.textures[material.bump] && model_draws.back().surface.textures[2]==h.level.textures[material.bump_aux],"1W/2W decoded repaired bases enable real bump and auxiliary bindings");
        RenderSnapshot_End("COMPLETE");const auto report=MaterialReport();
        Require(report.find("basis_mesh_derived_vertices=3")!=std::string::npos && report.find("basis_missing_index_references=0")!=std::string::npos && report.find("normal_path=SOC_R2_BUMP")!=std::string::npos,"repair result is visible in model execution telemetry");
    }
}
void BasisFallbackTelemetry(const char* directory){
    Harness h;Require(h.Start()==0,"basis telemetry setup");auto mesh=MaterialMesh();const auto material=CutoutMaterial();SeedTextures(h,material);
    mesh->vertices[1].attributes&=~(kich_r4_material::ATTR_TANGENT|kich_r4_material::ATTR_BINORMAL);
    mesh->vertices.push_back({}); // Unreferenced source vertex must not inflate this draw's missing count.
    mesh->derived_basis_vertices=1;
    kich_r4_capture::Batch batch;batch.Begin(640,480,60);Capture(batch,mesh,material);
    Require(h.models.Prepare(h.frame,h.level,batch)==0,"basis telemetry prepares partial source");
    BeginMaterialSnapshot(directory);
    // Earlier source descriptors can exhaust NORMAL_MAPPING without losing execution evidence.
    for(unsigned i=0;i<300;++i){char id[48];snprintf(id,sizeof(id),"early_%u",i);RenderSnapshot_Record("NORMAL_MAPPING",id,"source","EARLY_DESCRIPTOR");}
    RenderSnapshot_Pass("GBUFFER");Require(h.Draw(batch,KichR4ModelSession::Geometry)==0,"partially valid basis still draws");
    RenderSnapshot_End("COMPLETE");const std::string report=MaterialReport();
    const size_t start=report.find("[RECORD MODEL_MATERIAL_EXECUTION]");Require(start!=std::string::npos,"model execution record kept independently");
    const std::string record=report.substr(start,report.find("\n\n",start)-start);
    Require(record.find("basis_valid_index_references=2")!=std::string::npos && record.find("basis_missing_index_references=1")!=std::string::npos,"partial TBN counted only over actual referenced range");
    Require(record.find("basis_mesh_vertices=4")!=std::string::npos && record.find("basis_mesh_derived_vertices=1")!=std::string::npos && record.find("basis_resource=source_actor")!=std::string::npos,"whole-source derivation counts and source name are explicit");
    // P0-S11: a triangle with any basis vertex stays mapped; the pixel shader
    // falls back only where the interpolated tangent vanishes.
    Require(record.find("basis_scope=LAST_BOUND_COMMAND;")!=std::string::npos && record.find("bump_missing_valid_source_basis")==std::string::npos,"shared material record describes last command without pretending to cover all meshes");
}
void MixedBasis(){Harness h;Require(h.Start()==0,"mixed basis setup");auto mesh=MaterialMesh();const auto material=CutoutMaterial();SeedTextures(h,material);
    for(unsigned i=0;i<3;++i)mesh->vertices.push_back(mesh->vertices[i]);
    // P0-S11: the geometric subset holds triangles without any basis vertex.
    for(unsigned i=3;i<6;++i)mesh->vertices[i].attributes&=~kich_r4_material::ATTR_TANGENT;mesh->indices={0,1,2,3,4,5};
    kich_r4_capture::Batch batch;batch.Begin(640,480,90);const auto identity=kich_r4_scene::Identity();const kich_r4_scene::Matrix bones[]={identity,identity};kich_r4_capture::Style style;
    batch.NativeModel(mesh.get(),mesh,identity,identity,bones,2,6,0,2,style,KICH_R4_SKIN_HARDWARE,"mixed_actor",NULL,NULL,NULL,0,0,0,mesh.get(),&material);
    Require(h.models.Prepare(h.frame,h.level,batch)==0 && h.Draw(batch,KichR4ModelSession::Geometry)==0,"mixed model prepares and draws");
    Require(model_draws.size()==2 && model_draws[0].surface.state.draw.index_count==3 && model_draws[1].surface.state.draw.index_count==3,"two index groups, no per-triangle draw explosion");
    Require((uint32_t(constants_drawn[0].material[0])&1) && !(uint32_t(constants_drawn[1].material[0])&1),"good triangle retains real normal maps while bad triangle uses geometric normal");
    Require((uint32_t(constants_drawn[1].material[0])&kich_r4_material::SHADER_GLOSS_MAP) && model_draws[1].surface.textures[1]==model_draws[0].surface.textures[1] && !model_draws[1].surface.textures[2],"geometric subset keeps the same authored bump gloss");
    const auto& gpu=h.models.meshes.at(mesh.get());Require(gpu.index_bytes==24 && gpu.vertex_bytes==mesh->vertices.size()*sizeof(kich_r4_capture::SourceVertex),"partition indices accounted without CPU-expanded vertices");
    const size_t before=model_draws.size();Require(h.Draw(batch,KichR4ModelSession::Shadow)==0 && model_draws.size()==before+1 && model_draws.back().surface.state.draw.index_buffer==gpu.ib && model_draws.back().surface.state.draw.index_count==6,"shadow uses original range exactly once");
}
void DrawBasisAndExposure(){Harness h;Require(h.Start()==0,"object exposure setup");auto mesh=MaterialMesh();const auto material=CutoutMaterial();SeedTextures(h,material);
    mesh->vertices.push_back({});mesh->available_attributes=0; // unrelated unused vertex cannot poison this range
    kich_r4_capture::Batch batch;batch.Begin(640,480,50);Capture(batch,mesh,material);
    Require(batch.native_models[0].available_attributes==15,"model attributes come from referenced draw indices");
    auto& cmd=batch.native_models[0];cmd.object_light_valid=true;cmd.object_hemi=.2f;cmd.object_sun=0;
    Require(h.models.Prepare(h.frame,h.level,batch)==0 && h.Draw(batch,KichR4ModelSession::Geometry)==0,"object exposure draw");
    const auto& c=constants_drawn.back();Require(c.color_bias[3]==1 && c.color_scale[3]==.2f && c.sun_scale_bias[2]==0,"raw object visibility reaches model shader without weather multiplication");
    Require((uint32_t(c.material[0])&33)==33,"valid bump and object sun mask coexist");
}
void HUDMaterialLighting(const char* directory){Harness h;Require(h.Start()==0,"HUD material setup");auto mesh=MaterialMesh();auto material=CutoutMaterial();
    material.surface=KichR4DefaultSurface();material.fog_enabled=true;
    material.surface.samplers[1]={1,1,1,3,2,2,8,0,0,0};material.surface.samplers[2]={3,3,3,2,2,1,1,0,0,0};SeedTextures(h,material);
    kich_r4_capture::Batch batch;batch.Begin(640,480,160);Capture(batch,mesh,material);auto& command=batch.native_models[0];
    command.layer=1;command.style.min_z=0;command.style.max_z=.02f;command.object_light_valid=true;command.object_hemi=.2f;command.object_sun=0;
    command.world.m[12]=3;command.vp.m[0]=2;h.base.sun_color[0]=.4f;h.base.sun_color[1]=.5f;h.base.sun_color[2]=.6f;
    h.base.light_position[3]=10;h.base.light_color[3]=1;h.base.fog_params[2]=1;
    KichR4Handle maps[6];for(unsigned i=0;i<6;++i){maps[i]=f->Add(KICH_R4_TEXTURE_2D);h.base.shadow[i]=kich_r4_scene::Identity();h.base.shadow[i].m[12]=float(i+1);}
    kich_r4_record::Request(true);kich_r4_record::BeginFrame(160);
    Require(h.models.Prepare(h.frame,h.level,batch)==0,"HUD immutable source prepared");const unsigned pose_uploads=uploads;
    BeginMaterialSnapshot(directory);
    Require(h.models.Draw(h.frame,h.level,h.cb,h.base,batch,KichR4ModelSession::HUD,1,"hud_test",maps)==0,"HUD base lit draw");
    const auto base_draw=model_draws.back();const auto base=constants_drawn.back();
    Require(base_draw.material_textures[0]==h.level.textures[material.bump] && base_draw.material_textures[1]==h.level.textures[material.bump_aux] && uint32_t(base.material[0])==1025,"HUD normal and gloss use separate actual bump and auxiliary inputs");
    Require(!memcmp(base_draw.material_samplers,material.surface.samplers+1,sizeof(base_draw.material_samplers)),"HUD preserves authored normal-map sampler states");
    for(unsigned i=0;i<6;++i)Require(base_draw.surface.textures[i+2]==maps[i],"HUD material never aliases sun or local shadow texture slots");
    Require(base.color_bias[3]==1 && base.color_scale[3]==.2f && base.sun_scale_bias[2]==0,"HUD carries measured owner exposure");
    Require(base.sun_color[0]==.4f && base.sun_color[1]==.5f && base.sun_color[2]==.6f,"source opaque HUD keeps current PCF sunlight without multiplying ROS sun zero");
    Require(base.world.m[12]==3 && base.mvp.m[0]==2 && base.fog_params[2]==0,"HUD lighting world and separate projection survive while world fog stays disabled");
    Require(base_draw.surface.state.max_depth==.02f,"HUD near depth contract remains unchanged");
    Require(h.models.hud_recorded_draws==1 && h.models.hud_exposure_valid_draws==1 && h.models.hud_mapped_draws==1 && !h.models.hud_unlit_draws && h.models.hud_hemi_min==.2f && h.models.hud_hemi_max==.2f,"bounded HUD report aggregates actual successful base pass exposure and map bindings");
    Require(h.models.Draw(h.frame,h.level,h.cb,h.base,batch,KichR4ModelSession::HUDAdd,1,"hud_local_test",maps)==0,"HUD point light additive draw");
    const auto local_draw=model_draws.back();const auto local=constants_drawn.back();
    for(unsigned i=0;i<6;++i){Require(local_draw.surface.textures[i+2]==maps[i],"all six point-light shadow faces coexist with HUD normals");Require(local.shadow[i].m[12]==float(i+1),"HUD retains actual world-space local projection matrices");}
    Require(local_draw.material_textures[0]==base_draw.material_textures[0] && local_draw.material_textures[1]==base_draw.material_textures[1] && local_draw.surface.state.source_blend==2 && local_draw.surface.state.dest_blend==2,"HUD local contribution uses same material and additive state");
    Require(uploads==pose_uploads && model_draws.size()==2,"HUD base and local light reuse pose and introduce no material-only draw");
    Require(h.models.hud_recorded_draws==1 && h.models.draw_calls[KichR4ModelSession::HUDAdd]==1,"HUD local additions do not inflate base visual-part diagnostics");
    h.base.light_color[3]=2;h.base.sun_direction[3]=1;
    Require(h.models.Draw(h.frame,h.level,h.cb,h.base,batch,KichR4ModelSession::HUDAdd,1,"hud_spot_test",maps)==0,"HUD spot projector draw");
    const auto spot=model_draws.back();Require(spot.surface.textures[3]==maps[1] && spot.surface.state.surface.samplers[3].u==3 && spot.surface.state.surface.samplers[3].mip_filter==0,"HUD spot cookie retains its own clamp/no-mip sampler");
    Require(spot.material_textures[0]==base_draw.material_textures[0] && !memcmp(spot.material_samplers,base_draw.material_samplers,sizeof(spot.material_samplers)),"spot projector does not override HUD normal resources or samplers");
    RenderSnapshot_End("COMPLETE");Require(MaterialReport().find("[RECORD HUD_MATERIAL_EXECUTION]")!=std::string::npos && MaterialReport().find("SOC_R2_BUMP; WORLD_TBN; MODEL_T8_T9")!=std::string::npos,"snapshot identifies actual HUD material execution rather than claiming pixel luminance");
    command.material.semantic=kich_r4_material::BLENDED;command.material.passes=kich_r4_material::PASS_FORWARD;
    Require(h.Draw(batch,KichR4ModelSession::HUD)==0 && constants_drawn.back().sun_color[0]==0,"genuine source forward HUD preserves its ROS sun policy");
    command.effect=1;const size_t before=model_draws.size();Require(h.Draw(batch,KichR4ModelSession::HUD)==0 && model_draws.size()==before+1 && !model_draws.back().material_textures[0],"authored unlit HUD does not gain normal-map lighting");
    Require(h.models.hud_unlit_draws==1 && h.models.hud_mapped_draws==2,"unlit HUD is reported distinctly from mapped lighting");
    kich_r4_record::Request(false);kich_r4_record::BeginFrame(161);h.models.ResetFrame();
    Require(!h.models.hud_recorded_draws && !h.models.hud_exposure_valid_draws && !h.models.hud_mapped_draws,"new frame cannot replay previous HUD diagnostic counts");
}
void HUDMaterialFallbacks(){for(unsigned mode=0;mode<2;++mode){Harness h;Require(h.Start()==0,"HUD fallback setup");auto mesh=MaterialMesh();const auto material=CutoutMaterial();SeedTextures(h,material);
    if(mode==0)for(auto& v:mesh->vertices)v.attributes&=~kich_r4_material::ATTR_TANGENT;
    else{h.level.textures[material.bump]=h.level.checker;h.level.texture_registry.entries[kich_r4_texture::Key(material.bump)].handle=h.level.checker;}
    kich_r4_capture::Batch batch;batch.Begin(640,480,180+mode);Capture(batch,mesh,material);batch.native_models[0].layer=1;
    Require(h.models.Prepare(h.frame,h.level,batch)==0 && h.Draw(batch,KichR4ModelSession::HUD)==0,"missing HUD optional basis/resource retains draw");
    const auto& d=model_draws.back();
    // P0-S11: an invalid basis keeps the bump at t8 for gloss only; a checker bump binds nothing.
    if(mode==0)Require(d.material_textures[0]==h.level.textures.at(material.bump) && !d.material_textures[1] &&
        constants_drawn.back().material[0]==float(kich_r4_material::SHADER_GLOSS_MAP|kich_r4_material::SHADER_MODEL_MAPS),"HUD without a basis keeps gloss, never normal mapping");
    else Require(!d.material_textures[0] && !d.material_textures[1] && constants_drawn.back().material[0]==0,"HUD fallback clears both material slots and flags, never decodes checker as normal");
    Require(!h.models.hud_recorded_draws,"record off skips HUD numeric diagnostic collection");
}}
void WatchActualBindings(){Harness h;Require(h.Start()==0,"watch production model setup");auto mesh=MaterialMesh();const auto material=CutoutMaterial();SeedTextures(h,material);
    kich_r4_capture::Batch batch;batch.Begin(640,480,130);Capture(batch,mesh,material);auto& target=batch.native_models[0];target.lifecycle_serial=500;
    // The recording fixture bypasses OGF Decode/BuildBounds; supply its known triangle box.
    target.bounds_known=target.bounds.valid=true;target.bounds.min[0]=target.bounds.min[1]=0;target.bounds.max[0]=target.bounds.max[1]=.5f;target.bounds.min[2]=target.bounds.max[2]=.5f;
    auto& watch=kich_r4_watch::Global();kich_r4_record::Request(true);kich_r4_record::BeginFrame(130);watch.Request(true);
    const float camera[]={.1f,.1f,0},direction[]={0,0,1};watch.Begin(130,1000,1,batch,camera,direction);Require(watch.Active(),"watch locks actual captured model command");
    Require(h.models.Prepare(h.frame,h.level,batch)==0 && h.Draw(batch,KichR4ModelSession::Geometry)==0,"watched model uses production draw");
    const auto& recorded=watch.Latest().passes[KichR4ModelSession::Geometry];Require(recorded.observed && recorded.draws==1,"production hook records successful draw without synthetic observation");
    for(unsigned i=0;i<6;++i)Require(recorded.handles[i]==model_draws.back().surface.textures[i],"watch records exact production binding handles");
    Require(recorded.flags==uint32_t(constants_drawn.back().material[0]),"watch records exact production material flags");
    kich_r4_shadow_record::Pass shadow;{kich_r4_shadow_record::Scope scope(&shadow);Require(h.Draw(batch,KichR4ModelSession::Shadow)==0,"shadow recording production hook");}
    Require(shadow.work[kich_r4_shadow_record::Model].draws==1 && shadow.work[kich_r4_shadow_record::Model].casters==1,"shadow counts one successful caster model");
    watch.Finish(constants_drawn.front());watch.Reset();kich_r4_record::Request(false);kich_r4_record::BeginFrame(131);
}
}
int main(int argc,char** argv){try{Require(argc==2,"snapshot output directory");MaterialCaptureOwnership();MaterialBindingsAndRouting(argv[1]);MaterialFallbacks(argv[1]);BasisFallbackTelemetry(argv[1]);RepairedModelBindsBump(argv[1]);DrawBasisAndExposure();MixedBasis();WatchActualBindings();HUDMaterialLighting(argv[1]);HUDMaterialFallbacks();
    printf("G3c native model material integration: %d checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
