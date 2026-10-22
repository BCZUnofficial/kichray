#include "../../xr_3da/RenderSnapshot.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <stdio.h>
#include <string.h>
unsigned checks=0;
void Require(bool good,const char* why){++checks;if(!good)throw std::runtime_error(why);}
std::string Read(const char* path){std::ifstream file(path);std::ostringstream out;out<<file.rdbuf();return out.str();}
std::string Section(const std::string& report,const std::string& name){const std::string header="["+name+"]\n";const size_t begin=report.find(header);
    if(begin==std::string::npos)throw std::runtime_error("missing section: "+name);
    const size_t end=report.find("\n[",begin+header.size());return report.substr(begin,end==std::string::npos?end:end-begin);}
uint64_t Number(const std::string& section,const std::string& key){const std::string field="\n"+key+"=";const size_t begin=section.find(field);
    if(begin==std::string::npos)throw std::runtime_error("missing numeric field: "+key);
    return std::stoull(section.substr(begin+field.size()));}
std::vector<std::string> DrawAggregates(const std::string& report){std::vector<std::string> result;size_t at=0;
    while((at=report.find("[DRAW_AGGREGATE ",at))!=std::string::npos){const size_t end=report.find("\n[",at+1);result.push_back(report.substr(at,end==std::string::npos?end:end-at));at=end==std::string::npos?report.size():end;}
    return result;}
