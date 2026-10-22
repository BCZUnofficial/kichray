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
bool incomplete=false,overflowed=false,summary_overflow=false,aggregation_overflow=false;
const char* failure=NULL;
uint64_t draws=0,triangles=0,events=0,draw_bytes=0,dropped_draws=0,dropped_bytes=0;
uint64_t first_lost=0,last_lost=0,serialization_failures=0,invalid_draws=0;
uint64_t summary_seen=0,summary_retained=0,summary_dropped=0,visibility_seen=0;
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
Summary summaries[5];
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
struct DrawRoute {std::string pass,light,scope,phase,semantic,category,queue,material,vs,ps,skin,textures[8],states,samplers;
    uint32_t flags=0,mask=0,mask_known=0,variant=~uint32_t(0),topology=0;};
struct DrawSample {char mesh[160],resource[257],evidence[65],uv_source[49],material[160],semantic[32],vs[96],ps[96],texture0[128];
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
std::string DrawRouteKey(const DrawRoute& r){std::string key;const std::string* fields[]={&r.pass,&r.light,&r.scope,&r.phase,&r.semantic,&r.category,&r.queue,&r.material,&r.vs,&r.ps,&r.skin,&r.states,&r.samplers};
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
    sample.uv_count=provenance.uv_count>4?4:provenance.uv_count;memcpy(sample.uv,provenance.uv,sample.uv_count*2*sizeof(float));
    sample.start_index=d.start_index;sample.index_count=d.index_count;
    for(unsigned i=0;i<aggregate.sample_count;++i){DrawSample& old=aggregate.samples[i];
        if(!strcmp(old.mesh,sample.mesh) && !strcmp(old.resource,sample.resource) && !strcmp(old.material,sample.material) && !strcmp(old.semantic,sample.semantic)
            && !strcmp(old.vs,sample.vs) && !strcmp(old.ps,sample.ps) && !strcmp(old.texture0,sample.texture0) && !strcmp(old.uv_source,sample.uv_source)
            && old.start_index==sample.start_index && old.index_count==sample.index_count && old.uv_count==sample.uv_count && !memcmp(old.uv,sample.uv,sample.uv_count*2*sizeof(float))){++old.observations;old.last=draws;return;}}
    if(aggregate.sample_count==4){++aggregate.samples_omitted;return;}
    sample.first=sample.last=draws;sample.observations=1;aggregate.samples[aggregate.sample_count++]=sample;
}
void AggregateDraw(const RenderSnapshotDraw& d,uint64_t tris){draw_aggregate_totals.Add(d,tris,draws);try{
    DrawRoute route;route.pass=pass;route.light=light_id;route.scope=light_scope;route.phase=phase;
    route.semantic=provenance.semantic_type[0]?Clean(provenance.semantic_type,sizeof(provenance.semantic_type)):"UNKNOWN";route.category=category;
    route.queue=provenance.queue[0]?Clean(provenance.queue,sizeof(provenance.queue)):"UNKNOWN";
    route.material=provenance.material_id[0]?Clean(provenance.material_id,sizeof(provenance.material_id)):material;
    route.vs=d.vertex_shader[0]?Clean(d.vertex_shader,sizeof(d.vertex_shader)):vertex_shader;route.ps=d.pixel_shader[0]?Clean(d.pixel_shader,sizeof(d.pixel_shader)):pixel_shader;
    route.skin=d.skin[0]?Clean(d.skin,sizeof(d.skin)):skin;route.states=Clean(d.states,sizeof(d.states));route.samplers=Clean(d.samplers,sizeof(d.samplers));
    for(unsigned i=0;i<8;++i)route.textures[i]=d.texture_names[i][0]?Clean(d.texture_names[i],sizeof(d.texture_names[i])):texture_names[i];
    route.flags=provenance.flags;route.mask=provenance.pass_mask;route.mask_known=provenance.pass_mask_known;route.variant=detail_variant;route.topology=d.topology;
    const std::string key=DrawRouteKey(route);auto found=draw_aggregates.find(key);
    if(found==draw_aggregates.end()){const size_t budget=static_cast<size_t>(&TraceBudget(pass)-budgets);
        if(draw_aggregate_counts[budget]<draw_aggregate_limits[budget]){DrawAggregate value;value.route=route;
            found=draw_aggregates.insert(std::make_pair(key,value)).first;++draw_aggregate_counts[budget];}
        else{const std::string other="OTHER_DRAW_ROUTES/"+std::string(budgets[budget].name);found=draw_aggregates.find(other);
            if(found==draw_aggregates.end()){DrawAggregate value;value.coalesced=true;value.route.pass=budgets[budget].name;
                value.route.light=value.route.scope=value.route.phase=value.route.semantic=value.route.category=value.route.queue=value.route.material=value.route.vs=value.route.ps=value.route.skin=value.route.states=value.route.samplers="MULTIPLE";
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
void ClearStorage(){std::string().swap(body);passes.clear();groups.clear();visibility.clear();draw_aggregates.clear();for(unsigned i=0;i<summary_count;++i){summaries[i].fields.clear();summaries[i].bytes=0;}}
FILE* Exclusive(const char* path){
#if defined(_WIN32)
    int fd=-1;const int error=_sopen_s(&fd,path,_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_SH_DENYRW,_S_IREAD|_S_IWRITE);if(error){errno=error;return NULL;}
    FILE* file=_fdopen(fd,"wb");if(!file)_close(fd);return file;
#else
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return NULL;FILE* file=fdopen(fd,"wb");if(!file)close(fd);return file;
#endif
}
}
bool RenderSnapshot_Request(){if(pending>=16)return false;++pending;return true;}
bool RenderSnapshot_Wanted(){return pending!=0 || g_render_snapshot_active;}
void RenderSnapshot_Begin(const RenderSnapshotInfo& input){
    if(g_render_snapshot_active || !pending)return;
    --pending;
    try{failure=NULL;info=input;ClearStorage();pass.clear();category="UNKNOWN";resource="UNKNOWN";material="UNKNOWN";
        vertex_shader=pixel_shader="UNKNOWN";skin="NOT_INSTRUMENTED";g_render_snapshot_console=false;for(unsigned i=0;i<8;++i)texture_names[i]="UNKNOWN";
        incomplete=overflowed=summary_overflow=aggregation_overflow=false;draws=triangles=events=draw_bytes=dropped_draws=dropped_bytes=0;
        first_lost=last_lost=serialization_failures=invalid_draws=0;summary_seen=summary_retained=summary_dropped=visibility_seen=source_telemetry_losses=0;
        first_lost_pass.clear();last_lost_pass.clear();first_lost_pass.reserve(96);last_lost_pass.reserve(96);
        note_totals=Observations();ResetObject();light_id=light_scope=phase="NONE";
        draw_aggregate_totals=DrawTotals();draw_aggregate_failures=draw_aggregate_coalesced=0;memset(draw_aggregate_counts,0,sizeof(draw_aggregate_counts));
        for(auto& b:budgets)b.bytes=b.events=0;
        frame_start=pass_start=std::chrono::steady_clock::now();g_render_snapshot_active=true;RenderSnapshot_Pass("FRAME_SETUP");
        RenderSnapshot_Text("DETAIL_RENDERER","measurement_scope","INSTRUMENTED_FIELDS_ONLY; absent metrics NOT_INSTRUMENTED");
        RenderSnapshot_Text("MEMORY","measurement_scope","INSTRUMENTED_FIELDS_ONLY; absent metrics NOT_AVAILABLE; process GPU memory NOT_AVAILABLE");
    }catch(...){g_render_snapshot_active=false;failure="ERROR: snapshot allocation failure";}
}
void RenderSnapshot_Pass(const char* name){if(!g_render_snapshot_active)return;try{ClosePass();pass=Clean(name,96);pass_start=std::chrono::steady_clock::now();category="UNKNOWN";resource=material="UNKNOWN";ResetObject();light_id=light_scope=phase="NONE";}catch(...){incomplete=true;}}
void RenderSnapshot_Context(const char* kind,const char* name,const char* shader){if(!g_render_snapshot_active)return;try{category=Clean(kind,64);resource=Clean(name,256);material=Clean(shader,256);skin="NOT_INSTRUMENTED";ResetObject();}catch(...){incomplete=true;}}
void RenderSnapshot_Object(const char* semantic,const char* mesh,const char* mat,const char* evidence,uint32_t flags){if(!g_render_snapshot_active)return;try{
    provenance.uv_count=0;provenance.uv_source[0]=0;memset(provenance.uv,0,sizeof(provenance.uv));detail_variant=~uint32_t(0);
    Copy(provenance.semantic_type,sizeof(provenance.semantic_type),semantic);Copy(provenance.mesh_id,sizeof(provenance.mesh_id),mesh);Copy(provenance.material_id,sizeof(provenance.material_id),mat);
    Copy(provenance.evidence,sizeof(provenance.evidence),evidence);provenance.flags=flags;}catch(...){incomplete=true;}}
void RenderSnapshot_Queue(const char* name){if(!g_render_snapshot_active)return;try{Copy(provenance.queue,sizeof(provenance.queue),name);}catch(...){incomplete=true;}}
void RenderSnapshot_PassMask(uint32_t mask){if(!g_render_snapshot_active)return;provenance.pass_mask=mask;provenance.pass_mask_known=1;}
void RenderSnapshot_UV(const float* pairs,uint32_t count,const char* source){if(!g_render_snapshot_active)return;try{provenance.uv_count=pairs?(count>4?4:count):0;
    memset(provenance.uv,0,sizeof(provenance.uv));if(provenance.uv_count)memcpy(provenance.uv,pairs,provenance.uv_count*2*sizeof(float));Copy(provenance.uv_source,sizeof(provenance.uv_source),source);}catch(...){incomplete=true;}}
void RenderSnapshot_GetProvenance(RenderSnapshotProvenance* out){if(out)*out=g_render_snapshot_active?provenance:RenderSnapshotProvenance();}
void RenderSnapshot_SetProvenance(const RenderSnapshotProvenance* value){if(!g_render_snapshot_active)return;provenance=value?*value:RenderSnapshotProvenance();if(provenance.uv_count>4)provenance.uv_count=4;}
void RenderSnapshot_Light(const char* id,const char* scope){if(!g_render_snapshot_active)return;try{light_id=id?Clean(id,96):"NONE";light_scope=scope?Clean(scope,64):"NONE";}catch(...){incomplete=true;}}
void RenderSnapshot_Phase(const char* id){if(!g_render_snapshot_active)return;try{phase=id?Clean(id,96):"NONE";}catch(...){incomplete=true;}}
void RenderSnapshot_DetailVariant(uint32_t variant){if(g_render_snapshot_active)detail_variant=variant<3?variant:~uint32_t(0);}
void RenderSnapshot_Shaders(const char* vs,const char* ps){if(!g_render_snapshot_active)return;try{vertex_shader=Clean(vs,96);pixel_shader=Clean(ps,96);}catch(...){incomplete=true;}}
void RenderSnapshot_ShaderStage(bool vertex,const char* name){if(!g_render_snapshot_active)return;try{(vertex?vertex_shader:pixel_shader)=Clean(name,96);}catch(...){incomplete=true;}}
void RenderSnapshot_Texture(uint32_t stage,const char* name){if(!g_render_snapshot_active || stage>=8)return;try{texture_names[stage]=Clean(name,128);}catch(...){incomplete=true;}}
void RenderSnapshot_Skin(const char* mode){if(!g_render_snapshot_active)return;try{skin=Clean(mode,32);}catch(...){incomplete=true;}}
void SNAPSHOT_CALL RenderSnapshot_Draw(const RenderSnapshotDraw* d){
    if(!g_render_snapshot_active)return;
    if(!d || d->struct_size!=sizeof(*d)){++invalid_draws;incomplete=true;return;}
    ++draws;Pass* current=NULL;bool accounted=false;size_t serialized_bytes=0;
    const uint64_t tris=d->topology==4?uint64_t(d->index_count?d->index_count:d->vertex_count)/3*d->instance_count:
        d->topology==5 || d->topology==6?uint64_t((d->index_count?d->index_count:d->vertex_count)>2?(d->index_count?d->index_count:d->vertex_count)-2:0)*d->instance_count:0;
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
void RenderSnapshot_Note(const char* status,const char* kind,const char* name,const char* reason){if(!g_render_snapshot_active)return;++visibility_seen;Observe(note_totals,status);try{
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
void RenderSnapshot_Value(const char* section,const char* key,double value){if(!g_render_snapshot_active)return;try{std::ostringstream out;out<<std::setprecision(12)<<value;RenderSnapshot_Text(section,key,out.str().c_str());}catch(...){incomplete=summary_overflow=true;}}
void RenderSnapshot_Text(const char* section,const char* key,const char* value){if(!g_render_snapshot_active)return;++summary_seen;try{
    const std::string sec=Clean(section,64),name=Clean(key,96),text=Clean(value,2048);const unsigned index=sec=="DETAIL_RENDERER"?0:sec=="MEMORY"?1:sec=="SETTINGS"?2:sec=="MODEL_RENDERER"?4:3;
    if(sec=="TELEMETRY_LOSS"){incomplete=true;++source_telemetry_losses;}
    Summary& store=summaries[index];const auto id=std::make_pair(sec,name);auto found=store.fields.find(id);
    const size_t old=found==store.fields.end()?0:sec.size()+name.size()+found->second.size();const size_t next=sec.size()+name.size()+text.size();
    const size_t key_limit=index==3?1024:128,byte_limit=index==3?512u*1024u:128u*1024u;
    if((found==store.fields.end() && store.fields.size()>=key_limit) || next>byte_limit-(store.bytes-old)){++summary_dropped;incomplete=summary_overflow=true;return;}
    store.fields[id]=text;store.bytes=store.bytes-old+next;++summary_retained;
}catch(...){++summary_dropped;incomplete=summary_overflow=true;}}
const char* RenderSnapshot_LastPath(){return failure?failure:last_path.c_str();}
void RenderSnapshot_End(const char* status){
    if(!g_render_snapshot_active)return;
    g_render_snapshot_active=false;
    g_render_snapshot_console=false;
    try{ClosePass();const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frame_start).count();
        const std::string final_status=incomplete || !status || strcmp(status,"COMPLETE")?"INCOMPLETE":"COMPLETE";
        std::ostringstream out;out<<std::setprecision(10)<<"[SNAPSHOT]\nschema=2\nrenderer="<<Clean(info.renderer,sizeof(info.renderer))<<"\napi="<<Clean(info.api,sizeof(info.api))
            <<"\nframe="<<info.frame<<"\nstatus="<<final_status<<"\ncompletion_reason="<<Clean(status,96)<<"\nbuild="<<Clean(info.build,sizeof(info.build))
            <<"\nsource_sha256="<<Clean(info.source_id,sizeof(info.source_id))<<"\nshaders_sha256="<<Clean(info.shader_id,sizeof(info.shader_id))
            <<"\nadapter="<<Clean(info.adapter,sizeof(info.adapter))<<"\nconsole_visible="<<info.console_visible<<"\ngame_paused="<<info.paused<<"\nhud="<<info.hud
            <<"\n\n[SCENE]\nscene="<<Clean(info.scene,sizeof(info.scene))<<"\nlevel="<<Clean(info.level,sizeof(info.level))<<"\nweather="<<Clean(info.weather,sizeof(info.weather))
            <<"\nengine_time="<<info.time<<"\ngame_time_seconds="<<info.game_time<<"\nwind_strength="<<info.wind<<"\nsun_direction="<<info.sun[0]<<","<<info.sun[1]<<","<<info.sun[2]
            <<"\n\n[CAMERA]\nposition="<<info.position[0]<<","<<info.position[1]<<","<<info.position[2]<<"\ndirection="<<info.direction[0]<<","<<info.direction[1]<<","<<info.direction[2]
            <<"\nfov="<<info.fov<<"\nresolution="<<info.width<<"x"<<info.height<<"\n\n[MEASUREMENT_LIMITS]\nvisible_pixels=NOT_INSTRUMENTED\nvisible_objects=UNKNOWN\nbackground_culling=NOT_INSTRUMENTED\n"
            <<"gpu_timing=NOT_INSTRUMENTED\nprocess_gpu_memory=NOT_AVAILABLE\ntexture_slots_8_15=NOT_INSTRUMENTED\n"
            <<"cpu_timing=INSTRUMENTED_FRAME_INCLUDES_TRACE_OVERHEAD\ncoverage=BACKEND_DRAW_CALLS; direct external API calls NOT_INSTRUMENTED\n";
        out<<"visibility_notes=AGGREGATED_SOURCE_OBSERVATIONS; counts are events, not unique objects\n"
            <<"candidate_counts=EXPLICIT_CANDIDATE_NOTES_ONLY; never inferred from draw callbacks\n"
            <<"classified_candidates=SOURCE_GUARANTEES_CULLED_OR_SELECTED_OUTCOME; zero opposite count is known\n"
            <<"summary_values=LAST_VALUE_PER_SECTION_KEY\ntrace_policy=FIXED_PASS_BUDGETS; chronological retained draw records\n"
            <<"semantic_type=SOURCE_PROVENANCE; UNKNOWN when not instrumented\nuv_samples=AT_MOST_FOUR_SOURCE_PAIRS; not texture-coordinate bounds\n"
            <<"draw_aggregates=PASS_LIGHT_PHASE_SEMANTIC_MATERIAL_ACTUAL_SHADER_TEXTURE_STATE_ROUTE; mesh IDs are samples only\n"
            <<"draw_aggregate_identity=NO_GPU_HANDLES; unnamed texture/shader identities remain UNKNOWN\n"
            <<"draw_aggregate_samples=FIRST_FOUR_DISTINCT_MESH_RESOURCE_ROUTE_UV_REPRESENTATIVES; not all geometry\n";
        for(unsigned i=0;i<summary_count;++i){std::string section;for(const auto& field:summaries[i].fields){if(section!=field.first.first){section=field.first.first;out<<"\n["<<section<<"]\n";}out<<field.first.second<<"="<<field.second<<"\n";}}
        out<<body;
        unsigned aggregate_index=0;
        for(const auto& entry:draw_aggregates){const DrawAggregate& a=entry.second;const DrawRoute& r=a.route;
            out<<"\n[DRAW_AGGREGATE "<<std::setfill('0')<<std::setw(6)<<++aggregate_index<<"]\ncoalesced="<<(a.coalesced?1:0)
                <<"\npass="<<r.pass<<"\nlight_id="<<r.light<<"\nlight_scope="<<r.scope<<"\nphase="<<r.phase<<"\nsemantic_type="<<r.semantic
                <<"\ncategory="<<r.category<<"\nrender_queue="<<r.queue<<"\nmaterial_id="<<r.material<<"\nvertex_shader="<<r.vs<<"\npixel_shader="<<r.ps
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
                    <<"\n"<<prefix<<"uv_count="<<sample.uv_count<<"\n"<<prefix<<"start_index="<<sample.start_index<<"\n"<<prefix<<"index_count="<<sample.index_count
                    <<"\n"<<prefix<<"first_draw_event="<<sample.first<<"\n"<<prefix<<"last_draw_event="<<sample.last<<"\n"<<prefix<<"observations="<<sample.observations<<"\n";
                for(unsigned j=0;j<sample.uv_count;++j)out<<prefix<<"uv"<<j<<"="<<sample.uv[j*2]<<","<<sample.uv[j*2+1]<<"\n";}}
        out<<"\n[DRAW_AGGREGATE_TOTALS]\n";WriteDrawTotals(out,draw_aggregate_totals);
        out<<"groups="<<draw_aggregates.size()<<"\nroute_limit="<<max_draw_aggregates<<"\noverflow_bucket_limit="<<draw_budget_count<<"\n"
            <<"observations_aggregated="<<draw_aggregate_totals.observed-draw_aggregate_failures<<"\nobservations_lost="<<draw_aggregate_failures
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
            <<"\nserialization_failures="<<serialization_failures<<"\ninvalid_draw_callbacks="<<invalid_draws<<"\ntrace_overflow="<<(overflowed?1:0)
            <<"\nsource_telemetry_loss_events="<<source_telemetry_losses
            <<"\ndraw_byte_accounting="<<(serialization_failures?"PARTIAL; failed serialization has unknown bytes":"EXACT_SERIALIZED_RECORD_BYTES")
            <<"\nsummary_overflow="<<(summary_overflow?1:0)<<"\naggregation_overflow="<<(aggregation_overflow?1:0)
            <<"\ndraw_aggregate_observations_lost="<<draw_aggregate_failures<<"\ndraw_aggregate_observations_coalesced="<<draw_aggregate_coalesced
            <<"\nsummary_updates_observed="<<summary_seen<<"\nsummary_updates_retained="<<summary_retained<<"\nsummary_updates_dropped="<<summary_dropped
            <<"\nsummary_fields="<<summary_fields<<"\nsummary_payload_bytes="<<summary_bytes<<"\npass_aggregate_limit="<<max_passes<<"\npass_light_aggregate_limit="<<max_groups
            <<"\nvisibility_aggregate_limit="<<max_visibility<<"\naggregate_overflow_policy=COALESCE_INTO_OTHER; exact global source counts retained\naggregate_overflow_buckets=ONE_ADDITIONAL_PER_MAP\n";
        out<<"\n[END]\nstatus="<<final_status<<"\ndraw_events="<<draws<<"\ntriangles_submitted="<<triangles<<"\ncpu_frame_ms="<<ms<<"\nevents="<<events<<"\nbuffer_bytes="<<body.size()<<"\noverflow="<<(overflowed?1:0)<<"\n";
        const std::string report=out.str();time_t now=time(NULL);tm local={};
#if defined(_WIN32)
        localtime_s(&local,&now);
#else
        localtime_r(&now,&local);
#endif
        char stamp[64];strftime(stamp,sizeof(stamp),"snap%d.%m.%Y.%H.%M.%S",&local);
        std::string directory=Clean(info.directory,sizeof(info.directory));if(!directory.empty() && directory.back()!='/' && directory.back()!='\\')directory+='/';
        FILE* file=NULL;std::string path;
        for(unsigned suffix=0;suffix<10000 && !file;++suffix){char tail[32];if(suffix)snprintf(tail,sizeof(tail),"_%02u.log",suffix);else strcpy(tail,".log");path=directory+stamp+tail;file=Exclusive(path.c_str());if(!file && errno!=EEXIST)break;}
        if(!file){failure="ERROR: cannot create snapshot file";ClearStorage();return;}
        const size_t written=fwrite(report.data(),1,report.size(),file);const int closed=fclose(file);
        last_path=written==report.size() && closed==0?path:"ERROR: incomplete snapshot write";
        ClearStorage();
    }catch(...){failure="ERROR: snapshot allocation/write failure";ClearStorage();}
}
#endif
