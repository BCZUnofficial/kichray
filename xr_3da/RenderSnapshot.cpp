/* One-shot diagnostic recorder. No engine, graphics API, GPU wait or worker. */
#include "RenderSnapshot.h"
#if !defined(_EDITOR)
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <chrono>
#include <string>
#include <map>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <mutex>
#if defined(_WIN32)
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif
bool g_render_snapshot_active=false;
bool g_render_snapshot_console=false;
namespace {
const size_t max_bytes=8u*1024u*1024u,max_events=32768;
const size_t max_passes=64,max_groups=512,max_visibility=512;
const size_t max_draw_aggregates=512;
unsigned pending=0;
std::atomic<bool> diagnostic_enabled(false),event_enabled(false);std::atomic<uint64_t> event_render(0);std::mutex event_mutex;
struct Request {uint64_t id,group,requested_us;char reason[64],role[40];bool profile;
    Request():id(0),group(0),requested_us(0),profile(false){reason[0]=role[0]=0;}};
Request requests[16],capture_request;
uint64_t next_request=0,next_group=0,render_tick=0,render_id=0,present_id=0,level_generation=0,device_generation=0;
uint64_t captured_render_id=0,captured_present_id=0,captured_level_generation=0,captured_device_generation=0,captured_us=0;
uint64_t request_rejections=0,aborted_requests=0;
uint32_t profile_remaining=0,hotkey=0;
bool frame_ids_known=false,present_id_known=false,captured_present_known=false,captured_present_submitted=false,have_last_info=false,event_only=false,frame_profile=false;
void RefreshDiagnostic();
RenderSnapshotInfo last_info={};
uint64_t EpochUS(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
uint64_t SteadyUS(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
const uint64_t session_id=EpochUS();
struct UIObservation {char screen[32],root[192],owner[96];bool loaded,console,paused;
    UIObservation():loaded(false),console(false),paused(false){strcpy(screen,"GAME");strcpy(root,"NONE");strcpy(owner,"NOT_INSTRUMENTED");}} ui;
struct UIWindow {char kind[32],id[192];};UIWindow ui_windows[16]={};unsigned ui_window_count=0;uint64_t ui_window_overflow=0;
struct UIEvent {uint64_t ordinal,render,when;char kind[32],subject[192],action[96],result[192];};
UIEvent ui_events[256]={};uint32_t ui_events_count=0;uint64_t ui_events_observed=0,ui_events_dropped=0;
struct Cycle {bool active,queued,baseline,opened;unsigned stage;uint64_t group,start_tick,start_us,start_steady_us,level,device;uint32_t timeout;
    char target[32],status[64],baseline_root[192],baseline_owner[96];bool baseline_pause;
    Cycle():active(false),queued(false),baseline(false),opened(false),stage(0),group(0),start_tick(0),start_us(0),start_steady_us(0),level(0),device(0),timeout(1800),baseline_pause(false){target[0]=baseline_root[0]=baseline_owner[0]=0;strcpy(status,"NOT_REQUESTED");}} cycle;
struct ClosedCapture {uint64_t id,engine,render;std::string path;bool timing_done;ClosedCapture():id(0),engine(0),render(0),timing_done(false){}} closed_captures[16];unsigned closed_cursor=0;
bool gpu_completed=false;std::string gpu_completion_status="NOT_INSTRUMENTED";uint64_t timing_append_failures=0;

bool incomplete=false,overflowed=false,summary_overflow=false,aggregation_overflow=false;
const char* failure=NULL;
uint64_t draws=0,triangles=0,events=0,draw_bytes=0,dropped_draws=0,dropped_bytes=0;
uint64_t first_lost=0,last_lost=0,serialization_failures=0,invalid_draws=0;
uint64_t summary_seen=0,summary_retained=0,summary_dropped=0,summary_omitted=0,visibility_seen=0;
uint64_t source_telemetry_losses=0;
std::string first_lost_pass,last_lost_pass;
RenderSnapshotInfo info={};
RenderSnapshotProvenance provenance={};
uint32_t detail_variant=~uint32_t(0);
std::string body,pass,category,resource,material,vertex_shader,pixel_shader,texture_names[8],last_path,skin;
std::string light_id="NONE",light_scope="NONE",phase="NONE";
struct Pass {uint64_t calls=0,triangles=0,observed=0,retained=0,dropped=0,bytes=0,retained_bytes=0,dropped_bytes=0,first_lost=0,last_lost=0;double cpu_ms=0;};
std::map<std::string,Pass> passes;
/* Dedicated R4 pass reservations. R2 aliases share their corresponding stage;
 * dynamic/unknown pass names share OTHER, never a late R4 pass reservation. */
struct Budget {const char* name;size_t limit;size_t bytes,events;};
Budget budgets[]={
    {"SUN_SHADOW",2u*1024u*1024u,0,0},{"LOCAL_SHADOW",2u*1024u*1024u,0,0},
    {"GBUFFER",2u*1024u*1024u,0,0},{"LIGHT_ACCUMULATION",256u*1024u,0,0},
    {"ENVIRONMENT_COMBINE",256u*1024u,0,0},{"FORWARD_TRANSPARENCY",256u*1024u,0,0},
    {"HUD_UI",256u*1024u,0,0},{"POSTPROCESS",256u*1024u,0,0},
    {"MENU_UI",256u*1024u,0,0},{"DISTORTION",256u*1024u,0,0},{"OTHER",256u*1024u,0,0}};
struct Summary {std::map<std::pair<std::string,std::string>,std::string> fields;size_t bytes=0;};
/* Critical model metrics have their own reservation, independent of per-light
 * and per-phase generic summary cardinality. */
Summary summaries[15];
const unsigned summary_count=sizeof(summaries)/sizeof(summaries[0]);
struct Observations {uint64_t candidates=0,classified_candidates=0,culled=0,selected=0,other=0;bool candidates_known=false,culled_known=false,selected_known=false;};
struct Group {std::string pass,light,scope,phase;Observations notes;uint64_t observed=0,submitted=0,skipped=0,triangles=0;};
struct Visibility {Group group;std::string category,reason;Observations notes;std::string samples[4];uint32_t samples_count=0;uint64_t samples_omitted=0;};
std::map<std::string,Group> groups;
std::map<std::string,Visibility> visibility;
/* G3b: a bounded route summary survives loss of verbose DRAW records. Mesh IDs
 * are representatives, never map keys. Pass reservations protect late routes. */
struct DrawTotals {uint64_t observed=0,submitted=0,skipped=0,triangles=0,instances=0,submitted_instances=0,skipped_instances=0,first=0,last=0;
    void Add(const RenderSnapshotDraw& d,uint64_t tris,uint64_t event){++observed;submitted+=d.submitted?1:0;skipped+=d.submitted?0:1;
        triangles+=d.submitted?tris:0;instances+=d.instance_count;submitted_instances+=d.submitted?d.instance_count:0;skipped_instances+=d.submitted?0:d.instance_count;
        if(!first){first=event;}last=event;}
};
struct DrawRoute {std::string pass,light,scope,phase,semantic,category,queue,material,material_route,vs,ps,skin,textures[8],states,samplers;
    uint32_t flags=0,mask=0,mask_known=0,variant=~uint32_t(0),topology=0;};
struct DrawSample {char mesh[160],resource[257],evidence[65],uv_source[49],material[160],semantic[32],vs[96],ps[96],texture0[128],geometry[64],widget[192],ui_origin[64];
    uint32_t uv_count=0,start_index=0,index_count=0;float uv[8];uint64_t first=0,last=0,observations=0;};
struct DrawAggregate {DrawRoute route;DrawTotals totals;DrawSample samples[4]={};uint32_t sample_count=0;uint64_t samples_omitted=0;bool coalesced=false;};
std::map<std::string,DrawAggregate> draw_aggregates;
const unsigned draw_aggregate_limits[]={96,96,128,32,16,32,32,16,16,32,16};
const unsigned draw_budget_count=sizeof(budgets)/sizeof(budgets[0]);
unsigned draw_aggregate_counts[draw_budget_count]={};
DrawTotals draw_aggregate_totals;
uint64_t draw_aggregate_failures=0,draw_aggregate_coalesced=0;
Observations note_totals;
std::chrono::steady_clock::time_point frame_start,pass_start;
std::string Clean(const char* text,size_t max=512){std::string out;if(!text)return "UNKNOWN";
    for(size_t i=0;i<max && text[i];++i){const unsigned char c=static_cast<unsigned char>(text[i]);out+=(c<32 || c==127)?' ':char(c);}return out;}
void Copy(char* out,size_t size,const char* text){if(!size)return;const std::string value=Clean(text,size-1);memcpy(out,value.data(),value.size());out[value.size()]=0;}
void ResetObject(){provenance=RenderSnapshotProvenance();detail_variant=~uint32_t(0);}
Budget& TraceBudget(const std::string& name){
    if(name=="SUN_SHADOW" || name=="SUN_SHADOW_AND_LIGHT" || name=="SUN_SHADOW_TRANSPARENT")return budgets[0];
    if(name=="LOCAL_SHADOW" || name=="LOCAL_SHADOW_AND_LIGHT" || name=="LOCAL_SHADOW_TRANSPARENT")return budgets[1];
    if(name=="GBUFFER" || name=="Z_PREFILL")return budgets[2];
    if(name=="COMBINE_POSTPROCESS")return budgets[7];
    for(size_t i=3;i+1<draw_budget_count;++i)if(name==budgets[i].name)return budgets[i];
    return budgets[draw_budget_count-1];
}
void FixedText(char* out,size_t size,const char* value){size_t n=0;if(!value)value="UNKNOWN";
    for(;n+1<size && value[n];++n){const unsigned char c=static_cast<unsigned char>(value[n]);out[n]=(c<32 || c==127)?' ':char(c);}out[n]=0;}
std::string DrawRouteKey(const DrawRoute& r){std::string key;const std::string* fields[]={&r.pass,&r.light,&r.scope,&r.phase,&r.semantic,&r.category,&r.queue,&r.material,&r.material_route,&r.vs,&r.ps,&r.skin,&r.states,&r.samplers};
    for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);++i)key+=std::to_string(fields[i]->size())+":"+*fields[i];
    for(unsigned i=0;i<8;++i)key+=std::to_string(r.textures[i].size())+":"+r.textures[i];
    return key+"/flags="+std::to_string(r.flags)+"/mask="+std::to_string(r.mask)+"/known="+std::to_string(r.mask_known)+"/variant="+std::to_string(r.variant)+"/topology="+std::to_string(r.topology);
}
void DrawRepresentative(DrawAggregate& aggregate,const DrawRoute& route,const RenderSnapshotDraw& d){DrawSample sample={};
    FixedText(sample.mesh,sizeof(sample.mesh),provenance.mesh_id[0]?provenance.mesh_id:"UNKNOWN");FixedText(sample.resource,sizeof(sample.resource),resource.c_str());
    FixedText(sample.evidence,sizeof(sample.evidence),provenance.evidence[0]?provenance.evidence:"NOT_INSTRUMENTED");
    FixedText(sample.uv_source,sizeof(sample.uv_source),provenance.uv_source[0]?provenance.uv_source:"NOT_INSTRUMENTED");
    FixedText(sample.material,sizeof(sample.material),route.material.c_str());FixedText(sample.semantic,sizeof(sample.semantic),route.semantic.c_str());
    FixedText(sample.vs,sizeof(sample.vs),route.vs.c_str());FixedText(sample.ps,sizeof(sample.ps),route.ps.c_str());FixedText(sample.texture0,sizeof(sample.texture0),route.textures[0].c_str());
    FixedText(sample.geometry,sizeof(sample.geometry),provenance.geometry_id[0]?provenance.geometry_id:"NOT_INSTRUMENTED");
    FixedText(sample.widget,sizeof(sample.widget),provenance.ui_widget[0]?provenance.ui_widget:"NOT_INSTRUMENTED");
    FixedText(sample.ui_origin,sizeof(sample.ui_origin),provenance.ui_origin[0]?provenance.ui_origin:"NOT_INSTRUMENTED");
    sample.uv_count=provenance.uv_count>4?4:provenance.uv_count;memcpy(sample.uv,provenance.uv,sample.uv_count*2*sizeof(float));
    sample.start_index=d.start_index;sample.index_count=d.index_count;
    for(unsigned i=0;i<aggregate.sample_count;++i){DrawSample& old=aggregate.samples[i];
        if(!strcmp(old.mesh,sample.mesh) && !strcmp(old.resource,sample.resource) && !strcmp(old.material,sample.material) && !strcmp(old.semantic,sample.semantic)
            && !strcmp(old.vs,sample.vs) && !strcmp(old.ps,sample.ps) && !strcmp(old.texture0,sample.texture0) && !strcmp(old.uv_source,sample.uv_source)
            && !strcmp(old.geometry,sample.geometry) && !strcmp(old.widget,sample.widget) && old.start_index==sample.start_index && old.index_count==sample.index_count && old.uv_count==sample.uv_count && !memcmp(old.uv,sample.uv,sample.uv_count*2*sizeof(float))){++old.observations;old.last=draws;return;}}
    if(aggregate.sample_count==4){++aggregate.samples_omitted;return;}
    sample.first=sample.last=draws;sample.observations=1;aggregate.samples[aggregate.sample_count++]=sample;
}
void ProtectedDraw(const DrawRoute& route,const RenderSnapshotDraw& d){
    const char* kind=route.pass=="DISTORTION"?"DISTORTION_ROUTE":
        route.pass=="HUD_UI" || route.pass=="MENU_UI" || provenance.ui_widget[0]?"UI_ROUTE":
        route.semantic.find("DECAL")!=std::string::npos || route.semantic.find("WALLMARK")!=std::string::npos?"DECAL_ROUTE":NULL;
    if(!kind)return;
    const std::string route_key=DrawRouteKey(route);uint64_t hash=1469598103934665603ull;
    for(size_t i=0;i<route_key.size();++i){hash^=static_cast<unsigned char>(route_key[i]);hash*=1099511628211ull;}
    char id[32];snprintf(id,sizeof(id),"route_%016llx",static_cast<unsigned long long>(hash));
    RenderSnapshot_Record(kind,id,"identity_scope","FRAME_ROUTE_FNV64; representative geometry is not exhaustive");
    RenderSnapshot_Record(kind,id,"pass",route.pass.c_str());RenderSnapshot_Record(kind,id,"semantic_type",route.semantic.c_str());
    RenderSnapshot_Record(kind,id,"queue",route.queue.c_str());RenderSnapshot_Record(kind,id,"material_id",route.material.c_str());
    RenderSnapshot_Record(kind,id,"material_route_id",route.material_route.c_str());RenderSnapshot_Record(kind,id,"vertex_shader",route.vs.c_str());
    RenderSnapshot_Record(kind,id,"pixel_shader",route.ps.c_str());RenderSnapshot_Record(kind,id,"states",route.states.c_str());RenderSnapshot_Record(kind,id,"samplers",route.samplers.c_str());
    RenderSnapshot_Record(kind,id,"representative_mesh",provenance.mesh_id);RenderSnapshot_Record(kind,id,"representative_geometry",provenance.geometry_id);
    RenderSnapshot_Record(kind,id,"representative_ui_widget",provenance.ui_widget);RenderSnapshot_Record(kind,id,"representative_ui_origin",provenance.ui_origin);
    RenderSnapshot_RecordU64(kind,id,"last_draw_event",draws);RenderSnapshot_RecordU64(kind,id,"last_draw_submitted",d.submitted);
    RenderSnapshot_RecordU64(kind,id,"last_index_count",d.index_count);RenderSnapshot_RecordU64(kind,id,"last_instance_count",d.instance_count);
    RenderSnapshot_Record(kind,id,"visible_pixels","NOT_MEASURED; backend submission is not pixel execution");
    for(unsigned i=0;i<8;++i){char key[16];snprintf(key,sizeof(key),"texture%u",i);RenderSnapshot_Record(kind,id,key,route.textures[i].c_str());}
}
void AggregateDraw(const RenderSnapshotDraw& d,uint64_t tris){draw_aggregate_totals.Add(d,tris,draws);try{
    DrawRoute route;route.pass=pass;route.light=light_id;route.scope=light_scope;route.phase=phase;
    route.semantic=provenance.semantic_type[0]?Clean(provenance.semantic_type,sizeof(provenance.semantic_type)):"UNKNOWN";route.category=category;
    route.queue=provenance.queue[0]?Clean(provenance.queue,sizeof(provenance.queue)):"UNKNOWN";
    route.material=provenance.material_id[0]?Clean(provenance.material_id,sizeof(provenance.material_id)):material;
    route.material_route=provenance.material_route_id[0]?Clean(provenance.material_route_id,sizeof(provenance.material_route_id)):"NOT_INSTRUMENTED";
    route.vs=d.vertex_shader[0]?Clean(d.vertex_shader,sizeof(d.vertex_shader)):vertex_shader;route.ps=d.pixel_shader[0]?Clean(d.pixel_shader,sizeof(d.pixel_shader)):pixel_shader;
    route.skin=d.skin[0]?Clean(d.skin,sizeof(d.skin)):skin;route.states=Clean(d.states,sizeof(d.states));route.samplers=Clean(d.samplers,sizeof(d.samplers));
    for(unsigned i=0;i<8;++i)route.textures[i]=d.texture_names[i][0]?Clean(d.texture_names[i],sizeof(d.texture_names[i])):texture_names[i];
    route.flags=provenance.flags;route.mask=provenance.pass_mask;route.mask_known=provenance.pass_mask_known;route.variant=detail_variant;route.topology=d.topology;
    ProtectedDraw(route,d);const std::string key=DrawRouteKey(route);auto found=draw_aggregates.find(key);
    if(found==draw_aggregates.end()){const size_t budget=static_cast<size_t>(&TraceBudget(pass)-budgets);
        if(draw_aggregate_counts[budget]<draw_aggregate_limits[budget]){DrawAggregate value;value.route=route;
            found=draw_aggregates.insert(std::make_pair(key,value)).first;++draw_aggregate_counts[budget];}
        else{const std::string other="OTHER_DRAW_ROUTES/"+std::string(budgets[budget].name);found=draw_aggregates.find(other);
            if(found==draw_aggregates.end()){DrawAggregate value;value.coalesced=true;value.route.pass=budgets[budget].name;
                value.route.light=value.route.scope=value.route.phase=value.route.semantic=value.route.category=value.route.queue=value.route.material=value.route.material_route=value.route.vs=value.route.ps=value.route.skin=value.route.states=value.route.samplers="MULTIPLE";
                for(unsigned i=0;i<8;++i){value.route.textures[i]="MULTIPLE";}found=draw_aggregates.insert(std::make_pair(other,value)).first;}
            ++draw_aggregate_coalesced;aggregation_overflow=incomplete=true;}}
    found->second.totals.Add(d,tris,draws);DrawRepresentative(found->second,route,d);
}catch(...){++draw_aggregate_failures;aggregation_overflow=incomplete=true;}}
void WriteDrawTotals(std::ostream& out,const DrawTotals& t){out<<"draw_records_observed="<<t.observed<<"\nsubmitted_draw_calls="<<t.submitted<<"\nskipped_draw_calls="<<t.skipped
    <<"\ntriangles_submitted="<<t.triangles<<"\ninstances_observed="<<t.instances<<"\ninstances_submitted="<<t.submitted_instances<<"\ninstances_skipped="<<t.skipped_instances
    <<"\nfirst_draw_event="<<t.first<<"\nlast_draw_event="<<t.last<<"\n";}
Pass& CurrentPass(){auto found=passes.find(pass);if(found!=passes.end())return found->second;
    if(passes.size()<max_passes)return passes[pass];
    aggregation_overflow=incomplete=true;return passes["OTHER_PASSES"];}
void ClosePass(){if(!pass.empty())CurrentPass().cpu_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pass_start).count();}
std::string GroupKey(){/* Length-prefixing avoids collisions in resource-derived identifiers. */
    const std::string* fields[]={&pass,&light_id,&light_scope,&phase};std::string key;
    for(unsigned i=0;i<4;++i)key+=std::to_string(fields[i]->size())+":"+*fields[i];
    return key;}
