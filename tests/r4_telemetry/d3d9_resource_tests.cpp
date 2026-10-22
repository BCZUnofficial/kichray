#include <map>
#include <string>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include "d3d9.h"
#include "RenderSnapshot.h"
#include "RenderSnapshotD3D9.h"
namespace{bool detailed=true;uint64_t capture=1;RenderSnapshotProvenance current={};std::map<std::string,std::string> records;unsigned checks=0;
void Check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
std::string Key(const char* category,const char* id,const char* key){return std::string(category)+"/"+id+"/"+key;}
void CleanRefs(IDirect3DDevice9& d){Check(!d.color.refs&&!d.depth.refs&&!d.tex.refs&&!d.cube.refs&&!d.volume.refs&&!d.decl.refs,"query COM references all released");}
}
bool RenderSnapshot_Detailed(){return detailed;}uint64_t RenderSnapshot_CaptureID(){return capture;}
void RenderSnapshot_GetProvenance(RenderSnapshotProvenance* p){*p=current;}
void RenderSnapshot_Record(const char* c,const char* i,const char* k,const char* v){records[Key(c,i,k)]=v;}
void RenderSnapshot_RecordU64(const char* c,const char* i,const char* k,uint64_t v){records[Key(c,i,k)]=std::to_string(v);}
void RenderSnapshot_U64(const char* c,const char* k,uint64_t v){RenderSnapshot_RecordU64(c,"",k,v);}
void RenderSnapshot_Text(const char* c,const char* k,const char* v){RenderSnapshot_Record(c,"",k,v);}
int main(){try{IDirect3DDevice9 d;
    detailed=false;RenderSnapshot_D3D9Resources(&d,56);Check(d.queries==0,"ordinary/profile frames query nothing");detailed=true;
    RenderSnapshot_D3D9Resources(0,56);Check(d.queries==0,"null device guarded");
    RenderSnapshot_D3D9Resources(&d,56);CleanRefs(d);Check(d.texture_queries==20&&d.sampler_queries==180,"all sixteen PS and four VS sampler slots observed");
    Check(records.at("R2_BINDING/binding_1/layout_normal")=="1"&&records.at("R2_BINDING/binding_1/layout_tangent")=="1"&&records.at("R2_BINDING/binding_1/layout_binormal")=="1","actual declaration basis present");
    Check(records.at("R2_BINDING/binding_1/vertex_stride")=="56","stride linked to draw");
    Check(records.at("R2_BINDING/binding_1/scissor_rect")=="10,20,100,200","actual clip rectangle");
    const std::string cube=records.at("R2_BINDING/binding_1/ps_texture1"),volume=records.at("R2_BINDING/binding_1/ps_texture2");
    Check(records.at("R2_RESOURCE/"+cube+"/width")=="64"&&records.at("R2_RESOURCE/"+volume+"/depth")=="8","cube and volume descriptors use actual resource type");
    for(unsigned i=0;i<140;++i)RenderSnapshot_D3D9Resources(&d,56);
    const unsigned capped=d.queries;RenderSnapshot_D3D9Resources(&d,56);Check(d.queries==capped,"world sampling is bounded before device queries");
    std::strcpy(current.ui_widget,"root/pda");RenderSnapshot_D3D9Resources(&d,32);Check(d.queries>capped&&records.at("R2_UI_BINDING/binding_143/ui_widget")=="root/pda","late UI has an independent reservation");CleanRefs(d);
    current={};std::strcpy(current.semantic_type,"STATIC_WALLMARK");RenderSnapshot_D3D9Resources(&d,32);Check(records.count("R2_DECAL_BINDING/binding_144/vertex_stride"),"late decals have independent reservation");
    current={};std::strcpy(current.semantic_type,"SKELETAL_MODEL");RenderSnapshot_D3D9Resources(&d,32);Check(records.count("R2_MODEL_BINDING/binding_145/vertex_stride"),"models have independent reservation");
    ++capture;current={};d.fail=true;RenderSnapshot_D3D9Resources(&d,32);CleanRefs(d);Check(records.at("R2_BINDING/binding_1/ps_texture0")=="QUERY_FAILED_OR_STAGE_UNSUPPORTED","query failure stays unknown");
    d.fail=false;d.decl.oversize=true;RenderSnapshot_D3D9Resources(&d,32);CleanRefs(d);Check(records.at("R2_BINDING/binding_2/vertex_layout")=="QUERY_FAILED","invalid returned declaration length bounded");
    d.empty=true;const unsigned samplers=d.sampler_queries;RenderSnapshot_D3D9Resources(&d,32);Check(records.at("R2_BINDING/binding_3/ps_texture0")=="UNBOUND"&&d.sampler_queries==samplers,"unbound differs from failure");CleanRefs(d);
    std::cout<<"PASS: "<<checks<<" production D3D9 telemetry checks; no driver/runtime claim\n";
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