void ReconcileDrawAggregates(const std::string& report){const std::vector<std::string> groups=DrawAggregates(report);const std::string total=Section(report,"DRAW_AGGREGATE_TOTALS");
    const char* keys[]={"draw_records_observed","submitted_draw_calls","skipped_draw_calls","triangles_submitted","instances_observed","instances_submitted","instances_skipped"};
    for(unsigned k=0;k<sizeof(keys)/sizeof(keys[0]);++k){uint64_t sum=0;for(size_t i=0;i<groups.size();++i)sum+=Number(groups[i],keys[k]);Require(sum==Number(total,keys[k]),"bounded draw route totals reconcile with all source callbacks");}
    Require(Number(total,"draw_records_observed")==Number(Section(report,"TRACE_LOSS"),"draw_records_observed"),"aggregate total covers callbacks lost from detailed trace");
    for(size_t i=0;i<groups.size();++i){const std::string& g=groups[i];uint64_t samples=Number(g,"representative_observations_omitted");
        for(unsigned j=0;j<Number(g,"representative_samples");++j)samples+=Number(g,"sample"+std::to_string(j)+"_observations");
        Require(samples==Number(g,"draw_records_observed") && Number(g,"representative_samples")<=4,"representative counts are bounded and explicitly account for omitted observations");}
}
int main(int argc,char** argv){try{Require(argc==2,"output directory");
    RenderSnapshotInfo info={};info.frame=77;info.width=1920;info.height=1080;info.position[0]=123.125f;info.fov=75;
    strcpy(info.renderer,"R4");strcpy(info.api,"D3D11");strcpy(info.scene,"GAME");strcpy(info.level,"l01_escape");strncpy(info.directory,argv[1],sizeof(info.directory)-1);
    RenderSnapshot_Begin(info);Require(!g_render_snapshot_active && !RenderSnapshot_Wanted(),"ordinary frame does not capture");
    Require(RenderSnapshot_Request() && RenderSnapshot_Request(),"two requests queued independently");
    RenderSnapshot_Begin(info);Require(g_render_snapshot_active,"begins at complete frame boundary");
    RenderSnapshot_Pass("GBUFFER");RenderSnapshot_Context("DETAIL","mesh_0","details\\set");RenderSnapshot_Skin("NONE");RenderSnapshot_Texture(0,"grass");
    RenderSnapshot_ShaderStage(true,"VSDetail");RenderSnapshot_ShaderStage(false,"PSGeometry");
    RenderSnapshotDraw draw={};draw.struct_size=sizeof(draw);draw.submitted=1;draw.topology=4;draw.index_count=6;draw.instance_count=7;
    RenderSnapshot_Draw(&draw);RenderSnapshot_Value("DETAIL_RENDERER","legacy_capture_vertices",0);
    RenderSnapshot_Note("CULLED","STATIC","mesh_1","FRUSTUM");RenderSnapshot_End("COMPLETE");
    const std::string path=RenderSnapshot_LastPath(),first=Read(path.c_str());Require(!g_render_snapshot_active,"stops after exactly one frame");
    Require(first.find("frame=77")!=std::string::npos && first.find("triangles_submitted=14")!=std::string::npos,"identified frame, instanced triangle count");
    Require(first.find("category=DETAIL")!=std::string::npos && first.find("vertex_shader=VSDetail")!=std::string::npos && first.find("texture0=grass")!=std::string::npos,"material and resource provenance");
    Require(first.find("status=COMPLETE")!=std::string::npos && first.find("visible_objects=UNKNOWN")!=std::string::npos && first.find("gpu_ms=NOT_INSTRUMENTED")!=std::string::npos,"does not invent visibility or GPU timing");
    RenderSnapshot_Draw(&draw);Require(Read(path.c_str())==first,"ordinary frames cannot append");
    info.frame=78;strcpy(info.renderer,"R2");strcpy(info.api,"D3D9");strcpy(info.scene,"MENU");RenderSnapshot_Begin(info);
    RenderSnapshot_End("COMPLETE");Require(std::string(RenderSnapshot_LastPath())!=path && Read(path.c_str())==first,"exclusive filename never overwrites prior snapshot");
    const std::string second=Read(RenderSnapshot_LastPath());Require(second.find("frame=78")!=std::string::npos && second.find("renderer=R2")!=std::string::npos && second.find("scene=MENU")!=std::string::npos,"same schema for menu and R2");
    Require(Section(second,"DETAIL_RENDERER").find("measurement_scope=")!=std::string::npos && Section(second,"MEMORY").find("measurement_scope=")!=std::string::npos,"uninstrumented R2/menu reports retain truthful mandatory summary sections");
    Require(!RenderSnapshot_Wanted(),"queued requests consumed one per frame");
    RenderSnapshot_Request();RenderSnapshot_Begin(info);for(unsigned i=0;i<40000;++i)RenderSnapshot_Draw(&draw);RenderSnapshot_End("COMPLETE");
    const std::string overflow=Read(RenderSnapshot_LastPath());Require(overflow.find("status=INCOMPLETE")!=std::string::npos && overflow.find("overflow=1")!=std::string::npos && overflow.size()<9u*1024u*1024u,"bounded capture reports overflow");
    RenderSnapshot_Request();RenderSnapshot_Begin(info);RenderSnapshot_Pass("SUN_SHADOW");RenderSnapshot_Light("sun","DIRECTIONAL");RenderSnapshot_Phase("cascade_0");
    for(unsigned i=0;i<40000;++i){const std::string name="mesh_"+std::to_string(i);RenderSnapshot_Note("CANDIDATE","STATIC",name.c_str(),"FRUSTUM");RenderSnapshot_Note("CULLED","STATIC",name.c_str(),"FRUSTUM");}
    RenderSnapshot_Context("STATIC","survivor","material");RenderSnapshot_Draw(&draw);
    RenderSnapshot_Value("DETAIL_RENDERER","visible_instances",1234);RenderSnapshot_Value("MEMORY","cpu_capture_bytes",4567);RenderSnapshot_End("COMPLETE");
    const std::string flood=Read(RenderSnapshot_LastPath()),flood_group=Section(flood,"PASS_LIGHT"),flood_loss=Section(flood,"TRACE_LOSS");
    Require(Section(flood,"SNAPSHOT").find("\nschema=3\n")!=std::string::npos && Section(flood,"SNAPSHOT").find("\ncomparable_schema=2\n")!=std::string::npos && flood.find("[VISIBILITY_EVENT]")==std::string::npos,"schema three retains comparable schema two visibility aggregation");
    Require(Number(Section(flood,"VISIBILITY_TOTALS"),"source_note_events")==80000 && Number(flood_group,"candidates")==40000 && Number(flood_group,"culled")==40000,"source counters remain exact through visibility flood");
    Require(Number(flood_group,"submitted_draw_calls")==1 && flood_group.find("selected=NOT_INSTRUMENTED")!=std::string::npos,"submission callback is distinct from unspecified selection observations");
    Require(Number(flood_loss,"draw_records_retained")==1 && Number(flood_loss,"draw_records_dropped")==0 && flood.find("status=COMPLETE")!=std::string::npos,"deliberate resource sampling never marks trace overflow");
    Require(Number(Section(flood,"VISIBILITY_SUMMARY"),"resource_samples")==4 && flood.size()<20000,"visibility resource sampling is independently bounded");
    Require(Number(Section(flood,"DETAIL_RENDERER"),"visible_instances")==1234 && Number(Section(flood,"MEMORY"),"cpu_capture_bytes")==4567,"late critical summaries survive visibility flood");
    Require(flood_group.find("light_id=sun")!=std::string::npos && flood_group.find("phase=cascade_0")!=std::string::npos,"object context preserves active light and phase");
    RenderSnapshot_Request();RenderSnapshot_Begin(info);RenderSnapshot_Pass("GBUFFER");
    RenderSnapshot_Note("CANDIDATE_CLASSIFIED","STATIC","selected_mesh","FRUSTUM");RenderSnapshot_Note("SELECTED","STATIC","selected_mesh","FRUSTUM");RenderSnapshot_End("COMPLETE");
    const std::string selected_group=Section(Read(RenderSnapshot_LastPath()),"PASS_LIGHT");
    Require(Number(selected_group,"candidates")==1 && Number(selected_group,"classified_candidates")==1 && Number(selected_group,"selected")==1 && Number(selected_group,"culled")==0,"fully selected classified population reports known zero culls");
    RenderSnapshot_Request();RenderSnapshot_Begin(info);RenderSnapshot_Pass("GBUFFER");
    RenderSnapshot_Note("CANDIDATE_CLASSIFIED","STATIC","culled_mesh","FRUSTUM");RenderSnapshot_Note("CULLED","STATIC","culled_mesh","FRUSTUM");RenderSnapshot_End("COMPLETE");
    const std::string culled_group=Section(Read(RenderSnapshot_LastPath()),"PASS_LIGHT");
    Require(Number(culled_group,"candidates")==1 && Number(culled_group,"classified_candidates")==1 && Number(culled_group,"culled")==1 && Number(culled_group,"selected")==0,"fully culled classified population reports known zero selections");

    RenderSnapshot_Request();RenderSnapshot_Begin(info);RenderSnapshot_Pass("SUN_SHADOW");
    for(unsigned i=0;i<10000;++i)RenderSnapshot_Draw(&draw);
    const char* late_passes[]={"FORWARD_TRANSPARENCY","HUD_UI","POSTPROCESS","MENU_UI","DISTORTION"};
    for(unsigned i=0;i<5;++i){RenderSnapshot_Pass(late_passes[i]);RenderSnapshot_Draw(&draw);}
    RenderSnapshot_Value("DETAIL_RENDERER","legacy_capture_vertices",0);RenderSnapshot_Value("MEMORY","registered_gpu_payload_bytes",999);RenderSnapshot_End("COMPLETE");
    const std::string partitioned=Read(RenderSnapshot_LastPath()),loss=Section(partitioned,"TRACE_LOSS"),sun=Section(partitioned,"PASS SUN_SHADOW");
    Require(Number(loss,"draw_records_observed")==10005 && Number(loss,"draw_records_retained")+Number(loss,"draw_records_dropped")==10005,"observed draw callbacks equal retained plus dropped after saturation");
    Require(Number(loss,"draw_bytes_observed")==Number(loss,"draw_bytes_retained")+Number(loss,"draw_bytes_dropped"),"serialized byte loss is explicit and conserved");
    Require(Number(sun,"draw_calls")==10000 && Number(sun,"draw_records_dropped")>0 && Number(sun,"triangles")==140000,"saturated shadow pass retains exact aggregate counts");
    Require(loss.find("first_lost_pass=SUN_SHADOW")!=std::string::npos && Number(sun,"last_lost_draw_event")==10000,"loss identifies its pass and event interval");
    for(unsigned i=0;i<5;++i)Require(Number(Section(partitioned,std::string("PASS ")+late_passes[i]),"draw_records_retained")==1,"late pass has reserved draw capacity");
    Require(Number(Section(partitioned,"DETAIL_RENDERER"),"legacy_capture_vertices")==0 && Number(Section(partitioned,"MEMORY"),"registered_gpu_payload_bytes")==999,"critical summaries survive draw saturation");
    ReconcileDrawAggregates(partitioned);
    Require(Number(Section(partitioned,"DRAW_AGGREGATE_TOTALS"),"instances_submitted")==70035 && Number(Section(partitioned,"DRAW_AGGREGATE_TOTALS"),"triangles_submitted")==140070,"route summaries count all submitted instances after verbose trace quota");

    RenderSnapshot_Request();RenderSnapshot_Begin(info);RenderSnapshot_Pass("FORWARD_TRANSPARENCY");RenderSnapshot_Context("MODEL","flood","opaque");
    for(unsigned i=0;i<4000;++i)RenderSnapshot_Draw(&draw);
    RenderSnapshot_Context("SORTED_TRANSPARENT","static\\fire_distort","particles\\xdistort");
    RenderSnapshot_Object("PARTICLE","mesh:fire","material:fire","SOURCE_TYPE",RENDER_SNAPSHOT_PARTICLE);RenderSnapshot_Queue("SORTED_TRANSPARENT");
    RenderSnapshot_PassMask(RENDER_SNAPSHOT_PASS_FORWARD_TRANSPARENCY|RENDER_SNAPSHOT_PASS_DISTORTION);
    const float fire_uv[]={0.f,0.f,1.f,1.f};RenderSnapshot_UV(fire_uv,2,"SOURCE_VERTEX");
    RenderSnapshotDraw fire=draw;strcpy(fire.vertex_shader,"VSModel");strcpy(fire.pixel_shader,"PSPlain");strcpy(fire.texture_names[0],"pfx\\pfx_ani-fire01");
    RenderSnapshot_Draw(&fire);strcpy(fire.pixel_shader,"PSDistortion");strcpy(fire.texture_names[0],"pfx\\pfx_distortion");RenderSnapshot_Draw(&fire);
    RenderSnapshot_Pass("DISTORTION");RenderSnapshot_Context("DISTORTION","static\\fire_distort","particles\\xdistort");
    RenderSnapshot_Object("PARTICLE","mesh:fire","material:fire","SOURCE_TYPE",RENDER_SNAPSHOT_PARTICLE);RenderSnapshot_Queue("DISTORTION");
    RenderSnapshot_PassMask(RENDER_SNAPSHOT_PASS_DISTORTION);RenderSnapshot_UV(fire_uv,2,"SOURCE_VERTEX");RenderSnapshot_Draw(&fire);RenderSnapshot_End("COMPLETE");
    const std::string fire_report=Read(RenderSnapshot_LastPath());const std::vector<std::string> fire_groups=DrawAggregates(fire_report);
    Require(fire_groups.size()==4 && Number(Section(fire_report,"PASS FORWARD_TRANSPARENCY"),"draw_records_dropped")>0,"actual fire shader and texture routes stay distinct after detailed forward quota");
    unsigned fire_routes=0;for(size_t i=0;i<fire_groups.size();++i){const std::string& g=fire_groups[i];if(g.find("material_id=material:fire\n")==std::string::npos)continue;
        ++fire_routes;Require(g.find("semantic_type=PARTICLE\n")!=std::string::npos && g.find("sample0_resource=static\\fire_distort\n")!=std::string::npos && g.find("sample0_uv1=1,1\n")!=std::string::npos,"aggregate retains semantic resource and original UV evidence for rare late material");
        if(g.find("pass=DISTORTION\n")!=std::string::npos)Require(Number(g,"pass_mask")==512 && g.find("pixel_shader=PSDistortion\n")!=std::string::npos,"distortion is a separately identifiable reserved pass");}
    Require(fire_routes==3 && Number(Section(fire_report,"PASS DISTORTION"),"draw_records_retained")==1,"distortion survives early forward trace saturation");
    ReconcileDrawAggregates(fire_report);

    RenderSnapshot_Request();RenderSnapshot_Begin(info);RenderSnapshot_Pass("GBUFFER");RenderSnapshotDraw model=draw;model.instance_count=5;
    strcpy(model.vertex_shader,"VSModelGPU");strcpy(model.pixel_shader,"PSGeometry");strcpy(model.skin,"GPU_1W_2W");strcpy(model.texture_names[0],"actors\\stalker");
    for(unsigned i=0;i<13;++i){const std::string mesh="model:"+std::to_string(i),name="actor_resource_"+std::to_string(i);
        RenderSnapshot_Context("MODEL",name.c_str(),"models\\model");RenderSnapshot_Object("SKINNED_MODEL",mesh.c_str(),"material:actor","SOURCE_TYPE",0);
        RenderSnapshot_Queue("GBUFFER");RenderSnapshot_PassMask(RENDER_SNAPSHOT_PASS_GBUFFER);RenderSnapshot_UV(fire_uv,2,"MODEL_VERTEX");
        model.start_index=i*6;model.submitted=i<9?1:0;model.vb=i+1;model.ib=i+2;RenderSnapshot_Draw(&model);}
    RenderSnapshot_End("COMPLETE");const std::string model_report=Read(RenderSnapshot_LastPath());const std::vector<std::string> model_groups=DrawAggregates(model_report);
    Require(model_groups.size()==1,"mesh resource and transient GPU handle identity never multiply material routes");const std::string& model_group=model_groups[0];
    Require(Number(model_group,"draw_records_observed")==13 && Number(model_group,"submitted_draw_calls")==9 && Number(model_group,"skipped_draw_calls")==4 && Number(model_group,"triangles_submitted")==90,"model summary preserves submitted and rejected triangle totals");
    Require(Number(model_group,"instances_observed")==65 && Number(model_group,"instances_submitted")==45 && Number(model_group,"instances_skipped")==20,"model instance totals distinguish attempted and submitted work");
    Require(Number(model_group,"representative_samples")==4 && Number(model_group,"representative_observations_omitted")==9 && Number(model_group,"first_draw_event")==1 && Number(model_group,"last_draw_event")==13,"model representatives are bounded without truncating observed event interval");
    Require(model_group.find("semantic_type=SKINNED_MODEL\n")!=std::string::npos && model_group.find("skin_mode=GPU_1W_2W\n")!=std::string::npos && model_group.find("sample3_mesh_id=model:3\n")!=std::string::npos,"model route and representative source IDs remain reviewable");
    ReconcileDrawAggregates(model_report);

    RenderSnapshot_Request();RenderSnapshot_Begin(info);RenderSnapshot_Pass("SUN_SHADOW");
    for(unsigned i=0;i<700;++i){const std::string mat="material:"+std::to_string(i);RenderSnapshot_Context("MODEL","shadow_model",mat.c_str());
        RenderSnapshot_Object("SKINNED_MODEL","shadow_mesh",mat.c_str(),"SOURCE_TYPE",0);RenderSnapshot_Draw(&draw);}
    const char* aggregate_late[]={"FORWARD_TRANSPARENCY","DISTORTION","HUD_UI"};
    for(unsigned i=0;i<3;++i){RenderSnapshot_Pass(aggregate_late[i]);RenderSnapshot_Context("PARTICLE","static\\fire_distort","particles\\xdistort");
        RenderSnapshot_Object("PARTICLE","fire_mesh","material:fire","SOURCE_TYPE",RENDER_SNAPSHOT_PARTICLE);RenderSnapshot_Draw(&fire);}
    RenderSnapshot_End("COMPLETE");const std::string route_flood=Read(RenderSnapshot_LastPath()),route_totals=Section(route_flood,"DRAW_AGGREGATE_TOTALS");
    Require(Number(route_totals,"groups")==100 && Number(route_totals,"route_limit")==512 && Number(route_totals,"observations_coalesced")==604 && Number(route_totals,"observations_lost")==0,"material cardinality reaches explicit bounded OTHER bucket without losing global counts");
    Require(Number(route_totals,"draw_records_observed")==703 && Number(Section(route_flood,"TRACE_LOSS"),"aggregation_overflow")==1 && route_flood.size()<3u*1024u*1024u,"route coalescing marks incomplete attribution within bounded capture size");
    const std::vector<std::string> route_groups=DrawAggregates(route_flood);unsigned late_routes=0,other_routes=0;
    for(size_t i=0;i<route_groups.size();++i){const std::string& g=route_groups[i];if(Number(g,"coalesced")){++other_routes;Require(g.find("material_id=MULTIPLE\n")!=std::string::npos && Number(g,"draw_records_observed")==604,"OTHER route does not fabricate a single material identity");}
        else if(g.find("material_id=material:fire\n")!=std::string::npos)++late_routes;}
    Require(late_routes==3 && other_routes==1,"shadow cardinality cannot consume late forward distortion or HUD route reservations");ReconcileDrawAggregates(route_flood);

    RenderSnapshot_Request();RenderSnapshot_Begin(info);RenderSnapshot_Pass("GBUFFER");RenderSnapshot_Light("local_3","SPOT");RenderSnapshot_Phase("face_2");
    RenderSnapshot_Context("SORTED_TRANSPARENT","resource","material");RenderSnapshot_Object("PARTICLE","mesh:17","material:4","SOURCE_TYPE",RENDER_SNAPSHOT_PARTICLE|RENDER_SNAPSHOT_GLOW);
    RenderSnapshot_Queue("SORTED_TRANSPARENT");RenderSnapshot_PassMask(RENDER_SNAPSHOT_PASS_SUN_SHADOW|RENDER_SNAPSHOT_PASS_GBUFFER);
    const float uv[]={0.125f,0.25f,0.5f,0.75f,1.f,1.25f,1.5f,1.75f,2.f,2.25f};RenderSnapshot_UV(uv,5,"SOURCE_VERTEX");RenderSnapshot_DetailVariant(2);
    RenderSnapshotProvenance saved={};RenderSnapshot_GetProvenance(&saved);Require(saved.uv_count==4 && saved.pass_mask_known==1,"provenance API clamps bounded UV pairs");const RenderSnapshotProvenance original=saved;RenderSnapshot_Draw(&draw);
    RenderSnapshot_Context("STATIC","next","next_material");RenderSnapshot_GetProvenance(&saved);Require(saved.uv_count==0 && saved.flags==0 && !saved.pass_mask_known,"new object context clears provenance");
    RenderSnapshot_SetProvenance(&original);RenderSnapshot_GetProvenance(&saved);Require(saved.uv_count==4 && saved.flags==3 && !strcmp(saved.queue,"SORTED_TRANSPARENT"),"captured source provenance round trips without renderer ABI changes");
    RenderSnapshot_Object("STATIC","mesh:18","material:5","SOURCE_TYPE",0);RenderSnapshot_GetProvenance(&saved);Require(saved.uv_count==0 && saved.flags==0 && saved.pass_mask==5 && !strcmp(saved.queue,"SORTED_TRANSPARENT"),"object override preserves queue and pass mask while clearing stale source UV");
    RenderSnapshot_Context("STATIC","next","next_material");RenderSnapshot_Draw(&draw);
    RenderSnapshot_Pass("POSTPROCESS");RenderSnapshot_Draw(&draw);RenderSnapshot_End("COMPLETE");
    const std::string metadata=Read(RenderSnapshot_LastPath()),object=Section(metadata,"DRAW 000001"),next=Section(metadata,"DRAW 000002"),post=Section(metadata,"DRAW 000003");
    Require(object.find("semantic_type=PARTICLE")!=std::string::npos && object.find("render_queue=SORTED_TRANSPARENT")!=std::string::npos && object.find("mesh_id=mesh:17")!=std::string::npos && object.find("material_id=material:4")!=std::string::npos,"semantic type queue and stable source IDs are distinct");
    Require(object.find("is_particle=1")!=std::string::npos && object.find("is_glow=1")!=std::string::npos && object.find("pass_mask=5")!=std::string::npos && object.find("detail_variant_name=Wave2")!=std::string::npos,"source flags pass mask and detail variant serialize");
    Require(object.find("uv_sample3=1.5,1.75")!=std::string::npos && object.find("uv_sample4=")==std::string::npos,"UV output contains at most four pairs");
    Require(next.find("semantic_type=UNKNOWN")!=std::string::npos && next.find("detail_variant=NOT_INSTRUMENTED")!=std::string::npos && next.find("light_id=local_3")!=std::string::npos,"new object does not inherit metadata while active light survives");
    Require(next.find("is_particle=NOT_INSTRUMENTED")!=std::string::npos && next.find("is_glow=NOT_INSTRUMENTED")!=std::string::npos,"missing source provenance does not fabricate negative identity flags");
    Require(post.find("light_id=NONE")!=std::string::npos && post.find("phase=NONE")!=std::string::npos,"pass boundary resets light and phase");

    RenderSnapshot_Request();RenderSnapshot_Begin(info);const std::string large_value(2048,'x');
    for(unsigned i=0;i<1200;++i){const std::string key="field_"+std::to_string(i);RenderSnapshot_Text("CUSTOM",key.c_str(),large_value.c_str());}
    RenderSnapshot_Text("CUSTOM","field_0","UPDATED_AFTER_SATURATION");
    for(unsigned i=0;i<700;++i){const std::string id="light_"+std::to_string(i);RenderSnapshot_Light(id.c_str(),"POINT");RenderSnapshot_Note("CULLED","STATIC","sample","FRUSTUM");}
    for(unsigned i=0;i<100;++i){const std::string name="DYNAMIC_PASS_"+std::to_string(i);RenderSnapshot_Pass(name.c_str());RenderSnapshot_Draw(&draw);}
    RenderSnapshot_Value("DETAIL_RENDERER","visible_instances",42);RenderSnapshot_Value("MEMORY","cpu_capture_bytes",84);
    RenderSnapshot_Text("MODEL_RENDERER","path","NATIVE_DX11_GPU_SKINNING");RenderSnapshot_Value("MODEL_RENDERER","draw_calls",321);RenderSnapshot_End("COMPLETE");
    const std::string cardinality=Read(RenderSnapshot_LastPath()),card_loss=Section(cardinality,"TRACE_LOSS");
    Require(Number(card_loss,"summary_overflow")==1 && Number(card_loss,"aggregation_overflow")==1 && cardinality.size()<3u*1024u*1024u,"high cardinality summaries and aggregates have independent explicit bounds");
    Require(Number(Section(cardinality,"VISIBILITY_TOTALS"),"culled")==700 && Number(card_loss,"draw_records_observed")==100,"global source and callback totals survive aggregation saturation");
    Require(Number(Section(cardinality,"DETAIL_RENDERER"),"visible_instances")==42 && Number(Section(cardinality,"MEMORY"),"cpu_capture_bytes")==84,"generic summary saturation cannot consume protected fields");
    Require(Number(Section(cardinality,"MODEL_RENDERER"),"draw_calls")==321 && Section(cardinality,"MODEL_RENDERER").find("path=NATIVE_DX11_GPU_SKINNING")!=std::string::npos,"late model renderer summary has a dedicated reservation after generic saturation");
    Require(Section(cardinality,"CUSTOM").find("field_0=UPDATED_AFTER_SATURATION")!=std::string::npos,"existing summary keys remain updateable after cardinality or byte saturation");
    RenderSnapshot_Request();RenderSnapshot_Begin(info);RenderSnapshot_Draw(&draw);RenderSnapshot_End("COMPLETE");const std::string clean=Read(RenderSnapshot_LastPath()),clean_loss=Section(clean,"TRACE_LOSS");
    Require(Number(clean_loss,"draw_records_observed")==1 && Number(clean_loss,"trace_overflow")==0 && Number(clean_loss,"summary_overflow")==0 && Number(clean_loss,"aggregation_overflow")==0,"next capture resets all loss counters");
    Require(Section(clean,"DETAIL_RENDERER").find("visible_instances=")==std::string::npos && clean.find("[CUSTOM]")==std::string::npos && clean.find("[MODEL_RENDERER]")==std::string::npos && Number(Section(clean,"VISIBILITY_TOTALS"),"source_note_events")==0,"next capture does not retain metadata or visibility observations");
    Require(Section(clean,"PASS_LIGHT").find("candidates=NOT_INSTRUMENTED")!=std::string::npos,"draw callbacks never fabricate candidate counts");
    Require(Number(Section(clean,"DRAW_AGGREGATE_TOTALS"),"groups")==1 && Number(Section(clean,"DRAW_AGGREGATE_TOTALS"),"observations_coalesced")==0,"next capture clears route samples and coalescing counters");ReconcileDrawAggregates(clean);
    RenderSnapshot_Request();RenderSnapshot_Begin(info);RenderSnapshot_Text("TELEMETRY_LOSS","source_provenance","ALLOCATION_FAILED");RenderSnapshot_End("COMPLETE");
    const std::string source_loss=Read(RenderSnapshot_LastPath());Require(source_loss.find("status=INCOMPLETE")!=std::string::npos && Number(Section(source_loss,"TRACE_LOSS"),"source_telemetry_loss_events")==1,"explicit source telemetry failure cannot claim complete capture");
    RenderSnapshot_Request();RenderSnapshot_Begin(info);draw.submitted=0;RenderSnapshot_Draw(&draw);RenderSnapshot_End("COMPLETE");
    Require(Read(RenderSnapshot_LastPath()).find("status=INCOMPLETE")!=std::string::npos,"failed submission never reports a complete capture");
    strcpy(info.directory,"/does/not/exist/kich-snapshot");RenderSnapshot_Request();RenderSnapshot_Begin(info);RenderSnapshot_End("COMPLETE");
    Require(!g_render_snapshot_active && std::string(RenderSnapshot_LastPath()).find("ERROR:")==0,"I/O failure does not retain capture or throw into renderer");
    printf("G3b real snapshot recorder: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