Group NewGroup(){Group g;g.pass=pass;g.light=light_id;g.scope=light_scope;g.phase=phase;return g;}
Group& CurrentGroup(){const std::string key=GroupKey();auto found=groups.find(key);if(found!=groups.end())return found->second;
    if(groups.size()<max_groups)return groups.insert(std::make_pair(key,NewGroup())).first->second;
    aggregation_overflow=incomplete=true;Group& g=groups["OTHER_GROUPS"];g.pass=g.light=g.scope=g.phase="OTHER_GROUPS";return g;}
void Observe(Observations& o,const char* status){
    if(status && !strcmp(status,"CANDIDATE_CLASSIFIED")){++o.candidates;++o.classified_candidates;o.candidates_known=o.culled_known=o.selected_known=true;}
    else if(status && !strcmp(status,"CANDIDATE")){++o.candidates;o.candidates_known=true;}
    else if(status && !strcmp(status,"CULLED")){++o.culled;o.culled_known=true;}
    else if(status && !strcmp(status,"SELECTED")){++o.selected;o.selected_known=true;}
    else ++o.other;
}
void WriteObservations(std::ostream& out,const Observations& o){
    out<<"candidates="<<(o.candidates_known?std::to_string(o.candidates):"NOT_INSTRUMENTED")
       <<"\nclassified_candidates="<<(o.candidates_known?std::to_string(o.classified_candidates):"NOT_INSTRUMENTED")
       <<"\nculled="<<(o.culled_known?std::to_string(o.culled):"NOT_INSTRUMENTED")
       <<"\nselected="<<(o.selected_known?std::to_string(o.selected):"NOT_INSTRUMENTED")<<"\nother_note_events="<<o.other<<"\n";
}
void LoseDraw(Pass* p,size_t bytes){++dropped_draws;dropped_bytes+=bytes;incomplete=overflowed=true;
    if(!first_lost){first_lost=draws;first_lost_pass=pass;}last_lost=draws;last_lost_pass=pass;
    if(p){++p->dropped;p->dropped_bytes+=bytes;if(!p->first_lost)p->first_lost=draws;p->last_lost=draws;}}
void AddDraw(const std::string& text,Pass& p){const size_t size=text.size();draw_bytes+=size;p.bytes+=size;Budget& budget=TraceBudget(pass);
    const size_t event_limit=budget.limit/(256u*1024u)*1024u;
    if(budget.events>=event_limit || size>budget.limit-budget.bytes || events>=max_events || size>max_bytes-body.size()){LoseDraw(&p,size);return;}
    body+=text;++events;++p.retained;p.retained_bytes+=size;budget.bytes+=size;++budget.events;
}
struct DictionaryRecord {std::map<std::string,std::string> fields;size_t bytes=0;};
struct Dictionary {const char* name;size_t records_limit,field_limit,bytes_limit,bytes;uint64_t observed,retained,dropped,truncated;
    std::map<std::string,DictionaryRecord> records;};
