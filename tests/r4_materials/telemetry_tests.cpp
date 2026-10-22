#include "fake_materials.h"
#include <cassert>
#include <fstream>
#include <sstream>
#include <limits>
uint64_t material_metadata_errors=0;uint32_t effect=0;
int main(int argc,char** argv){assert(argc==2);ShaderStage vs("source_vs"),ps("source_ps");CTexture base("base"),bump("declared_arbitrary_name");TextureList list;list.push_back(std::make_pair(0,Ref<CTexture>(&base)));list.push_back(std::make_pair(3,Ref<CTexture>(&bump)));
    SPass pass;pass.vs=&vs;pass.ps=&ps;pass.T=&list;ShaderElement element;element.passes.push_back(&pass);
    // Real compiler/source hooks run before a capture, preserving source declarations.
    RenderSnapshot_RegisterMaterialElement(&element,0,"material", "base", "CPP_BLENDER", "declared_arbitrary_name", "detail_declared",3,UINT64_C(9007199254740993));
    RenderSnapshot_CompilerBegin(&pass);RenderSnapshot_CompilerSampler(&pass,"s_base","base",0,1,3,2,3);
    RenderSnapshot_CompilerSampler(&pass,"s_bump","declared_arbitrary_name",3,1,2,2,2);
    RenderSnapshot_CompilerSampler(&pass,"s_bumpX","auxiliary",UINT32_MAX,1,2,2,2);RenderSnapshot_CompilerEnd(&pass,&pass);
    RenderSnapshot_ShaderIncludesBegin();const char source[]="S.normal = Nu.wzyx + (NuE.xyz - 1.0h);\nS.gloss = Nu.x*Nu.x;\n";
    RenderSnapshot_ShaderInclude("sload.h",source,sizeof(source)-1);const char* names[]={"USE_BUMP","SKIN_2"};const char* values[]={"1","1"};unsigned code=0x12345678;
    RenderSnapshot_RegisterShaderVariant("source_ps","ps_3_0",source,sizeof(source)-1,&code,sizeof(code),names,values,2);
    RenderSnapshotInfo info={};strcpy(info.renderer,"R4");strcpy(info.api,"D3D11");strncpy(info.directory,argv[1],sizeof(info.directory)-1);assert(RenderSnapshot_Request());RenderSnapshot_Begin(info);
    RenderSnapshot_MaterialSelection(&element,0,0,1,"NORMAL_E0_AVAILABLE");RenderSnapshotProvenance original={};RenderSnapshot_GetProvenance(&original);assert(original.material_route_id[0]);const std::string first=original.material_route_id;
    // Same shader names with different compiled bytes must not merge source routes.
    ++code;RenderSnapshot_RegisterShaderVariant("source_ps","ps_3_0",source,sizeof(source)-1,&code,sizeof(code),names,values,2);RenderSnapshot_MaterialSelection(&element,0,0,1,"NORMAL_E0_AVAILABLE");RenderSnapshotProvenance changed={};RenderSnapshot_GetProvenance(&changed);assert(first!=changed.material_route_id);
    // A shared element reports aliases, not an invented exact source name.
    RenderSnapshot_RegisterMaterialElement(&element,1,"second_material","base","LUA_SHADER_DESCRIPTION","","",0);RenderSnapshot_MaterialSelection(&element,0,1,2,"NORMAL_E0_UNAVAILABLE_OR_DISTORT_FALLBACK_E1");
    RenderSnapshot_SetProvenance(&original);RenderSnapshotDraw draw={};draw.struct_size=sizeof(draw);draw.submitted=1;draw.topology=3;draw.index_count=draw.vertex_count=draw.vertex_capacity=3;draw.vertex_count_known=draw.instance_count=1;RenderSnapshot_Draw(&draw);
    kich_r4_capture::Batch batch;batch.Begin(640,480,1);batch.vertices.resize(200);std::vector<float> source_vertices(200*9,0);std::map<uint32_t,uint32_t> reused;for(uint32_t i=0;i<200;++i)reused[i]=i;
    D3DVERTEXELEMENT9 decl[4]={{0,0,2,0,3,0},{0,12,2,0,6,0},{0,24,2,0,7,0},{255,0,0,0,0,0}};
    const kich_r4_scene::Matrix identity=kich_r4_scene::Identity();kich_r4_capture::Command command(0,300,0,"base",kich_r4_capture::Style());command.semantic=kich_r4_capture::CAPTURE_PARTICLE;
    SnapshotGeneratedGeometry(decl,4,36,3,reinterpret_cast<const unsigned char*>(source_vertices.data()),reused,&decl[0],&decl[1],&decl[2],NULL,batch,0,identity,identity,false,command);
    RenderSnapshot_End("COMPLETE");std::ifstream file(RenderSnapshot_LastPath());std::ostringstream stream;stream<<file.rdbuf();const std::string report=stream.str();
    for(const char* category:{"MATERIAL","NORMAL_MAPPING","GEOMETRY","SHADER"}){const size_t start=report.find(std::string("[DICTIONARY ")+category+"]");assert(start!=std::string::npos);const size_t end=report.find("\n[",start+1);const std::string summary=report.substr(start,end-start);assert(summary.find("updates_dropped=0")!=std::string::npos && summary.find("updates_truncated=0")!=std::string::npos);}
    assert(report.find("source_0_class_id=9007199254740993")!=std::string::npos);assert(report.find("source_0_bump_assigned=declared_arbitrary_name")!=std::string::npos);assert(report.find("semantic=s_bump;resource=declared_arbitrary_name;slot=3")!=std::string::npos);
    assert(report.find("slot=NOT_AVAILABLE")!=std::string::npos);assert(report.find("SHARED_ELEMENT_ALIASES; EXACT_REQUEST_UNKNOWN")!=std::string::npos);
    assert(report.find("UNSUPPORTED_BY_CURRENT_R4_DIFFUSE_PIPELINE")!=std::string::npos);assert(report.find("S.normal = Nu.wzyx + (NuE.xyz - 1.0h)")!=std::string::npos);assert(report.find("SKIN_2=1")!=std::string::npos);
    assert(report.find("material_route="+first)!=std::string::npos || report.find("material_route_id="+first)!=std::string::npos);
    assert(report.find("source_check_scope=FIRST_128_UNIQUE_SOURCE_INDICES_ASCENDING")!=std::string::npos);
    assert(report.find("zero_source_normals=128")!=std::string::npos && report.find("degenerate_source_bases=128")!=std::string::npos);
    assert(report.find("uv_sample_scope=FIRST_128_GENERATED_VERTICES_NOT_FULL_RANGE")!=std::string::npos);
    RenderSnapshot_ForgetMaterialElement(&element);RenderSnapshot_ForgetMaterialPass(&pass);assert(!MaterialRegistry().sources.count(&element) && !MaterialRegistry().passes.count(&pass));
    snapshot_geometry::Checks checks;const float n[]={0,0,1},t[]={1,0,0},b[]={0,1,0},zero[]={0,0,0},bad[]={std::numeric_limits<float>::infinity(),0,0};checks.Normal(n);checks.Normal(zero);checks.Normal(bad);checks.Basis(n,t,b);checks.Basis(n,t,t);assert(checks.normals==3 && checks.zero==1 && checks.nonfinite==1 && checks.bases==2 && checks.degenerate==1);
    const float scale[]={-2,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};assert(snapshot_geometry::Determinant(scale)<0 && snapshot_geometry::Nonuniform(scale));
    // Source map is bounded independently of draw/record storage.
    snapshot_material::Registry cap;snapshot_material::Source s;s.element=0;s.usage=0;for(uintptr_t i=1;i<2200;++i)cap.AddSource(reinterpret_cast<const void*>(i),s);assert(cap.sources.size()==2048 && cap.dropped_sources==151);
    puts("PASS: production material/source hooks preserve pre-capture evidence, shader variant route identity, aliases, exact sampler selection, and bounded geometry checks");
}
