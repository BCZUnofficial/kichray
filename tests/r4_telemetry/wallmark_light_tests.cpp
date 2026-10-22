#include "xrRender/WallmarkSnapshot.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using std::string;
typedef uint32_t u32;typedef int BOOL;
const int FALSE=0,TRUE=1,CULL_NONE=0,CULL_CCW=1,D3DPT_TRIANGLELIST=4,SE_SUN_NEAR=0,SE_SUN_FAR=1;
#define ICF
struct Fvector {float x=1,y=2,z=3;};struct Fcolor {float r=1,g=1,b=1;};
struct Matrix {float _11=1;float rest[15]={};};
struct IRender_Light {enum {POINT=1,SPOT=2,OMNIPART=3,DIRECT=4};};
struct light {
    uint64_t snapshot_serial=1;u32 snapshot_record=0,snapshot_controller=0,snapshot_face=0;
    bool snapshot_record_valid=false;light* snapshot_parent=NULL;
    struct {u32 type=IRender_Light::POINT,bActive=1,bStatic=0,bShadow=1;} flags;
    Fvector position,direction;Fcolor color;float range=8,cone=1;
    u32 frame_render=0;struct {u32 smap_ID=0;}vis;
    struct {struct{Matrix view,project,combine;u32 posX=2,posY=3,size=256;}S;struct {Matrix combine;}D;} X;
    unsigned exports=0;void _export(int& p){++exports;++p;}
};
#include "xrRender/R2LightSnapshot.h"
struct CLight_DB {int package=0;void add_light(light* L);};
struct {struct {bool noshadows=false,HW_smap=true;u32 smapsize=1024;}o;} RImplementation;
const u32 R2FLAG_R1LIGHTS=1;
struct {bool enabled=false;bool test(u32)const{return enabled;}}ps_r2_ls_flags;
struct Statistics {u32 RenderDUMP_WMT_Count=0;}statistics;
struct {u32 dwFrame=2;Statistics* Statistic=&statistics;}Device;
namespace FVF {struct LIT {float x;};}
struct Geometry {u32 vb_stride=sizeof(FVF::LIT);};typedef Geometry* ref_geom;typedef int ref_shader;
struct FakeCache {
    struct VertexState {unsigned unlocks=0,vertices=0;void Unlock(u32 n,u32){++unlocks;vertices=n;}}Vertex;
    unsigned draws=0,shaders=0,geometry=0,culls=0;u32 triangles=0;string semantic;
    void set_Shader(ref_shader){++shaders;}void set_Geometry(ref_geom){++geometry;}
    void set_CullMode(int){++culls;}
    void Render(int,u32,u32 t){++draws;triangles=t;RenderSnapshotProvenance p={};RenderSnapshot_GetProvenance(&p);semantic=p.semantic_type;
        RenderSnapshotDraw d={};d.struct_size=sizeof(d);d.submitted=1;d.topology=4;d.vertex_count=t*3;d.vertex_count_known=1;d.instance_count=1;RenderSnapshot_Draw(&d);}
}RCache;
// PRODUCTION_FUNCTIONS
static unsigned checks=0;
void Require(bool condition,const char* reason){++checks;if(!condition)throw std::runtime_error(reason);}
bool Has(const string& s,const char* text){return s.find(text)!=string::npos;}
string Read(){std::ifstream f(RenderSnapshot_LastPath());return string(std::istreambuf_iterator<char>(f),{});}
int main(int argc,char** argv){try{
    Require(argc==2,"output directory");
    Geometry geometry;FVF::LIT vertices[9]={};FVF::LIT* begin=vertices,*end=vertices+6;u32 offset=7;
    FlushStream(&geometry,4,offset,end,begin,FALSE);
    Require(RCache.draws==1 && RCache.triangles==2 && RCache.culls==0 && statistics.RenderDUMP_WMT_Count==2,"inactive diagnostic preserves static stream draw and triangle statistics");
    const unsigned olddraws=RCache.draws;end=begin;FlushStream(&geometry,4,offset,end,begin,TRUE);
    Require(RCache.draws==olddraws && RCache.Vertex.vertices==0,"empty production stream unlocks without draw");
    RenderSnapshotInfo info={};snprintf(info.directory,sizeof(info.directory),"%s",argv[1]);
    Require(RenderSnapshot_Request(),"manual request");
    kich_wallmark_snapshot::Event("STATIC_REQUEST",&geometry,"ORIGIN","BLOOD; KNOWN_CALLSITE");
    RenderSnapshot_Begin(info);RenderSnapshot_Pass("FORWARD_TRANSPARENCY");
    RenderSnapshot_Context("STATIC","original_mesh","original_material");RenderSnapshot_Object("STATIC","original_mesh","original_material","SOURCE",0);
    RenderSnapshotProvenance before={},after={};RenderSnapshot_GetProvenance(&before);
    end=vertices+9;FlushStream(&geometry,4,offset,end,begin,TRUE);RenderSnapshot_GetProvenance(&after);
    Require(RCache.semantic=="DECAL_SKELETAL" && RCache.culls==2 && RCache.triangles==3,"actual stream draw has skeletal semantic and existing cull behavior");
    Require(!memcmp(&before,&after,sizeof(before)),"stream restores full previous provenance");
    char id[96];kich_wallmark_snapshot::ID(id,sizeof(id),"static",&geometry,UINT64_C(9007199254740993));
    float center[3]={1,2,3};kich_wallmark_snapshot::Mark(id,"STATIC_PROJECTED",&geometry,&vertices,center,.5f,3,12,4,.25f,true,123,false,"TEST_TIMER");
    kich_wallmark_snapshot::Selection(id,true,"CAMERA_FRUSTUM_AND_SSA");
    kich_wallmark_snapshot::Mark("old_skeletal","SKELETAL_CPU_1W_2W",&geometry,&geometry,center,.5f,1,20,10,.3f,false,0,false,"GAME_TIME");
    light source;source.snapshot_serial=UINT64_C(9007199254740993);source.flags.bStatic=1;source.snapshot_record_valid=true;source.snapshot_record=42;source.snapshot_controller=7;
    CLight_DB database;database.add_light(&source);
    Require(source.exports==0,"actual R2 policy rejects static light when R1LIGHTS disabled");
    source.frame_render=0;ps_r2_ls_flags.enabled=true;RImplementation.o.noshadows=true;database.add_light(&source);
    Require(source.exports==1 && !source.flags.bShadow,"actual enabled policy exports and preserves existing no-shadows behavior");
    database.add_light(&source);Require(source.exports==1,"duplicate frame does not export twice");
    light runtime;runtime.snapshot_serial=500;database.add_light(&runtime);
    Require(runtime.exports==1,"runtime source is exported without invented controller provenance");
    for(unsigned face=0;face<6;++face){light child;child.snapshot_serial=600+face;child.snapshot_parent=&source;child.snapshot_face=face;child.flags.type=IRender_Light::OMNIPART;
        snapshot_smap_spot(&child,false);RenderSnapshotDraw d={};d.struct_size=sizeof(d);d.submitted=1;d.topology=4;d.vertex_count=3;d.vertex_count_known=1;d.instance_count=1;RenderSnapshot_Draw(&d);}
    snapshot_smap_direct(&source,SE_SUN_NEAR,false);snapshot_smap_direct(&source,SE_SUN_FAR,true);
    RenderSnapshot_End("COMPLETE");const string output=Read();
    Require(Has(output,"9007199254740993"),"exact 64-bit source/mark identity survives output");
    Require(Has(output,"historical_creation_request=NOT_OBSERVED") && Has(output,"creation_engine_frame=NOT_MEASURED"),"pre-diagnostic marks do not invent creation history");
    Require(Has(output,"action=ORIGIN") && Has(output,"BLOOD; KNOWN_CALLSITE"),"pending diagnostic captures creation origin before frame start");
    Require(Has(output,"MANY_TO_ONE; INDIVIDUAL_MARK_DRAW_ID_NOT_AVAILABLE_AFTER_BATCHING") && Has(output,"semantic_type=DECAL_SKELETAL"),"batch relation is explicit with actual submitted draw evidence");
    Require(Has(output,"reason_duplicate_visit=ALREADY_PROCESSED_THIS_FRAME") && Has(output,"R2FLAG_R1LIGHTS_applied=1"),"observed policy and duplicate rejection recorded");
    Require(Has(output,"record_controller_valid=0") && Has(output,"controller=NOT_AVAILABLE"),"unknown runtime controller is not a sentinel ID");
    Require(Has(output,"role=POINT_SHADOW_FACE") && Has(output,"point_face=5") && Has(output,"source_light=legacy_light_9007199254740993"),"six service faces reference original source light");
    Require(Has(output,"light_id=legacy_light_9007199254740993") && !Has(output,"light_id=legacy_light_605\n"),"shadow draw groups use original light identity rather than six independent lamps");
    Require(Has(output,"caster_view_projection=1,0,0,0") && Has(output,"viewport_atlas=2,3,256,256;z=0,1"),"production shadow hook captures actual matrix and atlas placement");
    Require(Has(output,"phase=SUN_NEAR") && Has(output,"phase=SUN_FAR") && Has(output,"visible_shadow_pixels=NOT_MEASURED"),"sun cascades distinct and pixels not inferred from submission");
    Require(RenderSnapshot_ProfileRequest(1),"bounded profile starts after detailed capture");RenderSnapshot_Begin(info);
    Require(g_render_snapshot_active && !RenderSnapshot_Detailed(),"profile uses active counters without detailed introspection");
    // Null detailed-only inputs deliberately verify that profile guards precede
    // bounds/matrix/light dereferences and formatting, not just dictionary writes.
    kich_wallmark_snapshot::Mark("profile","STATIC",NULL,NULL,NULL,0,0,0,0,0,false,0,false,"NONE");
    kich_r2_light_snapshot::Record(NULL,"profile","IGNORED","NO_DETAILED_ACCESS");
    kich_r2_light_snapshot::Shadow(NULL,"LOCAL",NULL);snapshot_smap_spot(NULL,false);snapshot_smap_direct(NULL,SE_SUN_NEAR,false);
    const unsigned before_profile_draws=RCache.draws;end=vertices+6;FlushStream(&geometry,4,offset,end,begin,FALSE);
    Require(RCache.draws==before_profile_draws+1 && RCache.triangles==2,"counter profile preserves actual production stream draw");
    RenderSnapshot_End("COMPLETE");const string profile=Read();
    Require(Has(profile,"mode=COUNTERS_ONLY") && !Has(profile,"[RECORD DECALS]") && !Has(profile,"[RECORD SHADOW]") && Has(profile,"submitted_draw_calls=1"),"profile omits detailed mark/light data while counting submitted draw");
    printf("G3b.1 wallmark/R2 production hooks: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