Dictionary dictionaries[]={
    {"UI",128,48,384*1024,0,0,0,0,0,{}},{"DECALS",128,48,384*1024,0,0,0,0,0,{}},
    {"NORMAL_MAPPING",128,48,384*1024,0,0,0,0,0,{}},{"DISTORTION",64,48,192*1024,0,0,0,0,0,{}},
    {"MATERIAL",128,64,512*1024,0,0,0,0,0,{}},{"SHADER",128,48,512*1024,0,0,0,0,0,{}},{"RESOURCE",256,48,768*1024,0,0,0,0,0,{}},
    {"GEOMETRY",128,48,384*1024,0,0,0,0,0,{}},{"MODEL_POSE",128,48,384*1024,0,0,0,0,0,{}},
    {"MODEL_COMMAND",128,48,384*1024,0,0,0,0,0,{}},{"LIGHT",128,48,384*1024,0,0,0,0,0,{}},
    {"SHADOW",64,64,256*1024,0,0,0,0,0,{}},{"PASS_EXECUTION",128,48,384*1024,0,0,0,0,0,{}},
    {"ENVIRONMENT",64,48,192*1024,0,0,0,0,0,{}},{"POSTPROCESS",64,48,192*1024,0,0,0,0,0,{}},
    {"CPU_CAPTURE",64,48,192*1024,0,0,0,0,0,{}},{"ROUTE_WARNING",64,32,128*1024,0,0,0,0,0,{}},
    {"UI_ROUTE",128,48,384*1024,0,0,0,0,0,{}},{"DECAL_ROUTE",128,48,384*1024,0,0,0,0,0,{}},{"DISTORTION_ROUTE",64,48,192*1024,0,0,0,0,0,{}},
    {"R2_BINDING",128,128,768*1024,0,0,0,0,0,{}},{"R2_UI_BINDING",128,128,768*1024,0,0,0,0,0,{}},
    {"R2_DECAL_BINDING",128,128,768*1024,0,0,0,0,0,{}},{"R2_MODEL_BINDING",128,128,768*1024,0,0,0,0,0,{}},
    {"DETAIL_GEOMETRY",128,48,384*1024,0,0,0,0,0,{}},{"OTHER",64,48,192*1024,0,0,0,0,0,{}}};
const size_t dictionary_count=sizeof(dictionaries)/sizeof(dictionaries[0]);
bool dictionary_overflow=false;uint64_t profile_dictionary_skips=0;
Dictionary& PickDictionary(const std::string& category){std::string name=category;
    if(category=="R2_RESOURCE")name="RESOURCE";else if(category=="LIGHTS")name="LIGHT";else if(category=="SHADOWS")name="SHADOW";;
    for(size_t i=0;i+1<dictionary_count;++i){if(name==dictionaries[i].name)return dictionaries[i];}
    if(category.compare(0,3,"UI_")==0)return dictionaries[0];
    return dictionaries[dictionary_count-1];}
void RefreshDiagnostic(){diagnostic_enabled.store(pending!=0 || profile_remaining!=0 || g_render_snapshot_active || cycle.active,std::memory_order_release);
    event_enabled.store((pending!=0 || g_render_snapshot_active || cycle.active) && !frame_profile,std::memory_order_release);}
void CopyField(char* out,size_t size,const char* text){FixedText(out,size,text);}
bool QueueRequest(const char* reason,uint64_t group,const char* role,bool profile=false){
    if(pending>=16){++request_rejections;return false;}
    const bool phase_request=group && (!strcmp(role,"BASELINE") || !strcmp(role,"OPENED") || !strcmp(role,"RECOVERY"));
    const unsigned at=phase_request?0:pending;if(phase_request)for(unsigned i=pending;i>0;--i)requests[i]=requests[i-1];++pending;
    Request& r=requests[at];r=Request();r.id=++next_request;r.group=group;r.requested_us=EpochUS();r.profile=profile;
    RefreshDiagnostic();CopyField(r.reason,sizeof(r.reason),reason);CopyField(r.role,sizeof(r.role),role);return true;
}
void ClearEvents(){std::lock_guard<std::mutex> lock(event_mutex);ui_events_count=0;ui_events_observed=ui_events_dropped=0;}
void FinishCycle(const char* status){CopyField(cycle.status,sizeof(cycle.status),status);cycle.active=false;cycle.queued=false;RefreshDiagnostic();}
void WriteTiming(std::ostream& out,uint64_t capture,uint64_t engine,uint64_t render,const char* status,const double* values,uint32_t mask){
    out<<"\n[GPU_TIMING_COMPLETION]\ncapture_id="<<capture<<"\nengine_frame="<<engine<<"\nrender_frame="<<render<<"\nstatus="<<Clean(status,64)
        <<"\ncompletion_scope=TIMING_ONLY; no later frame graphics appended\nvalid_stage_mask="<<mask<<"\n";
    for(unsigned i=0;i<7;++i){out<<"stage"<<i<<"_milliseconds=";
        if(!status || strcmp(status,"READY"))out<<"NOT_MEASURED";
        else if(!(mask&(1u<<i)))out<<"NOT_APPLICABLE";
        else if(!values || !std::isfinite(values[i]) || values[i]<0)out<<"INVALID";
        else {out<<std::setprecision(17)<<values[i];}out<<"\n";}
}
void ClearStorage(){for(auto& d:dictionaries){d.records.clear();d.bytes=0;d.observed=d.retained=d.dropped=d.truncated=0;}dictionary_overflow=false;std::string().swap(body);passes.clear();groups.clear();visibility.clear();draw_aggregates.clear();for(unsigned i=0;i<summary_count;++i){summaries[i].fields.clear();summaries[i].bytes=0;}}
FILE* Exclusive(const char* path){
#if defined(_WIN32)
    int fd=-1;const int error=_sopen_s(&fd,path,_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_SH_DENYRW,_S_IREAD|_S_IWRITE);if(error){errno=error;return NULL;}
    FILE* file=_fdopen(fd,"wb");if(!file)_close(fd);return file;
#else
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return NULL;FILE* file=fdopen(fd,"wb");if(!file)close(fd);return file;
#endif
}
}
bool RenderSnapshot_Request(){return RenderSnapshot_RequestReason("CONSOLE");}
bool RenderSnapshot_RequestReason(const char* reason){if(!RenderSnapshot_DiagnosticActive())ClearEvents();return QueueRequest(reason,0,"MANUAL");}
bool RenderSnapshot_Wanted(){return pending!=0 || profile_remaining!=0 || g_render_snapshot_active;}
bool RenderSnapshot_DiagnosticActive(){return diagnostic_enabled.load(std::memory_order_acquire);}
bool RenderSnapshot_Detailed(){return g_render_snapshot_active && !frame_profile;}
uint64_t RenderSnapshot_CaptureID(){return g_render_snapshot_active?capture_request.id:0;}
uint64_t RenderSnapshot_GroupID(){return cycle.active?cycle.group:g_render_snapshot_active?capture_request.group:0;}
uint64_t RenderSnapshot_RenderFrameID(){return g_render_snapshot_active?captured_render_id:(frame_ids_known?render_id:render_tick);}
void RenderSnapshot_SetHotkey(uint32_t dik){if(dik<=255)hotkey=dik;}
uint32_t RenderSnapshot_GetHotkey(){return hotkey;}
bool RenderSnapshot_ProfileRequest(uint32_t frames){if(!frames || frames>120 || profile_remaining || cycle.active || pending || g_render_snapshot_active)return false;ClearEvents();profile_remaining=frames;RefreshDiagnostic();return true;}
void RenderSnapshot_FrameIDs(uint64_t render,uint64_t present,uint64_t level,uint64_t device){
    const uint64_t missing=~uint64_t(0);if(render!=missing){render_id=render;frame_ids_known=true;event_render.store(render,std::memory_order_release);}
    if(present!=missing){present_id=present;present_id_known=true;}
    if(level!=missing)level_generation=level;
    if(device!=missing)device_generation=device;
}
void RenderSnapshot_PresentID(uint64_t ordinal,bool submitted){if(submitted){present_id=ordinal;present_id_known=true;}if(g_render_snapshot_active){captured_present_id=ordinal;captured_present_known=true;captured_present_submitted=submitted;}}
void RenderSnapshot_Event(const char* kind,const char* subject,const char* action,const char* result){
    if(!event_enabled.load(std::memory_order_acquire))return;
    std::lock_guard<std::mutex> lock(event_mutex);if(!event_enabled.load(std::memory_order_acquire))return;
    ++ui_events_observed;if(ui_events_count>=256){++ui_events_dropped;return;}
    UIEvent& event=ui_events[ui_events_count++];event.ordinal=ui_events_observed;event.render=event_render.load(std::memory_order_acquire);event.when=EpochUS();
    CopyField(event.kind,sizeof(event.kind),kind);CopyField(event.subject,sizeof(event.subject),subject);CopyField(event.action,sizeof(event.action),action);CopyField(event.result,sizeof(event.result),result);
}
void RenderSnapshot_UIWindow(const char* kind,const char* id,bool shown){
    unsigned at=ui_window_count;for(unsigned i=0;i<ui_window_count;++i)if(!strcmp(ui_windows[i].id,id?id:"UNKNOWN")){at=i;break;}
    if(!shown && at==ui_window_count)return;
    char event_kind[32];CopyField(event_kind,sizeof(event_kind),!shown && at<ui_window_count?ui_windows[at].kind:kind);
    if(at<ui_window_count){for(unsigned i=at+1;i<ui_window_count;++i)ui_windows[i-1]=ui_windows[i];--ui_window_count;}
    if(shown){if(ui_window_count==16){++ui_window_overflow;for(unsigned i=1;i<16;++i)ui_windows[i-1]=ui_windows[i];--ui_window_count;}
        UIWindow& window=ui_windows[ui_window_count++];CopyField(window.kind,sizeof(window.kind),kind);CopyField(window.id,sizeof(window.id),id);}
    RenderSnapshot_Event("UI_WINDOW",id,shown?"SHOWN":"HIDDEN",event_kind);
}
const char* RenderSnapshot_UIScreen(){return ui_window_count?ui_windows[ui_window_count-1].kind:"GAME";}
const char* RenderSnapshot_UIRoot(){return ui_window_count?ui_windows[ui_window_count-1].id:"NONE";}
void RenderSnapshot_UIInput(const char* owner){if(owner && strcmp(ui.owner,owner)){RenderSnapshot_Event("INPUT",owner,"OWNER_CHANGED",ui.owner);CopyField(ui.owner,sizeof(ui.owner),owner);}}
const char* RenderSnapshot_UIInputOwner(){return ui.owner;}
bool RenderSnapshot_UICycleRequest(const char* target,uint32_t limit){
    if(!target || (strcmp(target,"PDA") && strcmp(target,"INVENTORY") && strcmp(target,"MENU")) || cycle.active || !limit || limit>36000 || profile_remaining || pending || g_render_snapshot_active)return false;
    ClearEvents();cycle=Cycle();cycle.active=true;cycle.group=++next_group;cycle.start_tick=render_tick;cycle.start_us=EpochUS();cycle.start_steady_us=SteadyUS();cycle.level=level_generation;cycle.device=device_generation;cycle.timeout=limit;RefreshDiagnostic();
    CopyField(cycle.target,sizeof(cycle.target),target);CopyField(cycle.status,sizeof(cycle.status),"WAITING_BASELINE");RenderSnapshot_Event("UI_CYCLE",target,"REQUESTED","WAITING_BASELINE");return true;
}
void RenderSnapshot_UICycleCancel(const char* reason){if(!cycle.active)return;RenderSnapshot_Event("UI_CYCLE",cycle.target,"CANCELLED",reason);
    for(unsigned i=0;i<pending;){if(requests[i].group==cycle.group){for(unsigned j=i+1;j<pending;++j)requests[j-1]=requests[j];--pending;++aborted_requests;}else ++i;}
    FinishCycle(reason?reason:"CANCELLED");QueueRequest("UI_CYCLE_CANCELLED",cycle.group,"TRANSITION_TERMINATED");
}
void RenderSnapshot_UIState(const char* screen,const char* root,const char* owner,bool loaded,bool console,bool paused){
    ++render_tick;if(!frame_ids_known)event_render.store(render_tick,std::memory_order_release);UIObservation next;CopyField(next.screen,sizeof(next.screen),screen);CopyField(next.root,sizeof(next.root),root);CopyField(next.owner,sizeof(next.owner),owner);
    next.loaded=loaded;next.console=console;next.paused=paused;
    if(strcmp(ui.screen,next.screen))RenderSnapshot_Event("UI_STATE",next.root,"SCREEN_CHANGED",next.screen);
    if(ui.paused!=paused)RenderSnapshot_Event("PAUSE",next.root,paused?"PAUSED":"RESUMED","OBSERVED");
    if(strcmp(ui.owner,next.owner))RenderSnapshot_Event("INPUT",next.owner,"OWNER_CHANGED",ui.owner);
    ui=next;if(!cycle.active)return;
    if(cycle.level && level_generation!=cycle.level){RenderSnapshot_UICycleCancel("LEVEL_CHANGED");return;}
    if(cycle.device && device_generation!=cycle.device){RenderSnapshot_UICycleCancel("DEVICE_RESET");return;}
    if(render_tick-cycle.start_tick>=cycle.timeout || SteadyUS()-cycle.start_steady_us>300000000u){RenderSnapshot_UICycleCancel("TIMEOUT");return;}
    if(cycle.queued)return;
    const bool game=loaded && !console && !strcmp(ui.screen,"GAME");const bool target=(loaded || !strcmp(cycle.target,"MENU")) && !console && (!strcmp(ui.screen,cycle.target) || (!strcmp(cycle.target,"MENU") && !strcmp(ui.screen,"START_MENU")));
    if(cycle.stage==0 && game){cycle.queued=QueueRequest("UI_CYCLE_BASELINE",cycle.group,"BASELINE");}
    else if(cycle.stage<=1 && target){cycle.queued=QueueRequest("UI_CYCLE_OPENED",cycle.group,"OPENED");if(!cycle.baseline)CopyField(cycle.status,sizeof(cycle.status),"BASELINE_MISSING");}
    else if(cycle.stage==2 && game){cycle.queued=QueueRequest("UI_CYCLE_RECOVERY",cycle.group,"RECOVERY");}
}
namespace {
bool CyclePhaseMatches(const Request& request){
    if(!request.group || (strcmp(request.role,"BASELINE") && strcmp(request.role,"OPENED") && strcmp(request.role,"RECOVERY")))return true;
    if(!cycle.active || request.group!=cycle.group || ui.console)return false;
    const bool game=ui.loaded && !strcmp(ui.screen,"GAME");
    if(!strcmp(request.role,"BASELINE"))return game;
    if(!strcmp(request.role,"RECOVERY"))return game && cycle.opened;
    return (ui.loaded || !strcmp(cycle.target,"MENU")) && (!strcmp(ui.screen,cycle.target) || (!strcmp(cycle.target,"MENU") && !strcmp(ui.screen,"START_MENU")));
}
}
void RenderSnapshot_Begin(const RenderSnapshotInfo& input){
    if(g_render_snapshot_active || (!pending && !profile_remaining))return;
    while(pending && !CyclePhaseMatches(requests[0])){
        RenderSnapshot_Event("UI_CYCLE",requests[0].role,"CAPTURE_DEFERRED","ACTUAL_SCREEN_CHANGED_BEFORE_BEGIN");
        if(requests[0].group==cycle.group)cycle.queued=false;
        for(unsigned i=1;i<pending;++i){requests[i-1]=requests[i];}--pending;++aborted_requests;
    }
    if(!pending && !profile_remaining){RefreshDiagnostic();return;}
    if(!pending && profile_remaining){QueueRequest("COUNTER_PROFILE",0,"PROFILE",true);--profile_remaining;}
    capture_request=requests[0];for(unsigned i=1;i<pending;++i)requests[i-1]=requests[i];--pending;
    frame_profile=capture_request.profile;captured_present_known=captured_present_submitted=false;gpu_completed=false;gpu_completion_status="NOT_INSTRUMENTED";profile_dictionary_skips=0;
    captured_render_id=frame_ids_known?render_id:render_tick;captured_present_id=present_id;captured_level_generation=level_generation;captured_device_generation=device_generation;captured_us=EpochUS();
    last_info=input;have_last_info=true;
    try{failure=NULL;info=input;ClearStorage();pass.clear();category="UNKNOWN";resource="UNKNOWN";material="UNKNOWN";
        vertex_shader=pixel_shader="UNKNOWN";skin="NOT_INSTRUMENTED";g_render_snapshot_console=false;for(unsigned i=0;i<8;++i)texture_names[i]="UNKNOWN";
        incomplete=overflowed=summary_overflow=aggregation_overflow=false;draws=triangles=events=draw_bytes=dropped_draws=dropped_bytes=0;
        first_lost=last_lost=serialization_failures=invalid_draws=0;summary_seen=summary_retained=summary_dropped=summary_omitted=visibility_seen=source_telemetry_losses=0;
        first_lost_pass.clear();last_lost_pass.clear();first_lost_pass.reserve(96);last_lost_pass.reserve(96);
        note_totals=Observations();ResetObject();light_id=light_scope=phase="NONE";
        draw_aggregate_totals=DrawTotals();draw_aggregate_failures=draw_aggregate_coalesced=0;memset(draw_aggregate_counts,0,sizeof(draw_aggregate_counts));
        for(auto& b:budgets)b.bytes=b.events=0;
        frame_start=pass_start=std::chrono::steady_clock::now();g_render_snapshot_active=true;RefreshDiagnostic();RenderSnapshot_Pass("FRAME_SETUP");
        RenderSnapshot_Text("DETAIL_RENDERER","measurement_scope","INSTRUMENTED_FIELDS_ONLY; absent metrics NOT_INSTRUMENTED");
        RenderSnapshot_Text("MEMORY","measurement_scope","INSTRUMENTED_FIELDS_ONLY; absent metrics NOT_AVAILABLE; process GPU memory NOT_AVAILABLE");
        RenderSnapshot_Text("UI_STATE","screen",ui.screen);RenderSnapshot_Text("UI_STATE","root",ui.root);RenderSnapshot_Text("UI_STATE","input_owner",ui.owner);
        RenderSnapshot_U64("UI_STATE","level_loaded",ui.loaded?1:0);RenderSnapshot_U64("UI_STATE","console_visible",ui.console?1:0);RenderSnapshot_U64("UI_STATE","paused",ui.paused?1:0);
        RenderSnapshot_U64("UI_STATE","observed_window_count",ui_window_count);RenderSnapshot_U64("UI_STATE","window_stack_overflow",ui_window_overflow);
        for(unsigned i=0;i<ui_window_count;++i){char key[32];snprintf(key,sizeof(key),"window_%u",i);RenderSnapshot_Text("UI_STATE",key,ui_windows[i].id);}
        if(capture_request.group && capture_request.group==cycle.group){if(!strcmp(capture_request.role,"BASELINE")){CopyField(cycle.baseline_root,sizeof(cycle.baseline_root),ui.root);CopyField(cycle.baseline_owner,sizeof(cycle.baseline_owner),ui.owner);cycle.baseline_pause=ui.paused;}
            if(!strcmp(capture_request.role,"RECOVERY") && cycle.baseline){RenderSnapshot_Text("UI_RESTORATION","input_owner_comparison",strcmp(cycle.baseline_owner,ui.owner)?"CHANGED_OBSERVATION":"MATCH");
                RenderSnapshot_Text("UI_RESTORATION","pause_comparison",cycle.baseline_pause!=ui.paused?"CHANGED_OBSERVATION":"MATCH");RenderSnapshot_Text("UI_RESTORATION","contract_verdict","NOT_EVALUATED; differences are observations");}}

    }catch(...){g_render_snapshot_active=false;frame_profile=false;failure="ERROR: snapshot allocation failure";aborted_requests+=pending+profile_remaining;pending=profile_remaining=0;if(cycle.active)FinishCycle("ALLOCATION_ERROR");ClearStorage();RefreshDiagnostic();}
}
void RenderSnapshot_Pass(const char* name){if(!g_render_snapshot_active)return;try{ClosePass();pass=Clean(name,96);pass_start=std::chrono::steady_clock::now();category="UNKNOWN";resource=material="UNKNOWN";ResetObject();light_id=light_scope=phase="NONE";}catch(...){incomplete=true;}}
void RenderSnapshot_Context(const char* kind,const char* name,const char* shader){if(!RenderSnapshot_Detailed())return;try{category=Clean(kind,64);resource=Clean(name,256);material=Clean(shader,256);skin="NOT_INSTRUMENTED";ResetObject();}catch(...){incomplete=true;}}
void RenderSnapshot_Object(const char* semantic,const char* mesh,const char* mat,const char* evidence,uint32_t flags){if(!RenderSnapshot_Detailed())return;try{
    provenance.uv_count=0;provenance.uv_source[0]=0;memset(provenance.uv,0,sizeof(provenance.uv));detail_variant=~uint32_t(0);
    Copy(provenance.semantic_type,sizeof(provenance.semantic_type),semantic);Copy(provenance.mesh_id,sizeof(provenance.mesh_id),mesh);Copy(provenance.material_id,sizeof(provenance.material_id),mat);
    Copy(provenance.evidence,sizeof(provenance.evidence),evidence);provenance.flags=flags;}catch(...){incomplete=true;}}
void RenderSnapshot_Queue(const char* name){if(!RenderSnapshot_Detailed())return;try{Copy(provenance.queue,sizeof(provenance.queue),name);}catch(...){incomplete=true;}}
void RenderSnapshot_PassMask(uint32_t mask){if(!RenderSnapshot_Detailed())return;provenance.pass_mask=mask;provenance.pass_mask_known=1;}
void RenderSnapshot_UV(const float* pairs,uint32_t count,const char* source){if(!RenderSnapshot_Detailed())return;try{provenance.uv_count=pairs?(count>4?4:count):0;
    memset(provenance.uv,0,sizeof(provenance.uv));if(provenance.uv_count)memcpy(provenance.uv,pairs,provenance.uv_count*2*sizeof(float));Copy(provenance.uv_source,sizeof(provenance.uv_source),source);}catch(...){incomplete=true;}}
void RenderSnapshot_GetProvenance(RenderSnapshotProvenance* out){if(out)*out=g_render_snapshot_active?provenance:RenderSnapshotProvenance();}
void RenderSnapshot_SetProvenance(const RenderSnapshotProvenance* value){if(!RenderSnapshot_Detailed())return;provenance=value?*value:RenderSnapshotProvenance();if(provenance.uv_count>4)provenance.uv_count=4;}
void RenderSnapshot_Light(const char* id,const char* scope){if(!RenderSnapshot_Detailed())return;try{light_id=id?Clean(id,96):"NONE";light_scope=scope?Clean(scope,64):"NONE";}catch(...){incomplete=true;}}
void RenderSnapshot_Phase(const char* id){if(!RenderSnapshot_Detailed())return;try{phase=id?Clean(id,96):"NONE";}catch(...){incomplete=true;}}
void RenderSnapshot_DetailVariant(uint32_t variant){if(RenderSnapshot_Detailed())detail_variant=variant<3?variant:~uint32_t(0);}
void RenderSnapshot_Shaders(const char* vs,const char* ps){if(!RenderSnapshot_Detailed())return;try{vertex_shader=Clean(vs,96);pixel_shader=Clean(ps,96);}catch(...){incomplete=true;}}
void RenderSnapshot_ShaderStage(bool vertex,const char* name){if(!RenderSnapshot_Detailed())return;try{(vertex?vertex_shader:pixel_shader)=Clean(name,96);}catch(...){incomplete=true;}}
void RenderSnapshot_Texture(uint32_t stage,const char* name){if(!RenderSnapshot_Detailed() || stage>=8)return;try{texture_names[stage]=Clean(name,128);}catch(...){incomplete=true;}}
void RenderSnapshot_Skin(const char* mode){if(!RenderSnapshot_Detailed())return;try{skin=Clean(mode,32);}catch(...){incomplete=true;}}
void RenderSnapshot_MaterialRoute(const char* id){if(RenderSnapshot_Detailed())CopyField(provenance.material_route_id,sizeof(provenance.material_route_id),id);}
void RenderSnapshot_GeometrySource(const char* id){if(RenderSnapshot_Detailed())CopyField(provenance.geometry_id,sizeof(provenance.geometry_id),id);}
void RenderSnapshot_UIProvenance(const char* widget,const char* origin){if(!RenderSnapshot_Detailed())return;CopyField(provenance.ui_widget,sizeof(provenance.ui_widget),widget);CopyField(provenance.ui_origin,sizeof(provenance.ui_origin),origin);}
void RenderSnapshot_UIWidget(const char* widget,const char* origin){RenderSnapshot_UIProvenance(widget,origin);}
void SNAPSHOT_CALL RenderSnapshot_Draw(const RenderSnapshotDraw* d){
    if(!g_render_snapshot_active)return;
    if(!d || d->struct_size!=sizeof(*d)){++invalid_draws;incomplete=true;return;}
    ++draws;Pass* current=NULL;bool accounted=false;size_t serialized_bytes=0;
    const uint64_t tris=d->topology==4?uint64_t(d->index_count?d->index_count:d->vertex_count)/3*d->instance_count:
        d->topology==5 || d->topology==6?uint64_t((d->index_count?d->index_count:d->vertex_count)>2?(d->index_count?d->index_count:d->vertex_count)-2:0)*d->instance_count:0;
    if(frame_profile){draw_aggregate_totals.Add(*d,tris,draws);triangles+=d->submitted?tris:0;
        try{Pass& p=CurrentPass();++p.observed;p.calls+=d->submitted?1:0;p.triangles+=d->submitted?tris:0;++p.dropped;++dropped_draws;
            if(!p.first_lost){p.first_lost=draws;}p.last_lost=draws;}
        catch(...){incomplete=summary_overflow=true;}return;}
    AggregateDraw(*d,tris);
    try{if(!d->submitted)incomplete=true;
        triangles+=d->submitted?tris:0;Pass& p=CurrentPass();current=&p;++p.observed;p.calls+=d->submitted?1:0;p.triangles+=d->submitted?tris:0;
        Group& g=CurrentGroup();++g.observed;g.submitted+=d->submitted?1:0;g.skipped+=d->submitted?0:1;g.triangles+=d->submitted?tris:0;
        const std::string semantic=provenance.semantic_type[0]?Clean(provenance.semantic_type,sizeof(provenance.semantic_type)):"UNKNOWN";
        const std::string evidence=provenance.evidence[0]?Clean(provenance.evidence,sizeof(provenance.evidence)):"NOT_INSTRUMENTED";
        const bool flags_known=provenance.flags || (semantic!="UNKNOWN" && evidence!="NOT_INSTRUMENTED" && evidence!="UNKNOWN" && evidence.find("FALLBACK")==std::string::npos && evidence.find("AMBIGUOUS")==std::string::npos);
        std::ostringstream out;out<<"\n[DRAW "<<std::setfill('0')<<std::setw(6)<<draws<<"]\nstatus="<<(d->submitted?"SUBMITTED":"SKIPPED")
            <<"\npass="<<pass<<"\ncategory="<<category<<"\nresource="<<resource<<"\nmaterial="<<material
            <<"\nsemantic_type="<<semantic
            <<"\nrender_queue="<<(provenance.queue[0]?Clean(provenance.queue,sizeof(provenance.queue)):"UNKNOWN")
            <<"\nmesh_id="<<(provenance.mesh_id[0]?Clean(provenance.mesh_id,sizeof(provenance.mesh_id)):"UNKNOWN")
            <<"\nmaterial_id="<<(provenance.material_id[0]?Clean(provenance.material_id,sizeof(provenance.material_id)):"UNKNOWN")
            <<"\nmaterial_route_id="<<(provenance.material_route_id[0]?Clean(provenance.material_route_id,sizeof(provenance.material_route_id)):"NOT_INSTRUMENTED")
            <<"\ngeometry_id="<<(provenance.geometry_id[0]?Clean(provenance.geometry_id,sizeof(provenance.geometry_id)):"NOT_INSTRUMENTED")
            <<"\nui_widget="<<(provenance.ui_widget[0]?Clean(provenance.ui_widget,sizeof(provenance.ui_widget)):"NOT_INSTRUMENTED")
            <<"\nui_origin="<<(provenance.ui_origin[0]?Clean(provenance.ui_origin,sizeof(provenance.ui_origin)):"NOT_INSTRUMENTED")
            <<"\nsemantic_evidence="<<evidence
            <<"\nobject_flags="<<(flags_known?std::to_string(provenance.flags):"NOT_INSTRUMENTED")
            <<"\nis_particle="<<(flags_known?((provenance.flags&RENDER_SNAPSHOT_PARTICLE)?"1":"0"):"NOT_INSTRUMENTED")
            <<"\nis_glow="<<(flags_known?((provenance.flags&RENDER_SNAPSHOT_GLOW)?"1":"0"):"NOT_INSTRUMENTED")
            <<"\npass_mask="<<(provenance.pass_mask_known?std::to_string(provenance.pass_mask):"NOT_INSTRUMENTED")
            <<"\nlight_id="<<light_id<<"\nlight_scope="<<light_scope<<"\nphase="<<phase
            <<"\ndetail_variant="<<(detail_variant<3?std::to_string(detail_variant):"NOT_INSTRUMENTED")
            <<"\ndetail_variant_name="<<(detail_variant==0?"Still":detail_variant==1?"Wave1":detail_variant==2?"Wave2":"NOT_INSTRUMENTED")
            <<"\nuv_source="<<(provenance.uv_source[0]?Clean(provenance.uv_source,sizeof(provenance.uv_source)):"NOT_INSTRUMENTED")<<"\nuv_sample_count="<<provenance.uv_count
            <<"\nvertex_shader="<<(d->vertex_shader[0]?Clean(d->vertex_shader,sizeof(d->vertex_shader)):vertex_shader)
            <<"\npixel_shader="<<(d->pixel_shader[0]?Clean(d->pixel_shader,sizeof(d->pixel_shader)):pixel_shader)
            <<"\nvertices="<<(d->vertex_count_known?std::to_string(d->vertex_count):"NOT_AVAILABLE")
            <<"\nvb_capacity_vertices="<<(d->vertex_capacity?std::to_string(d->vertex_capacity):"NOT_AVAILABLE")<<"\nindices="<<d->index_count<<"\ninstances="<<d->instance_count
            <<"\ntriangles_submitted="<<(d->submitted?tris:0)<<"\ntopology_raw="<<d->topology<<"\nstart_index="<<d->start_index<<"\nfirst_instance="<<d->first_instance
            <<"\npipeline="<<d->pipeline<<"\nvb="<<d->vb<<"\nib="<<d->ib<<"\ncb="<<d->cb<<"\ninstance_buffer="<<d->instances
            <<"\nvs_handle="<<d->vs<<"\nps_handle="<<d->ps<<"\nskin_mode="<<(d->skin[0]?Clean(d->skin,sizeof(d->skin)):skin)
            <<"\nraw_states="<<Clean(d->states,sizeof(d->states))<<"\nsamplers="<<Clean(d->samplers,sizeof(d->samplers))<<"\n";
        for(unsigned i=0;i<8;++i)out<<"texture"<<i<<"="<<(d->texture_names[i][0]?Clean(d->texture_names[i],sizeof(d->texture_names[i])):texture_names[i])<<"; handle="<<d->textures[i]<<"\n";
        for(uint32_t i=0;i<provenance.uv_count;++i)out<<"uv_sample"<<i<<"="<<provenance.uv[i*2]<<","<<provenance.uv[i*2+1]<<"\n";
        const std::string record=out.str();serialized_bytes=record.size();AddDraw(record,p);accounted=true;
    }catch(...){++serialization_failures;incomplete=true;if(!accounted)LoseDraw(current,serialized_bytes);}
}
void RenderSnapshot_Note(const char* status,const char* kind,const char* name,const char* reason){if(!g_render_snapshot_active)return;++visibility_seen;Observe(note_totals,status);if(frame_profile)return;try{
    Observe(CurrentGroup().notes,status);const std::string cat=Clean(kind,64),why=Clean(reason,256);
    const std::string key=GroupKey()+std::to_string(cat.size())+":"+cat+std::to_string(why.size())+":"+why;
    auto found=visibility.find(key);
    if(found==visibility.end()){
        if(visibility.size()<max_visibility){Visibility value;value.group=NewGroup();value.category=cat;value.reason=why;found=visibility.insert(std::make_pair(key,value)).first;}
        else{aggregation_overflow=incomplete=true;found=visibility.find("OTHER_VISIBILITY");if(found==visibility.end()){
            Visibility value;value.group.pass=value.group.light=value.group.scope=value.group.phase=value.category=value.reason="OTHER_VISIBILITY";found=visibility.insert(std::make_pair("OTHER_VISIBILITY",value)).first;}}
    }
    Visibility& v=found->second;Observe(v.notes,status);const std::string sample=Clean(name,256);bool present=false;
    for(uint32_t i=0;i<v.samples_count;++i)if(v.samples[i]==sample)present=true;
    if(!present){if(v.samples_count<4)v.samples[v.samples_count++]=sample;else ++v.samples_omitted;}
}catch(...){incomplete=aggregation_overflow=true;}}
void RenderSnapshot_U64(const char* section,const char* key,uint64_t value){if(!g_render_snapshot_active)return;char text[32];snprintf(text,sizeof(text),"%llu",static_cast<unsigned long long>(value));RenderSnapshot_Text(section,key,text);}
void RenderSnapshot_I64(const char* section,const char* key,int64_t value){if(!g_render_snapshot_active)return;char text[32];snprintf(text,sizeof(text),"%lld",static_cast<long long>(value));RenderSnapshot_Text(section,key,text);}
void RenderSnapshot_Record(const char* kind,const char* identity,const char* key,const char* value){
    if(!g_render_snapshot_active)return;
    if(frame_profile){++profile_dictionary_skips;return;}
    try{const std::string category=Clean(kind,32),id=Clean(identity,192),name=Clean(key,96),text=Clean(value,2048);Dictionary& d=PickDictionary(category);++d.observed;
        const bool shortened=(kind && strlen(kind)>32) || (identity && strlen(identity)>192) || (key && strlen(key)>96) || (value && strlen(value)>2048);
        if(shortened){++d.truncated;dictionary_overflow=incomplete=true;}
        const std::string record_id=category+"/"+id;auto found=d.records.find(record_id);
        if(found==d.records.end()){if(d.records.size()>=d.records_limit){++d.dropped;dictionary_overflow=incomplete=true;return;}found=d.records.insert(std::make_pair(record_id,DictionaryRecord())).first;}
        DictionaryRecord& record=found->second;auto field=record.fields.find(name);const size_t previous=field==record.fields.end()?0:name.size()+field->second.size();const size_t next=name.size()+text.size();
        if((field==record.fields.end() && record.fields.size()>=d.field_limit) || next>d.bytes_limit-(d.bytes-previous)){++d.dropped;dictionary_overflow=incomplete=true;return;}
        record.fields[name]=text;record.bytes=record.bytes-previous+next;d.bytes=d.bytes-previous+next;++d.retained;
    }catch(...){dictionary_overflow=incomplete=true;}
}
void RenderSnapshot_RecordU64(const char* category,const char* id,const char* key,uint64_t value){if(!RenderSnapshot_Detailed())return;char text[32];snprintf(text,sizeof(text),"%llu",static_cast<unsigned long long>(value));RenderSnapshot_Record(category,id,key,text);}
void RenderSnapshot_RecordI64(const char* category,const char* id,const char* key,int64_t value){if(!RenderSnapshot_Detailed())return;char text[32];snprintf(text,sizeof(text),"%lld",static_cast<long long>(value));RenderSnapshot_Record(category,id,key,text);}
void RenderSnapshot_Value(const char* section,const char* key,double value){if(!g_render_snapshot_active)return;try{std::ostringstream out;out<<std::setprecision(12)<<value;RenderSnapshot_Text(section,key,out.str().c_str());}catch(...){incomplete=summary_overflow=true;}}
void RenderSnapshot_Text(const char* section,const char* key,const char* value){if(!g_render_snapshot_active)return;++summary_seen;try{
    const std::string sec=Clean(section,64),name=Clean(key,96),text=Clean(value,2048);unsigned index=sec=="DETAIL_RENDERER"?0:sec=="MEMORY"?1:sec=="SETTINGS"?2:sec=="MODEL_RENDERER"?4:3;
    const char* protected_sections[]={"UI_STATE","UI_RESTORATION","FRAME_PASSPORT","MATERIAL_SUMMARY","NORMAL_MAPPING_SUMMARY","DECAL_SUMMARY","LIGHT_SUMMARY","CPU_CAPTURE","TIMING","GPU_TIMING"};
    for(unsigned i=0;i<10;++i)if(sec==protected_sections[i])index=5+i;
    if(frame_profile && index!=1 && index!=7 && index!=13 && index!=14){++summary_omitted;return;}
    if(sec=="TELEMETRY_LOSS"){incomplete=true;++source_telemetry_losses;}
    Summary& store=summaries[index];const auto id=std::make_pair(sec,name);auto found=store.fields.find(id);
    const size_t old=found==store.fields.end()?0:sec.size()+name.size()+found->second.size();const size_t next=sec.size()+name.size()+text.size();
    const size_t key_limit=index==3?1024:128,byte_limit=index==3?512u*1024u:128u*1024u;
    if((found==store.fields.end() && store.fields.size()>=key_limit) || next>byte_limit-(store.bytes-old)){++summary_dropped;incomplete=summary_overflow=true;return;}
    store.fields[id]=text;store.bytes=store.bytes-old+next;++summary_retained;
}catch(...){++summary_dropped;incomplete=summary_overflow=true;}}
namespace {
FILE* AppendExisting(const char* path){
#if defined(_WIN32)
    int fd=-1;const int error=_sopen_s(&fd,path,_O_WRONLY|_O_APPEND|_O_BINARY,_SH_DENYRW,0);if(error){errno=error;return NULL;}
    FILE* file=_fdopen(fd,"ab");if(!file)_close(fd);return file;
#else
    int fd=open(path,O_WRONLY|O_APPEND);if(fd<0)return NULL;FILE* file=fdopen(fd,"ab");if(!file)close(fd);return file;
#endif
}
void CompleteCycleFrame(const char* status){if(!capture_request.group || capture_request.group!=cycle.group || !cycle.active)return;
    if(strcmp(capture_request.role,"BASELINE") && strcmp(capture_request.role,"OPENED") && strcmp(capture_request.role,"RECOVERY"))return;
    cycle.queued=false;
    if(!status || strcmp(status,"COMPLETE")){RenderSnapshot_Event("UI_CYCLE",cycle.target,"CAPTURE_ABORTED",status);FinishCycle(status?status:"ABORTED");return;}
    RenderSnapshot_Event("UI_CYCLE",cycle.target,"FRAME_CAPTURED",capture_request.role);
    if(!strcmp(capture_request.role,"BASELINE")){cycle.baseline=true;cycle.stage=1;cycle.level=captured_level_generation;CopyField(cycle.status,sizeof(cycle.status),"WAITING_OPEN");}
    else if(!strcmp(capture_request.role,"OPENED")){cycle.opened=true;cycle.stage=2;CopyField(cycle.status,sizeof(cycle.status),cycle.baseline?"WAITING_RECOVERY":"BASELINE_MISSING_WAITING_RECOVERY");}
    else {cycle.stage=3;FinishCycle(cycle.baseline?"COMPLETE":"COMPLETE_BASELINE_MISSING");}
}
}
const char* RenderSnapshot_LastPath(){return failure?failure:last_path.c_str();}
void RenderSnapshot_Abort(const char* reason){
    if(!RenderSnapshot_DiagnosticActive())return;
    const uint64_t termination_group=cycle.active?cycle.group:pending?requests[0].group:0;
    RenderSnapshot_Event("DIAGNOSTIC","RECORDER","ABORTED",reason);aborted_requests+=pending+profile_remaining;pending=profile_remaining=0;
    if(cycle.active)FinishCycle(reason?reason:"ABORTED");
    if(g_render_snapshot_active){RenderSnapshot_End(reason?reason:"ABORTED");RefreshDiagnostic();return;}
    if(have_last_info){QueueRequest(reason?reason:"ABORTED",termination_group,"EVENT_ONLY_TERMINATION");event_only=true;RenderSnapshot_Begin(last_info);RenderSnapshot_End(reason?reason:"ABORTED");event_only=false;}
    else failure="ERROR: diagnostic cancelled before any render metadata; no frame captured";
    RefreshDiagnostic();
}
bool RenderSnapshot_GPUTiming(uint64_t capture,uint64_t engine,uint64_t render,const char* status,const double* values,uint32_t mask){
    if(!status || !capture || mask>127)return false;
    const bool ready=!strcmp(status,"READY"),pending_status=!strcmp(status,"NOT_READY");
    if(!ready && !pending_status && strcmp(status,"DISJOINT") && strcmp(status,"TIMEOUT") && strcmp(status,"UNSUPPORTED") && strcmp(status,"CANCELLED") && strcmp(status,"DEVICE_RESET"))return false;
    if(ready){if(!values)return false;for(unsigned i=0;i<7;++i)if((mask&(1u<<i)) && (!std::isfinite(values[i]) || values[i]<0))return false;}
    try{if(g_render_snapshot_active && capture_request.id==capture){if(info.frame!=engine || captured_render_id!=render || gpu_completed)return false;
            RenderSnapshot_Text("GPU_TIMING","status",status);RenderSnapshot_U64("GPU_TIMING","capture_id",capture);RenderSnapshot_U64("GPU_TIMING","engine_frame",engine);RenderSnapshot_U64("GPU_TIMING","render_frame",render);RenderSnapshot_U64("GPU_TIMING","valid_stage_mask",mask);
            for(unsigned i=0;i<7;++i){char key[32];snprintf(key,sizeof(key),"stage%u_milliseconds",i);if(!ready)RenderSnapshot_Text("GPU_TIMING",key,"NOT_MEASURED");else if(!(mask&(1u<<i)))RenderSnapshot_Text("GPU_TIMING",key,"NOT_APPLICABLE");else RenderSnapshot_Value("GPU_TIMING",key,values[i]);}
            gpu_completed=!pending_status;gpu_completion_status=status;return true;}
        for(auto& saved:closed_captures)if(saved.id==capture){if(saved.engine!=engine || saved.render!=render || saved.timing_done || pending_status)return false;
            std::ostringstream out;WriteTiming(out,capture,engine,render,status,values,mask);const std::string record=out.str();FILE* file=AppendExisting(saved.path.c_str());if(!file){++timing_append_failures;return false;}
            const size_t written=fwrite(record.data(),1,record.size(),file);const int closed=fclose(file);if(written!=record.size() || closed){++timing_append_failures;return false;}saved.timing_done=true;return true;}
    }catch(...){++timing_append_failures;}return false;
}
void RenderSnapshot_End(const char* status){
    if(!g_render_snapshot_active)return;
    const bool was_cycle_capture=cycle.active && capture_request.group==cycle.group;
    CompleteCycleFrame(status);
    g_render_snapshot_active=false;
    g_render_snapshot_console=false;
    RefreshDiagnostic();
    try{std::vector<UIEvent> captured_events;uint64_t captured_event_observed=0,captured_event_dropped=0;
        {std::lock_guard<std::mutex> lock(event_mutex);captured_events.assign(ui_events,ui_events+ui_events_count);captured_event_observed=ui_events_observed;captured_event_dropped=ui_events_dropped;}
        ClosePass();const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frame_start).count();
        const std::string final_status=incomplete || captured_event_dropped || !status || strcmp(status,"COMPLETE")?"INCOMPLETE":"COMPLETE";
        const std::chrono::steady_clock::time_point report_format_start=std::chrono::steady_clock::now();
        std::ostringstream out;out<<std::setprecision(10)<<"[SNAPSHOT]\nschema=3\ncomparable_schema=2\nrenderer="<<Clean(info.renderer,sizeof(info.renderer))<<"\napi="<<Clean(info.api,sizeof(info.api))
            <<"\nframe="<<info.frame<<"\nsession_id="<<session_id<<"\ncapture_id="<<capture_request.id<<"\nrequest_id="<<capture_request.id<<"\ngroup_id="<<capture_request.group<<"\nrequest_reason="<<capture_request.reason<<"\nscenario_role="<<capture_request.role<<"\nstatus="<<final_status<<"\ncompletion_reason="<<Clean(status,96)<<"\nbuild="<<Clean(info.build,sizeof(info.build))
            <<"\nsource_sha256="<<Clean(info.source_id,sizeof(info.source_id))<<"\nshaders_sha256="<<Clean(info.shader_id,sizeof(info.shader_id))
            <<"\nadapter="<<Clean(info.adapter,sizeof(info.adapter))<<"\nconsole_visible="<<info.console_visible<<"\ngame_paused="<<info.paused<<"\nhud="<<info.hud
            <<"\n\n[SCENE]\nscene="<<Clean(info.scene,sizeof(info.scene))<<"\nlevel="<<Clean(info.level,sizeof(info.level))<<"\nweather="<<Clean(info.weather,sizeof(info.weather))
            <<"\nengine_time="<<info.time<<"\ngame_time_seconds="<<info.game_time<<"\nwind_strength="<<info.wind<<"\nsun_direction="<<info.sun[0]<<","<<info.sun[1]<<","<<info.sun[2]
            <<"\n\n[CAMERA]\nposition="<<info.position[0]<<","<<info.position[1]<<","<<info.position[2]<<"\ndirection="<<info.direction[0]<<","<<info.direction[1]<<","<<info.direction[2]
            <<"\nfov="<<info.fov<<"\nresolution="<<info.width<<"x"<<info.height<<"\n\n[MEASUREMENT_LIMITS]\nvisible_pixels=NOT_INSTRUMENTED\nvisible_objects=UNKNOWN\nbackground_culling=NOT_INSTRUMENTED\n"
            <<"gpu_timing="<<gpu_completion_status<<"\nprocess_gpu_memory=NOT_AVAILABLE\ntexture_slots_8_15=SEE_RESOURCE_BINDING_RECORDS; absent fields NOT_INSTRUMENTED\n"
            <<"cpu_timing=INSTRUMENTED_FRAME_INCLUDES_TRACE_OVERHEAD\ncpu_frame_ms_scope=BEGIN_TO_END_HOOK; final report serialization and file IO excluded\ncoverage=BACKEND_DRAW_CALLS; direct external API calls NOT_INSTRUMENTED\n";
        out<<"visibility_notes=AGGREGATED_SOURCE_OBSERVATIONS; counts are events, not unique objects\n"
            <<"candidate_counts=EXPLICIT_CANDIDATE_NOTES_ONLY; never inferred from draw callbacks\n"
            <<"classified_candidates=SOURCE_GUARANTEES_CULLED_OR_SELECTED_OUTCOME; zero opposite count is known\n"
            <<"summary_values=LAST_VALUE_PER_SECTION_KEY\ntrace_policy=FIXED_PASS_BUDGETS; chronological retained draw records\n"
            <<"semantic_type=SOURCE_PROVENANCE; UNKNOWN when not instrumented\nuv_samples=AT_MOST_FOUR_SOURCE_PAIRS; not texture-coordinate bounds\n"
            <<"draw_aggregates=PASS_LIGHT_PHASE_SEMANTIC_MATERIAL_ACTUAL_SHADER_TEXTURE_STATE_ROUTE; mesh IDs are samples only\n"
            <<"draw_aggregate_identity=NO_GPU_HANDLES; unnamed texture/shader identities remain UNKNOWN\n"
            <<"draw_aggregate_samples=FIRST_FOUR_DISTINCT_MESH_RESOURCE_ROUTE_UV_REPRESENTATIVES; not all geometry\n";
        out<<"\n[FRAME_IDENTITY]\nsession_id="<<session_id<<"\ncapture_id="<<capture_request.id<<"\nrequest_id="<<capture_request.id
            <<"\ngroup_id="<<capture_request.group<<"\nrequest_reason="<<capture_request.reason<<"\nscenario_role="<<capture_request.role
            <<"\nrequest_timestamp_us="<<capture_request.requested_us<<"\ncapture_timestamp_us="<<captured_us<<"\nengine_frame="<<info.frame
            <<"\nrender_frame="<<captured_render_id<<"\npresent_frame="<<(captured_present_known?(captured_present_submitted?std::to_string(captured_present_id):"NOT_SUBMITTED"):"NOT_INSTRUMENTED")
            <<"\npresent_status="<<(captured_present_known?(captured_present_submitted?"SUBMITTED":"NOT_SUBMITTED"):"NOT_INSTRUMENTED")
            <<"\nlast_present_observed="<<(present_id_known?std::to_string(present_id):"NOT_INSTRUMENTED")
            <<"\nlevel_generation="<<captured_level_generation<<"\ndevice_generation="<<captured_device_generation
            <<"\nframe_scope="<<(event_only?"EVENT_ONLY_TERMINATION; no graphics frame captured":"SINGLE_RENDER_FRAME")
            <<"\nscene_metadata_source="<<(event_only?"LAST_OBSERVED_FRAME; termination context only":"CURRENT_BEGIN_FRAME")
            <<"\nexternal_source_age_policy=SOURCE_RECORD_MUST_NAME_ENGINE_FRAME_AND_RENDER_AGE; absent source age UNKNOWN\n";
        out<<"\n[COMPLETENESS]\nframe_capture="<<(event_only?"NOT_CAPTURED":status && !strcmp(status,"COMPLETE")?"COMPLETE":"INCOMPLETE")
            <<"\nsummary_integrity="<<(summary_overflow || invalid_draws || source_telemetry_losses?"INCOMPLETE":"COMPLETE")
            <<"\naggregate_attribution="<<(frame_profile?"NOT_REQUESTED_COUNTER_MODE":aggregation_overflow || draw_aggregate_failures || draw_aggregate_coalesced?"INCOMPLETE":"COMPLETE")
            <<"\nchronological_trace="<<(frame_profile?"NOT_REQUESTED_COUNTER_MODE":dropped_draws || serialization_failures?"INCOMPLETE":"COMPLETE")
            <<"\nrepresentative_dictionaries="<<(frame_profile?"NOT_REQUESTED_COUNTER_MODE":dictionary_overflow?"INCOMPLETE":"COMPLETE")
            <<"\nevent_tape="<<(captured_event_dropped?"INCOMPLETE":"COMPLETE")
            <<"\nui_transition="<<(capture_request.group==cycle.group && capture_request.group?cycle.status:"NOT_REQUESTED")
            <<"\ngpu_timing="<<gpu_completion_status<<"\ngpu_timing_completion=LATEST_MATCHING_COMPLETION_SUPERSEDES_INITIAL_VALIDITY; graphics unchanged\n"
            <<"render_success=NOT_INFERRED_FROM_CAPTURE_COMPLETENESS\n";
        out<<"\n[CAPTURE_MODE]\nmode="<<(frame_profile?"COUNTERS_ONLY":"DETAILED_SNAPSHOT")
            <<"\nremaining_profile_frames="<<profile_remaining<<"\nprofile_frame_limit=120\nprofile_dictionary_updates_omitted="<<profile_dictionary_skips
            <<"\nperformance_interpretation=INSTRUMENTED_CAPTURE_COST; not ordinary game FPS\nrequest_queue_limit=16\nrequest_rejections="<<request_rejections<<"\naborted_requests="<<aborted_requests<<"\ngpu_timing_append_failures_prior="<<timing_append_failures<<"\n";
        if(capture_request.group && capture_request.group==cycle.group){out<<"\n[UI_TRANSITION]\ngroup_id="<<cycle.group<<"\ntarget="<<cycle.target<<"\nrole="<<capture_request.role
            <<"\nstatus="<<cycle.status<<"\nbaseline_missing="<<(cycle.baseline?0:1)<<"\nopened_observed="<<(cycle.opened?1:0)<<"\nstart_render_tick="<<cycle.start_tick
            <<"\ntimeout_render_frames="<<cycle.timeout<<"\ntimeout_wall_seconds=300\ntrigger=ACTUAL_SCREEN_STATE; input keys alone never trigger a phase\n";}
        {out<<"\n[EVENT_TAPE]\nobserved="<<captured_event_observed<<"\nretained="<<captured_events.size()<<"\ndropped="<<captured_event_dropped
            <<"\nlimit=256\nstatus="<<(captured_event_dropped?"INCOMPLETE":"COMPLETE")<<"\nscope=EXPLICIT_DIAGNOSTIC_REQUEST_OR_UI_CYCLE_ONLY\n";
            for(size_t i=0;i<captured_events.size();++i){const UIEvent& e=captured_events[i];out<<"\n[UI_EVENT]\nordinal="<<e.ordinal<<"\nrender_frame="<<e.render<<"\ntimestamp_us="<<e.when
                <<"\nkind="<<e.kind<<"\nsubject="<<e.subject<<"\naction="<<e.action<<"\nresult="<<e.result<<"\n";}}
        for(unsigned i=0;i<summary_count;++i){std::string section;for(const auto& field:summaries[i].fields){if(section!=field.first.first){section=field.first.first;out<<"\n["<<section<<"]\n";}out<<field.first.second<<"="<<field.second<<"\n";}}
        for(const auto& dictionary:dictionaries){out<<"\n[DICTIONARY "<<dictionary.name<<"]\nrecords="<<dictionary.records.size()<<"\nrecord_limit="<<dictionary.records_limit
            <<"\nfields_per_record_limit="<<dictionary.field_limit<<"\npayload_bytes="<<dictionary.bytes<<"\npayload_limit_bytes="<<dictionary.bytes_limit
            <<"\nupdates_observed="<<dictionary.observed<<"\nupdates_retained="<<dictionary.retained<<"\nupdates_dropped="<<dictionary.dropped<<"\nupdates_truncated="<<dictionary.truncated
            <<"\nstatus="<<(frame_profile?"NOT_REQUESTED_COUNTER_MODE":dictionary.dropped || dictionary.truncated?"INCOMPLETE":"COMPLETE")
            <<"\npolicy=FIRST_DISTINCT_RECORDS; last value per admitted record key; no claim of exhaustive geometry\n";
            for(const auto& record:dictionary.records){out<<"\n[RECORD "<<dictionary.name<<"]\nid="<<record.first<<"\ncapture_id="<<capture_request.id<<"\nrender_frame="<<captured_render_id<<"\n";
                for(const auto& field:record.second.fields)out<<field.first<<"="<<field.second<<"\n";}}
        out<<body;
        unsigned aggregate_index=0;
        for(const auto& entry:draw_aggregates){const DrawAggregate& a=entry.second;const DrawRoute& r=a.route;
            out<<"\n[DRAW_AGGREGATE "<<std::setfill('0')<<std::setw(6)<<++aggregate_index<<"]\ncoalesced="<<(a.coalesced?1:0)
                <<"\npass="<<r.pass<<"\nlight_id="<<r.light<<"\nlight_scope="<<r.scope<<"\nphase="<<r.phase<<"\nsemantic_type="<<r.semantic
                <<"\ncategory="<<r.category<<"\nrender_queue="<<r.queue<<"\nmaterial_id="<<r.material<<"\nmaterial_route_id="<<r.material_route<<"\nvertex_shader="<<r.vs<<"\npixel_shader="<<r.ps
                <<"\nskin_mode="<<r.skin<<"\nraw_states="<<r.states<<"\nsamplers="<<r.samplers
                <<"\nsource_flags_raw="<<(a.coalesced?"MULTIPLE":std::to_string(r.flags))
                <<"\npass_mask="<<(a.coalesced?"MULTIPLE":r.mask_known?std::to_string(r.mask):"NOT_INSTRUMENTED")
                <<"\ndetail_variant="<<(a.coalesced?"MULTIPLE":r.variant<3?std::to_string(r.variant):"NOT_INSTRUMENTED")
                <<"\ntopology_raw="<<(a.coalesced?"MULTIPLE":std::to_string(r.topology))<<"\n";
            for(unsigned i=0;i<8;++i)out<<"texture"<<i<<"="<<r.textures[i]<<"\n";
            WriteDrawTotals(out,a.totals);out<<"representative_samples="<<a.sample_count<<"\nrepresentative_observations_omitted="<<a.samples_omitted<<"\n";
            for(unsigned i=0;i<a.sample_count;++i){const DrawSample& sample=a.samples[i];const std::string prefix="sample"+std::to_string(i)+"_";
                out<<prefix<<"mesh_id="<<sample.mesh<<"\n"<<prefix<<"resource="<<sample.resource<<"\n"<<prefix<<"semantic_type="<<sample.semantic
                    <<"\n"<<prefix<<"semantic_evidence="<<sample.evidence<<"\n"<<prefix<<"material_id="<<sample.material<<"\n"<<prefix<<"vertex_shader="<<sample.vs
                    <<"\n"<<prefix<<"pixel_shader="<<sample.ps<<"\n"<<prefix<<"texture0="<<sample.texture0<<"\n"<<prefix<<"uv_source="<<sample.uv_source
                    <<"\n"<<prefix<<"geometry_id="<<sample.geometry<<"\n"<<prefix<<"ui_widget="<<sample.widget<<"\n"<<prefix<<"ui_origin="<<sample.ui_origin
                    <<"\n"<<prefix<<"uv_count="<<sample.uv_count<<"\n"<<prefix<<"start_index="<<sample.start_index<<"\n"<<prefix<<"index_count="<<sample.index_count
                    <<"\n"<<prefix<<"first_draw_event="<<sample.first<<"\n"<<prefix<<"last_draw_event="<<sample.last<<"\n"<<prefix<<"observations="<<sample.observations<<"\n";
                for(unsigned j=0;j<sample.uv_count;++j)out<<prefix<<"uv"<<j<<"="<<sample.uv[j*2]<<","<<sample.uv[j*2+1]<<"\n";}}
        out<<"\n[DRAW_AGGREGATE_TOTALS]\n";WriteDrawTotals(out,draw_aggregate_totals);
        out<<"groups="<<draw_aggregates.size()<<"\nroute_limit="<<max_draw_aggregates<<"\noverflow_bucket_limit="<<draw_budget_count<<"\n"
            <<"observations_aggregated="<<(frame_profile?0:draw_aggregate_totals.observed-draw_aggregate_failures)<<"\nobservations_lost="<<draw_aggregate_failures
            <<"\nobservations_coalesced="<<draw_aggregate_coalesced<<"\n";
        for(unsigned i=0;i<draw_budget_count;++i)out<<"route_limit_"<<budgets[i].name<<"="<<draw_aggregate_limits[i]<<"\n";
        for(const auto& entry:passes){const Pass& p=entry.second;const Budget& budget=TraceBudget(entry.first);
            out<<"\n[PASS "<<entry.first<<"]\ndraw_calls="<<p.calls<<"\ntriangles="<<p.triangles<<"\ncpu_ms="<<p.cpu_ms<<"\ngpu_ms=NOT_INSTRUMENTED\n"
               <<"draw_records_observed="<<p.observed<<"\ndraw_records_retained="<<p.retained<<"\ndraw_records_dropped="<<p.dropped
               <<"\ndraw_bytes_observed="<<p.bytes<<"\ndraw_bytes_retained="<<p.retained_bytes<<"\ndraw_bytes_dropped="<<p.dropped_bytes
               <<"\nfirst_lost_draw_event="<<p.first_lost<<"\nlast_lost_draw_event="<<p.last_lost
               <<"\ntrace_budget_group="<<(entry.first=="OTHER_PASSES"?"MULTIPLE":budget.name)
               <<"\ntrace_budget_bytes="<<(entry.first=="OTHER_PASSES"?"NOT_APPLICABLE":std::to_string(budget.limit))<<"\n";}
        for(const auto& entry:groups){const Group& g=entry.second;out<<"\n[PASS_LIGHT]\npass="<<g.pass<<"\nlight_id="<<g.light<<"\nlight_scope="<<g.scope<<"\nphase="<<g.phase<<"\n";
            WriteObservations(out,g.notes);out<<"draw_records_observed="<<g.observed<<"\nsubmitted_draw_calls="<<g.submitted<<"\nskipped_draw_calls="<<g.skipped<<"\ntriangles_submitted="<<g.triangles<<"\n";}
        for(const auto& entry:visibility){const Visibility& v=entry.second;out<<"\n[VISIBILITY_SUMMARY]\npass="<<v.group.pass<<"\nlight_id="<<v.group.light<<"\nlight_scope="<<v.group.scope<<"\nphase="<<v.group.phase
            <<"\ncategory="<<v.category<<"\nreason="<<v.reason<<"\n";WriteObservations(out,v.notes);
            out<<"resource_samples="<<v.samples_count<<"\nresource_sample_observations_omitted="<<v.samples_omitted<<"\n";
            for(uint32_t i=0;i<v.samples_count;++i)out<<"resource_sample"<<i<<"="<<v.samples[i]<<"\n";}
        out<<"\n[VISIBILITY_TOTALS]\nsource_note_events="<<visibility_seen<<"\n";WriteObservations(out,note_totals);
        size_t summary_bytes=0,summary_fields=0;for(unsigned i=0;i<summary_count;++i){summary_bytes+=summaries[i].bytes;summary_fields+=summaries[i].fields.size();}
        out<<"\n[TRACE_LOSS]\ndraw_records_observed="<<draws<<"\ndraw_records_retained="<<events<<"\ndraw_records_dropped="<<dropped_draws
            <<"\ndraw_bytes_observed="<<draw_bytes<<"\ndraw_bytes_retained="<<body.size()<<"\ndraw_bytes_dropped="<<dropped_bytes
            <<"\nfirst_lost_draw_event="<<first_lost<<"\nfirst_lost_pass="<<(first_lost?first_lost_pass:"NONE")
            <<"\nlast_lost_draw_event="<<last_lost<<"\nlast_lost_pass="<<(last_lost?last_lost_pass:"NONE")
            <<"\nomission_reason="<<(frame_profile?"COUNTER_MODE_NO_TRACE":dropped_draws?"TRACE_BUDGET_OR_SERIALIZATION_FAILURE":"NONE")
            <<"\nserialization_failures="<<serialization_failures<<"\ninvalid_draw_callbacks="<<invalid_draws<<"\ntrace_overflow="<<(overflowed?1:0)
            <<"\nsource_telemetry_loss_events="<<source_telemetry_losses
            <<"\ndraw_byte_accounting="<<(serialization_failures?"PARTIAL; failed serialization has unknown bytes":"EXACT_SERIALIZED_RECORD_BYTES")
            <<"\nsummary_overflow="<<(summary_overflow?1:0)<<"\naggregation_overflow="<<(aggregation_overflow?1:0)
            <<"\ndraw_aggregate_observations_lost="<<draw_aggregate_failures<<"\ndraw_aggregate_observations_coalesced="<<draw_aggregate_coalesced
            <<"\nsummary_updates_observed="<<summary_seen<<"\nsummary_updates_retained="<<summary_retained<<"\nsummary_updates_dropped="<<summary_dropped
            <<"\nsummary_updates_omitted="<<summary_omitted<<"\nsummary_accounting=observed equals retained plus dropped plus intentional_mode_omissions"
            <<"\nsummary_fields="<<summary_fields<<"\nsummary_payload_bytes="<<summary_bytes<<"\npass_aggregate_limit="<<max_passes<<"\npass_light_aggregate_limit="<<max_groups
            <<"\nvisibility_aggregate_limit="<<max_visibility<<"\naggregate_overflow_policy=COALESCE_INTO_OTHER; exact global source counts retained\naggregate_overflow_buckets=ONE_ADDITIONAL_PER_MAP\n";
        out<<"\n[END]\nstatus="<<final_status<<"\ndraw_events="<<draws<<"\ntriangles_submitted="<<triangles<<"\ncpu_frame_ms="<<ms<<"\nevents="<<events<<"\nbuffer_bytes="<<body.size()<<"\noverflow="<<(overflowed?1:0)<<"\n";
        const std::string report=out.str();const double report_format_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-report_format_start).count();time_t now=time(NULL);tm local={};
#if defined(_WIN32)
        localtime_s(&local,&now);
#else
        localtime_r(&now,&local);
#endif
        char stamp[64];strftime(stamp,sizeof(stamp),"snap%d.%m.%Y.%H.%M.%S",&local);
        std::string directory=Clean(info.directory,sizeof(info.directory));if(!directory.empty() && directory.back()!='/' && directory.back()!='\\')directory+='/';
        FILE* file=NULL;std::string path;
        for(unsigned suffix=0;suffix<10000 && !file;++suffix){char tail[32];if(suffix)snprintf(tail,sizeof(tail),"_%02u.log",suffix);else strcpy(tail,".log");path=directory+stamp+tail;file=Exclusive(path.c_str());if(!file && errno!=EEXIST)break;}
        if(!file){failure="ERROR: cannot create snapshot file";if(was_cycle_capture)FinishCycle("IO_ERROR");aborted_requests+=pending+profile_remaining;pending=profile_remaining=0;ClearStorage();frame_profile=false;RefreshDiagnostic();return;}
        const std::chrono::steady_clock::time_point write_start=std::chrono::steady_clock::now();
        const size_t written=fwrite(report.data(),1,report.size(),file);const int flushed=fflush(file);
        const double payload_write_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-write_start).count();
        char footer[1024];const int footer_size=snprintf(footer,sizeof(footer),
            "\n[TELEMETRY_COST]\ncapture_id=%llu\nreport_format_ms=%.12g\nreport_payload_write_flush_ms=%.12g\npayload_bytes=%llu"
            "\nformat_scope=FINAL_REPORT_FORMAT_AND_STRING; footer excluded\nwrite_scope=PAYLOAD_FWRITE_AND_FFLUSH; footer and close excluded; no fsync guarantee"
            "\nperformance_scope=DIAGNOSTIC_OVERHEAD; not ordinary game FPS\n",
            static_cast<unsigned long long>(capture_request.id),report_format_ms,payload_write_ms,static_cast<unsigned long long>(report.size()));
        const size_t footer_bytes=footer_size>0 && static_cast<size_t>(footer_size)<sizeof(footer)?static_cast<size_t>(footer_size):0;
        const size_t footer_written=written==report.size() && flushed==0 && footer_bytes?fwrite(footer,1,footer_bytes,file):0;
        const int closed=fclose(file);
        if(written==report.size() && flushed==0 && footer_bytes && footer_written==footer_bytes && closed==0){last_path=path;ClosedCapture& saved=closed_captures[closed_cursor++%16];saved.id=capture_request.id;saved.engine=info.frame;saved.render=captured_render_id;saved.path=path;saved.timing_done=gpu_completed || event_only;}
        else {failure="ERROR: incomplete snapshot write";if(was_cycle_capture)FinishCycle("IO_ERROR");aborted_requests+=pending+profile_remaining;pending=profile_remaining=0;}
        ClearStorage();
    }catch(...){failure="ERROR: snapshot allocation/write failure";if(was_cycle_capture)FinishCycle("SERIALIZATION_ERROR");aborted_requests+=pending+profile_remaining;pending=profile_remaining=0;ClearStorage();}
    frame_profile=false;RefreshDiagnostic();
}
#endif
