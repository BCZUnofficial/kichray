#include "KichR4NativePrelude.h"
#include "xrRender/WallmarkSnapshot.h"
#include "KichR4Native.h"
#include "KichR4NativeCPU.h"
#include "KichR4Capture.h"
#include "KichR4Lighting.h"
#include "KichR4Visibility.h"
#include "KichR4LevelVisibility.h"
#include "KichR4MaterialRules.h"
#include "KichR4OpticalRules.h"
#include "KichR4MaterialServices.h"
#include "KichR4LodPolicy.h"
#include "KichR4RosBudget.h"
#include "KichR4RosExposure.h"
#include "KichR4PostprocessNoise.h"
#include "RenderSnapshot.h"
#include "KichR4TelemetryText.h"
#include "RenderSnapshotMaterial.h"
#include "IGame_Persistent.h"
#include "Environment.h"
#include "CustomHUD.h"
#include "IRenderable.h"
#include "xr_object.h"
#include "ISpatial.h"
#include "SkeletonCustom.h"
#include "FHierrarhyVisual.h"
#include "xrRender/DetailModel.h"
#include "xrRender/xrRender_console.h"
#include "xrRender/FSkinned.h"
#include "xrLevel.h"
#include "_d3d_extensions.h"
#include <algorithm>
#include <set>

extern float ps_r2_lt_smooth;

namespace {
class R4Sector:public IRender_Sector {};
kich_r4_level::Topology native_topology;
CDB::COLLIDER native_sector_collider;
kich_r4_ros::Budget native_ros_budget;
struct NativeLodView {kich_r4_scene::Matrix vp;uint32_t resolution;};
std::vector<NativeLodView> native_lod_shadow_views;
float NativeGeometryLOD(IRender_Visual* visual,const Fmatrix& world,bool hud){
    if(hud || !visual || !(visual->vis.sphere.R>0))return 1.f;
    Fvector center;world.transform_tiny(center,visual->vis.sphere.P);
    // Frobenius bound also covers scaled/sheared transforms conservatively.
    const float scale=_sqrt(world.i.square_magnitude()+world.j.square_magnitude()+world.k.square_magnitude());
    const kich_r4_lod::Parameters parameters=kich_r4_lod::CameraParameters(Device.dwWidth,Device.dwHeight,Device.fFOV,ps_r__LOD,ps_r__GLOD_ssa_start,ps_r__GLOD_ssa_end);
    float detail=kich_r4_lod::Geometry(visual->vis.sphere.R*scale,center.distance_to_sqr(Device.vCameraPosition),parameters);
    if(detail>=1.f)return 1.f;
    float low[3]={},high[3]={};
    for(unsigned corner=0;corner<8;++corner){Fvector p;p.set(corner&1?visual->vis.box.max.x:visual->vis.box.min.x,corner&2?visual->vis.box.max.y:visual->vis.box.min.y,corner&4?visual->vis.box.max.z:visual->vis.box.min.z);world.transform_tiny(p);
        const float values[]={p.x,p.y,p.z};for(unsigned axis=0;axis<3;++axis){if(!_valid(values[axis]))return 1.f;
            if(!corner || values[axis]<low[axis])low[axis]=values[axis];if(!corner || values[axis]>high[axis])high[axis]=values[axis];}}
    // A command is captured once and reused for all passes. Use the most
    // detailed request, including an off-camera local shadow near its source.
    for(size_t i=0;i<native_lod_shadow_views.size() && detail<1.f;++i){const NativeLodView& view=native_lod_shadow_views[i];
        if(kich_r4_visibility::Visible(low,high,view.vp))detail=(std::max)(detail,kich_r4_lod::Projected(low,high,view.vp,view.resolution,view.resolution,ps_r__GLOD_ssa_start,ps_r__GLOD_ssa_end));}
    return detail;
}
const KichR4LightingFrame& NativeRosLights(){
    static KichR4LightingFrame lights;static u32 frame=u32(-1);static IGame_Level* level=NULL;static bool r1=false;
    const bool requested=ps_r2_ls_flags.test(R2FLAG_R1LIGHTS)!=0;
    if(frame!=Device.dwFrame || level!=g_pGameLevel || r1!=requested){
        frame=Device.dwFrame;level=g_pGameLevel;r1=requested;
        // ROS may be requested by AI outside the camera. Capture all permitted
        // sources once; object range/cone tests below retain their semantics.
        KichR4_CaptureLighting(&Device.vCameraPosition.x,lights,requested);}
    return lights;
}
class R4ROS:public IRender_ObjectSpecific {
    IRenderable* parent;kich_r4_ros::Entry scheduling;kich_r4_ros::VisualExposure visual;
    float total,sky;u32 ai_updated=0;bool ai_initialized=false;
    void Center(Fvector& point)const {
        point.set(0,0,0);
        if(parent){point=parent->renderable.xform.c;
            if(parent->renderable.visual)parent->renderable.xform.transform_tiny(point,parent->renderable.visual->vis.sphere.P);}
    }
    void VisualPoint(Fvector& point,Fvector& center,float& radius)const {
        Center(center);point=center;radius=0;
        if(!parent || !parent->renderable.visual)return;
        const Fmatrix& world=parent->renderable.xform;const vis_data& bounds=parent->renderable.visual->vis;
        const float scale=_sqrt((std::max)(world.i.square_magnitude(),(std::max)(world.j.square_magnitude(),world.k.square_magnitude())));
        radius=bounds.sphere.R*scale;
        Fvector half,box_center;half.sub(bounds.box.max,bounds.box.min);half.mul(.5f);
        box_center.add(bounds.box.min,bounds.box.max);box_center.mul(.5f);world.transform_tiny(box_center);
        const float height=_abs(world.i.y)*half.x+_abs(world.j.y)*half.y+_abs(world.k.y)*half.z;
        point.y=kich_r4_ros::RaisedSampleY(point.y,radius,box_center.y-height,box_center.y+height);
    }
    void UpdateVisual(){
        if(!g_pGamePersistent)return;
        const bool actor=g_pGameLevel && static_cast<IRenderable*>(g_pGameLevel->CurrentViewEntity())==parent;
        if(!native_ros_budget.Request(scheduling,Device.dwFrame,Device.dwTimeGlobal,actor))return;
        scheduling.updated=Device.dwTimeGlobal;scheduling.initialized=true;
        Fvector point,center;float radius;VisualPoint(point,center,radius);
        visual.Begin(&center.x,radius,Device.dwTimeGlobal);
        const bool trace=g_pGameLevel && g_pGameLevel->bReady;
        for(u32 i=0;i<5;++i){Fvector direction;const float* sample=kich_r4_ros::HemisphereDirection(visual.NextDirection());
            direction.set(sample[0],sample[1],sample[2]);direction.normalize();
            visual.Sample(!trace || !g_pGameLevel->ObjectSpace.RayTest(point,direction,32,collide::rqtStatic,NULL,NULL));}
        Fvector sun;sun.invert(g_pGamePersistent->Environment().CurrentEnv.sun_dir);
        visual.Commit(!trace || !g_pGameLevel->ObjectSpace.RayTest(point,sun,256,collide::rqtStatic,NULL,NULL));
    }
    // AI keeps the established unsmoothed 200 ms observation at its original
    // point/directions. Rendering neither changes that observation nor performs
    // its per-local-light collision queries just to obtain visual hemi/sun.
    void UpdateAI(){if(!g_pGamePersistent || (ai_initialized && u32(Device.dwTimeGlobal-ai_updated)<200))return;
        ai_updated=Device.dwTimeGlobal;ai_initialized=true;
        const CEnvDescriptor& e=g_pGamePersistent->Environment().CurrentEnv;Fvector p;Center(p);
        float exposure=1;const bool trace=g_pGameLevel && g_pGameLevel->bReady;
        if(trace){const Fvector directions[5]={{0,1,0},{.70710678f,.70710678f,0},{-.70710678f,.70710678f,0},{0,.70710678f,.70710678f},{0,.70710678f,-.70710678f}};
            exposure=0;for(u32 i=0;i<5;++i)if(!g_pGameLevel->ObjectSpace.RayTest(p,directions[i],32,collide::rqtStatic,NULL,NULL))exposure+=.2f;}
        sky=clampr((e.ambient.x+e.ambient.y+e.ambient.z)/3.f+(e.hemi_color.x+e.hemi_color.y+e.hemi_color.z)*exposure/3.f,0.f,1.f);total=sky;
        Fvector sun;sun.invert(e.sun_dir);
        if(!trace || !g_pGameLevel->ObjectSpace.RayTest(p,sun,256,collide::rqtStatic,NULL,NULL))total+=(e.sun_color.x+e.sun_color.y+e.sun_color.z)/3.f;
        const KichR4LightingFrame& lights=NativeRosLights();
        for(u32 i=0;i<lights.count;++i){const KichR4LocalLight& l=lights.lights[i];Fvector d;d.set(l.position[0]-p.x,l.position[1]-p.y,l.position[2]-p.z);const float distance=d.magnitude();if(distance>=l.range || distance<EPS_L)continue;d.div(distance);
            if(l.type==IRender_Light::SPOT && -(d.x*l.direction[0]+d.y*l.direction[1]+d.z*l.direction[2])<cosf(l.cone*.5f))continue;
            if(trace && g_pGameLevel->ObjectSpace.RayTest(p,d,distance,collide::rqtStatic,NULL,NULL))continue;const float fade=1-distance/l.range;total+=(l.color[0]+l.color[1]+l.color[2])*fade*fade/3.f;}
        total=clampr(total,0.f,1.f);
    }
public:
    explicit R4ROS(IRenderable* p):parent(p),total(.5f),sky(.5f){native_ros_budget.Add(scheduling);}
    ~R4ROS(){native_ros_budget.Remove(scheduling);}
    void force_mode(u32){scheduling.initialized=false;ai_initialized=false;visual.Reset();}
    float get_luminocity(){UpdateAI();return total;}
    float get_luminocity_hemi(){UpdateAI();return sky;}
    void Exposure(float& hemi,float& sun){UpdateVisual();visual.Smooth(Device.dwFrame,Device.fTimeDelta,ps_r2_lt_smooth);hemi=visual.hemi;sun=visual.sun;}
};

xr_vector<ref_light> static_lights;
u32 native_level_light_records=0,native_level_directional_skips=0;
class R4Light:public IRender_Light {
    KichR4LocalLight value;KichR4LightIdentity identity;KichR4LightProjector projector;bool active,shadow;
    void Update(){const bool registered=KichR4_CaptureLightIdentity(this,identity);if(registered)KichR4_CaptureLightProjector(this,projector);KichR4_CaptureLight(this,value,active && registered);KichR4_CaptureLightShadow(this,shadow);}
public:
    R4Light():active(false),shadow(false){static uint64_t next_serial=0;ZeroMemory(&identity,sizeof(identity));
        identity.process_handle=static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));identity.serial=++next_serial;identity.role=KICH_R4_LIGHT_ROLE_RUNTIME;
        identity.level_record=identity.controller=UINT32_MAX;ZeroMemory(&value,sizeof(value));value.type=POINT;value.range=8;value.cone=PI_DIV_2;value.direction[2]=1;}
    ~R4Light(){KichR4_CaptureLightForget(this);}
    void SetLevelIdentity(uint32_t record,uint32_t controller){identity.role=KICH_R4_LIGHT_ROLE_LEVEL;identity.flags|=KICH_R4_LIGHT_STATIC;identity.level_record=record;identity.controller=controller;Update();}
    void set_type(LT v){value.type=v;Update();}void set_active(bool v){active=v;Update();}bool get_active(){return active;}
    void set_shadow(bool v){shadow=v;Update();}void set_position(const Fvector& p){CopyMemory(value.position,&p.x,12);Update();}
    void set_rotation(const Fvector& d,const Fvector& r){CopyMemory(value.direction,&d.x,12);CopyMemory(projector.right,&r.x,12);Update();}
    void set_cone(float v){value.cone=v;Update();}void set_range(float v){value.range=v;Update();}
    void set_virtual_size(float){}void set_texture(LPCSTR name){try{projector.texture=name?name:"";}catch(...){projector.texture.clear();}Update();}
    void set_color(const Fcolor& c){set_color(c.r,c.g,c.b);}void set_color(float r,float g,float b){value.color[0]=r;value.color[1]=g;value.color[2]=b;Update();}
};
class R4Glow;
xr_vector<R4Glow*> glows;
struct GlowFrameStats {u32 active,frustum_rejected,facing_rejected,queued,recorded;};
GlowFrameStats glow_stats={};
class R4Glow:public IRender_Glow {
public:
    bool active,level_source;Fvector position,direction;float radius;Fcolor color;ref_shader shader;
    kich_r4_optics::GlowVisibility visibility;
    R4Glow():active(false),level_source(false),radius(.2f){position.set(0,0,0);direction.set(0,0,0);color.set(1,1,1,1);glows.push_back(this);}
    ~R4Glow(){glows.erase(std::remove(glows.begin(),glows.end(),this),glows.end());}
    void set_active(bool v){active=v;if(!v)visibility.Reset();}bool get_active(){return active;}
    void set_position(const Fvector& v){position=v;}void set_direction(const Fvector& v){direction=v;}
    void set_radius(float v){radius=v;}void set_texture(LPCSTR v){shader.create("effects\\flare",v);}
    void set_color(const Fcolor& c){color=c;}void set_color(float r,float g,float b){color.set(r,g,b,1);}
    void Draw(){if(!active || !shader || !(radius>0)){visibility.Reset();return;}
        ++glow_stats.active;
        if(!RImplementation.ViewBase.testSphere_dirty(position,radius)){++glow_stats.frustum_rejected;visibility.Reset();return;}
        Fvector observer;observer.sub(Device.vCameraPosition,position);
        const float facing=kich_r4_optics::GlowFacing(&direction.x,&observer.x);
        if(!(facing>0)){++glow_stats.facing_rejected;visibility.Reset();return;}
        // A single static collision ray incorrectly hides a whole lamp when
        // only its centre is behind the fixture, and misses moving occluders.
        // Use the completed scene depth for each pixel of this unlit billboard.
        const float intensity=facing*visibility.Step(true,Device.dwTimeGlobal,&Device.vCameraPosition.x);
        if(intensity<=0)return;
        ++glow_stats.queued;
        char id[48]={};const bool record=g_render_snapshot_active && glow_stats.recorded<32;
        if(g_render_snapshot_active)sprintf_s(id,sizeof(id),"glow_%p",static_cast<void*>(this));
        if(record){++glow_stats.recorded;
            kich_r4_telemetry::Vector("GLOW",id,"position",&position.x,3);
            kich_r4_telemetry::Vector("GLOW",id,"direction",&direction.x,3);
            kich_r4_telemetry::Float("GLOW",id,"radius",radius);
            kich_r4_telemetry::Float("GLOW",id,"facing_scale",facing);
            kich_r4_telemetry::Float("GLOW",id,"visibility_alpha",visibility.value);
            kich_r4_telemetry::Text("GLOW",id,"source_kind",level_source?"LEVEL_OMNIDIRECTIONAL":direction.square_magnitude()>0?"RUNTIME_DIRECTED":"RUNTIME_OMNIDIRECTIONAL");
            kich_r4_telemetry::Text("GLOW",id,"occlusion_policy","OPAQUE_SCENE_DEPTH_PER_PIXEL; STATIC_AND_DYNAMIC_OCCLUDERS; NO_CENTER_RAY_OR_DEPTH_OFFSET");
            kich_r4_telemetry::Text("GLOW",id,"material_policy","EXPLICIT_UNLIT; ORIGINAL_TEXTURE_AND_BLEND; DEPTH_LESS_EQUAL; NO_DEPTH_WRITE");
            kich_r4_telemetry::Text("GLOW",id,"visibility_result","QUEUED_FOR_CAPTURE; NOT_GPU_VISIBILITY");}
        Fvector right,up;right.mul(Device.vCameraRight,radius);up.mul(Device.vCameraTop,radius);
        u32 offset;FVF::LIT* v=(FVF::LIT*)RCache.Vertex.Lock(4,sizeof(FVF::LIT),offset);Fvector p;
        const float coords[4][2]={{-1,-1},{-1,1},{1,-1},{1,1}};
        const u32 c=color_rgba(iFloor(clampr(color.r*intensity,0.f,1.f)*255),iFloor(clampr(color.g*intensity,0.f,1.f)*255),iFloor(clampr(color.b*intensity,0.f,1.f)*255),255);
        for(u32 i=0;i<4;++i){p=position;p.mad(right,coords[i][0]);p.mad(up,coords[i][1]);v[i].set(p,c,(coords[i][0]+1)*.5f,(1-coords[i][1])*.5f);}
        RCache.Vertex.Unlock(4,sizeof(FVF::LIT));ref_geom geometry;geometry.create(FVF::F_LIT,RCache.Vertex.Buffer(),RCache.QuadIB);
        RenderSnapshotProvenance previous={};if(g_render_snapshot_active){RenderSnapshot_GetProvenance(&previous);
            RenderSnapshot_Object("GLOW",id,NULL,"R4Glow::Draw; PROCEDURAL_GLOW_QUAD",RENDER_SNAPSHOT_GLOW);}
        KichR4NativeGlowScope native_glow;
        RCache.set_xform_world(Fidentity);RCache.set_Shader(shader);RCache.set_Geometry(geometry);RCache.Render(D3DPT_TRIANGLELIST,offset,0,4,0,2);
        if(g_render_snapshot_active)RenderSnapshot_SetProvenance(&previous);
    }
};
xr_vector<ref_glow> static_glows;
}
float r_ssaDISCARD=1.e-6f;
CRender RImplementation;
namespace {bool NativeLightVisible(const KichR4LocalLight& light,const float* camera_vp){
    return RImplementation.HOM.LightVisible(light.position,light.range,camera_vp)!=FALSE;
}}
IRender_interface* KichR4_CreateNativeRenderer(){xrRender_initconsole();return &RImplementation;}
CRender::CRender():object(NULL),invisible(false),loaded(false),phase(PHASE_NORMAL),val_bHUD(FALSE),Models(NULL),Details(NULL),Wallmarks(NULL){transform.identity();View=&ViewBase;m_skinning=-1;}
CRender::~CRender(){}
void CRender::create(){KichR4_SetLightVisibility(NativeLightVisible);Device.seqFrame.Add(this,REG_PRIORITY_HIGH);Models=xr_new<CModelPool>();Wallmarks=xr_new<CWallmarksEngine>();PSLibrary.OnCreate();Msg("[R4 native B5] renderer queues/models/particles/wallmarks: DX11; no R1/R2 module, no DX9 device");}
void CRender::destroy(){KichR4_SetLightVisibility(NULL);Device.seqFrame.Remove(this);level_Unload();xr_delete(Wallmarks);xr_delete(Models);PSLibrary.OnDestroy();}
void CRender::reset_begin(){queue.clear();shadow_views.clear();native_lod_shadow_views.clear();HOM.Invalidate();KichR4_ResetFrozenLighting();}
void CRender::reset_end(){}
void CRender::LoadBuffers(){IReader* f=FS.r_open("$level$","level.geom");R_ASSERT2(f,"R4 level.geom");IReader* v=f->open_chunk(fsL_VB);R_ASSERT(v);
    const u32 n=v->r_u32();declarations.resize(n);for(u32 i=0;i<n;++i){D3DVERTEXELEMENT9 e;do{v->r(&e,sizeof(e));declarations[i].push_back(e);}while(e.Stream!=255);
        const u32 stride=D3DXGetDeclVertexSize(declarations[i].begin(),0),count=v->r_u32();IDirect3DVertexBuffer9* buffer=NULL;R_CHK(KichR4_CPUVertexBuffer(stride*count,&buffer));void* data=NULL;R_CHK(buffer->Lock(0,0,&data,0));v->r(data,stride*count);buffer->Unlock();vertices.push_back(buffer);}
    v->close();v=f->open_chunk(fsL_IB);R_ASSERT(v);const u32 ni=v->r_u32();for(u32 i=0;i<ni;++i){const u32 count=v->r_u32();IDirect3DIndexBuffer9* buffer=NULL;R_CHK(KichR4_CPUIndexBuffer(count*2,&buffer));void* data=NULL;R_CHK(buffer->Lock(0,0,&data,0));v->r(data,count*2);buffer->Unlock();indices.push_back(buffer);}v->close();
    v=f->open_chunk(fsL_SWIS);if(v){const u32 nw=v->r_u32();windows.resize(nw);for(u32 i=0;i<nw;++i){FSlideWindowItem& w=windows[i];v->r(w.reserved,sizeof(w.reserved));w.count=v->r_u32();w.sw=xr_alloc<FSlideWindow>(w.count);v->r(w.sw,w.count*sizeof(FSlideWindow));}v->close();}FS.r_close(f);
}
void CRender::level_Load(IReader* f){R_ASSERT(!loaded);
    const unsigned char* level_start=static_cast<const unsigned char*>(f->pointer())-f->tell();
    kich_r4_level::LoadTopology(kich_r4_level::Span(level_start,f->length()),native_topology);
    IReader* c=f->open_chunk(fsL_SHADERS);R_ASSERT(c);const u32 ns=c->r_u32();shaders.resize(ns);
    for(u32 i=0;i<ns;++i){string_path entry;c->r_stringZ(entry,sizeof(entry));char* sep=strchr(entry,'/');if(sep){*sep++=0;shaders[i].create(entry,sep);}}c->close();
    c=f->open_chunk(fsL_VISUALS);R_ASSERT(c);for(u32 i=0;;++i){IReader* v=c->open_chunk(i);if(!v)break;ogf_header header;v->r_chunk_safe(OGF_HEADER,&header,sizeof(header));IRender_Visual* visual=Models->Instance_Create(header.type);visual->Load(NULL,v,0);visuals.push_back(visual);v->close();}c->close();
    c=f->open_chunk(fsL_SECTORS);if(c){for(u32 i=0;;++i){IReader* s=c->open_chunk(i);if(!s)break;sectors.push_back(xr_new<R4Sector>());s->close();}c->close();}if(sectors.empty())sectors.push_back(xr_new<R4Sector>());
    native_level_light_records=native_level_directional_skips=0;
    c=f->open_chunk(fsL_LIGHT_DYNAMIC);if(c){u32 record=0;while(c->elapsed()>=sizeof(Flight)+4){const u32 ordinal=record++,controller=c->r_u32();Flight source;c->r(&source,sizeof(source));++native_level_light_records;if(source.type==D3DLIGHT_DIRECTIONAL){++native_level_directional_skips;continue;}
        R4Light* source_light=xr_new<R4Light>();source_light->SetLevelIdentity(ordinal,controller);ref_light l=source_light;
        l->set_shadow(true);l->set_type(source.type==D3DLIGHT_SPOT?IRender_Light::SPOT:IRender_Light::POINT);l->set_position(source.position);Fvector right;right.set(1,0,0);l->set_rotation(source.direction,right);l->set_range(source.range);l->set_color(source.diffuse);l->set_active(true);static_lights.push_back(l);}c->close();}
    c=f->open_chunk(fsL_GLOWS);if(c){while(c->elapsed()>=18){Fvector p;c->r(&p,sizeof(p));const float r=c->r_float();const u16 shader=c->r_u16();R4Glow* g=xr_new<R4Glow>();g->position=p;g->radius=r;if(shader<shaders.size())g->shader=shaders[shader];g->active=g->level_source=true;ref_glow owner=g;static_glows.push_back(owner);}c->close();}
    Details=xr_new<CDetailManager>();Details->m_frame_calc=Device.dwFrame-1;Details->m_frame_rendered=Device.dwFrame-1;Details->Load();HOM.Load();loaded=true;KichR4_NativeLevel(g_pGameLevel->name().c_str());
}
void CRender::level_Unload(){queue.clear();shadow_views.clear();native_lod_shadow_views.clear();HOM.Unload();KichR4_ResetFrozenLighting();KichR4_NativeLevel(NULL);static_glows.clear();static_lights.clear();if(Details){Details->Unload();xr_delete(Details);}if(Wallmarks)Wallmarks->clear();
    native_topology=kich_r4_level::Topology();
    for(u32 i=0;i<visuals.size();++i){visuals[i]->Release();xr_delete(visuals[i]);}visuals.clear();shaders.clear();for(u32 i=0;i<sectors.size();++i)xr_delete(sectors[i]);sectors.clear();
    for(u32 i=0;i<vertices.size();++i)_RELEASE(vertices[i]);vertices.clear();for(u32 i=0;i<indices.size();++i)_RELEASE(indices[i]);indices.clear();declarations.clear();for(u32 i=0;i<windows.size();++i)xr_free(windows[i].sw);windows.clear();loaded=false;
}
IDirect3DBaseTexture9* CRender::texture_load(LPCSTR name,u32& bytes){bytes=0;std::string base=name?name:"";
    if(base.size()>=4 && !stricmp(base.c_str()+base.size()-4,".dds"))base.resize(base.size()-4);
    string_path path;IReader* f=NULL;const std::string absolute=base+".dds";
    if(FS.exist(absolute.c_str()))f=FS.r_open(absolute.c_str());
    else if(FS.exist(path,"$level$",base.c_str(),".dds"))f=FS.r_open(path);
    else if(FS.exist(path,"$game_saves$",base.c_str(),".dds"))f=FS.r_open(path);
    else if(FS.exist(path,"$game_textures$",base.c_str(),".dds"))f=FS.r_open(path);
    u32 w=1,h=1;if(f){if(f->length()>=128){const unsigned char* p=(const unsigned char*)f->pointer();u32 magic,width,height;CopyMemory(&magic,p,4);CopyMemory(&height,p+12,4);CopyMemory(&width,p+16,4);
        if(magic==0x20534444u && width && height && width<=16384 && height<=16384){w=width;h=height;bytes=f->length();}}FS.r_close(f);}
    IDirect3DTexture9* result=NULL;R_CHK(KichR4_CPUTexture(base.c_str(),w,h,&result));return result;
}
HRESULT CRender::shader_compile(LPCSTR,LPCSTR,UINT,void*,void*,LPCSTR,LPCSTR,DWORD,void*,void*,void*){return E_NOTIMPL;}
ref_shader CRender::getShader(int id){R_ASSERT(id>=0 && id<(int)shaders.size());return shaders[id];}
IRender_Sector* CRender::getSector(int id){R_ASSERT(id>=0 && id<(int)sectors.size());return sectors[id];}
IRender_Visual* CRender::getVisual(int id){R_ASSERT(id>=0 && id<(int)visuals.size());return visuals[id];}
uint32_t KichR4_NativeSector(const float* position){
    if(!position || !native_topology.valid || !g_pGameLevel || !g_pGameLevel->ObjectSpace.GetStaticModel())return UINT32_MAX;
    Fvector origin;origin.set(position[0],position[1],position[2]);
    if(!_valid(origin.x) || !_valid(origin.y) || !_valid(origin.z))return UINT32_MAX;
    native_sector_collider.ray_options(CDB::OPT_ONLYNEAREST);
    // Follow R2's downward then upward probe, selecting the nearest CFORM or
    // portal crossing. Never guess a sector from overlapping world AABBs.
    for(u32 probe=0;probe<2;++probe){Fvector direction;direction.set(0,probe?1.f:-1.f,0);float portal_range=500.f;
        const kich_r4_level::Vec3 p={origin.x,origin.y,origin.z},d={direction.x,direction.y,direction.z};
        const uint32_t portal_sector=kich_r4_level::PortalRaySector(native_topology,p,d,portal_range);
        if(portal_range==0)return UINT32_MAX; // Camera/light straddles a portal.
        native_sector_collider.ray_query(g_pGameLevel->ObjectSpace.GetStaticModel(),origin,direction,portal_range);
        if(native_sector_collider.r_count()){
            const CDB::RESULT& hit=*native_sector_collider.r_begin();
            if(portal_sector!=UINT32_MAX && portal_range<=hit.range+EPS)return portal_sector;
            const uint32_t sector=g_pGameLevel->ObjectSpace.GetStaticTris()[hit.id].sector;
            return sector<native_topology.sectors.size()?sector:UINT32_MAX;
        }
        if(portal_sector!=UINT32_MAX)return portal_sector;
    }
    return UINT32_MAX;
}
IRender_Sector* CRender::detectSector(const Fvector& position){const uint32_t sector=KichR4_NativeSector(&position.x);return sector<sectors.size()?sectors[sector]:NULL;}
D3DVERTEXELEMENT9* CRender::getVB_Format(int id){if(vertices.empty())LoadBuffers();R_ASSERT(id>=0 && id<(int)declarations.size());return declarations[id].begin();}
IDirect3DVertexBuffer9* CRender::getVB(int id){if(vertices.empty())LoadBuffers();R_ASSERT(id>=0 && id<(int)vertices.size());return vertices[id];}
IDirect3DIndexBuffer9* CRender::getIB(int id){if(vertices.empty())LoadBuffers();R_ASSERT(id>=0 && id<(int)indices.size());return indices[id];}
FSlideWindowItem* CRender::getSWI(int id){if(vertices.empty())LoadBuffers();R_ASSERT(id>=0 && id<(int)windows.size());return &windows[id];}
void CRender::set_Transform(Fmatrix* v){transform=v?*v:Fidentity;RCache.set_xform_world(transform);}
void CRender::set_HUD(BOOL v){val_bHUD=v;}
static void NativePrepareVisualPose(IRender_Visual* visual){
    if(!visual)return;
    CKinematics* skeleton=visual->dcast_PKinematics();
    // Like R2 add_leafs_Dynamic, finish attachment callbacks before the caller
    // submits held items. Their world matrices are copied into the queue below.
    // Submit retains its guard and wallmark work; same-time bones are cached.
    if(skeleton)skeleton->CalculateBones(TRUE);
    if(skeleton || visual->Type==MT_HIERRARHY || visual->Type==MT_LOD){
        FHierrarhyVisual* hierarchy=static_cast<FHierrarhyVisual*>(visual);
        for(u32 i=0;i<hierarchy->children.size();++i)NativePrepareVisualPose(hierarchy->children[i]);
    }
}
void CRender::add_Visual(IRender_Visual* v){if(!v || invisible)return;NativePrepareVisualPose(v);Fvector center;transform.transform_tiny(center,v->vis.sphere.P);Queued q={v,transform,val_bHUD!=FALSE,center.distance_to_sqr(Device.vCameraPosition),false,1,1};
    const float radius=v->vis.sphere.R*_sqrt(transform.i.square_magnitude()+transform.j.square_magnitude()+transform.k.square_magnitude());
    // Off-camera shadow-only commands do not consume object irradiance. They
    // must not trigger ray work for an exposure their depth pass never uses.
    const bool camera_needed=!(radius>0) || !_valid(radius) || ViewBase.testSphere_dirty(center,radius);
    // HUD geometry has a separate projection, but its indirect exposure belongs
    // to the carrier. Keep the item identity for wetness/material callbacks;
    // only the visual ROS owner follows its root. Multiple HUD parts reuse the
    // same budgeted, smoothed exposure rather than tracing per bone or draw.
    IRenderable* light_object=object;
    if(q.hud){CObject* item=dynamic_cast<CObject*>(object);
        if(item)light_object=item->H_Root();
        if(!light_object && g_pGameLevel)light_object=g_pGameLevel->CurrentViewEntity();}
    if(light_object && (q.hud || camera_needed)){R4ROS* ros=dynamic_cast<R4ROS*>(light_object->renderable_ROS());if(ros){ros->Exposure(q.hemi,q.sun);q.light_valid=true;}}
    queue.push_back(q);}
void CRender::add_Geometry(IRender_Visual* v){if(v && occ_visible(v->vis))add_Visual(v);}
void CRender::Submit(IRender_Visual* v,CKinematics* parent){if(!v)return;CKinematics* k=v->dcast_PKinematics();if(k){k->CalculateBones(TRUE);k->CalculateWallmarks();parent=k;}
    const Fmatrix original=RCache.get_xform_world();
    if(v->Type==MT_PARTICLE_GROUP){PS::CParticleGroup* group=static_cast<PS::CParticleGroup*>(v);for(u32 i=0;i<group->items.size();++i){xr_vector<IRender_Visual*> children;group->items[i].GetVisuals(children);for(u32 j=0;j<children.size();++j){RCache.set_xform_world(original);Submit(children[j],parent);}}RCache.set_xform_world(original);return;}
    if(v->Type==MT_HIERRARHY || k || v->Type==MT_LOD){FHierrarhyVisual* h=static_cast<FHierrarhyVisual*>(v);for(u32 i=0;i<h->children.size();++i){RCache.set_xform_world(original);Submit(h->children[i],parent);}RCache.set_xform_world(original);return;}
    if(!v->shader)return;
    /* The spatial source is the camera/shadow union. Generated streams have
     * no shadow path; reject only when both possible bound conventions are
     * outside the camera. Particle bounds can already be in world space. */
    if(!val_bHUD && !KichR4_NativeInlineGeometry(v) && v->vis.sphere.R>0){
        Fvector center;original.transform_tiny(center,v->vis.sphere.P);
        const float scale=_sqrt(original.i.square_magnitude()+original.j.square_magnitude()+original.k.square_magnitude());
        if(!ViewBase.testSphere_dirty(center,v->vis.sphere.R*scale) &&
            !ViewBase.testSphere_dirty(v->vis.sphere.P,v->vis.sphere.R))return;
    }
    uint32_t available_elements=0;
    for(u32 i=0;i<6;++i)if(v->shader->E[i] && !v->shader->E[i]->passes.empty())available_elements|=1u<<i;
    kich_r4_material::Element elements[6];
    for(u32 i=0;i<6;++i){ShaderElement* e=v->shader->E[i]._get();if(e)elements[i]=kich_r4_material::Element(e->passes.size(),e->flags.bDistort!=0,e->flags.bEmissive!=0);}
    const kich_r4_material::Selection selected=kich_r4_material::Select(elements,KichR4_NativeDistortionEnabled());
    float emissive_gain=0,emissive_specular=0;
    if(selected.normal!=UINT32_MAX && elements[selected.normal].emissive && elements[4].passes==1 && !elements[4].distort){
        // R2 E[4] accum_emissive[l] has NO texture: it adds constant irradiance
        // to the base pass, not a second textured forward surface. Fold its
        // known gain once; an empty-texture Plain draw would overwrite white.
        const SPass* special=v->shader->E[4]->passes[0]._get();
        const float gain=special && special->ps?kich_r4_material::EmissiveGain(special->ps->cName.c_str()):0;
        if(gain>0){emissive_gain=gain;emissive_specular=kich_r4_material::EmissiveSpecular(special->ps->cName.c_str());}
        if(RenderSnapshot_Detailed())RenderSnapshot_MaterialSelection(v->shader->E[4]._get(),0,4,available_elements,
            gain>0?"KNOWN_ACCUM_EMISSIVE_IRRADIANCE_FOLDED_ONCE_IN_TEXTURED_NORMAL":"UNKNOWN_SPECIAL_EMISSIVE_NO_UNVERIFIED_OVERLAY");
    }
    if(RenderSnapshot_Detailed()){
        if(selected.normal==UINT32_MAX)RenderSnapshot_MaterialSelection(NULL,UINT32_MAX,UINT32_MAX,available_elements,"NORMAL_E0_E1_ABSENT_OR_DISTORT_NO_NORMAL_DRAW");
        if(!KichR4_NativeDistortionEnabled()){
            const uint32_t candidates[3]={4,0,1};for(unsigned i=0;i<3;++i){const uint32_t index=candidates[i];if(elements[index].passes && elements[index].distort){RenderSnapshot_MaterialSelection(v->shader->E[index]._get(),UINT32_MAX,index,available_elements,"SPECIAL_DISTORTION_DISABLED_BY_SETTING");break;}}
        }
    }
    const float geometry_lod=NativeGeometryLOD(v,original,val_bHUD!=FALSE);
    const uint32_t indices[2]={selected.normal,selected.distortion};
    for(u32 route=0;route<2;++route){const uint32_t index=indices[route];if(index==UINT32_MAX)continue;
        ShaderElement* element=v->shader->E[index]._get();
        const kich_r4_material::Descriptor* material=KichR4_NativeFindMaterial(element,0);
        // R1 multipass modulation is represented once by native material data.
        // Special/unknown sources keep every original pass and binding.
        const u32 pass_count=material && kich_r4_material::Geometry(*material)?1:element->passes.size();
        for(u32 pass=0;pass<pass_count;++pass){
            KichR4NativeEmissiveScope native_emissive(route==0?emissive_gain:0,route==0?emissive_specular:0);
            RCache.set_xform_world(original);RCache.set_Element(element,pass);KichR4_NativeElementSource(index);
            if(RenderSnapshot_Detailed())RenderSnapshot_MaterialSelection(element,pass,index,available_elements,
                route?(index==4?"SPECIAL_E4_FLAGGED_DISTORTION_ENABLED":index==0?"SPECIAL_E4_UNAVAILABLE_FLAGGED_E0_FALLBACK":"SPECIAL_E4_E0_UNAVAILABLE_FLAGGED_E1_FALLBACK"):index==1?"NORMAL_E0_UNAVAILABLE_OR_DISTORT_FALLBACK_E1":"NORMAL_E0_AVAILABLE");
            v->Render(geometry_lod);
        }
    }
    RCache.set_xform_world(original);
}
void CRender::flush(){const Fmatrix original=Device.mProject;Fmatrix hands=original;extern ENGINE_API float psHUD_FOV;
    if(g_pGamePersistent)hands.build_projection(deg2rad(psHUD_FOV*Device.fFOV),Device.fASPECT,VIEWPORT_NEAR,g_pGamePersistent->Environment().CurrentEnv.far_plane);
    if(g_render_snapshot_active){kich_r4_telemetry::Vector("HUD","projection","world_projection",&original._11,16);kich_r4_telemetry::Vector("HUD","projection","hands_projection",&hands._11,16);
        kich_r4_telemetry::Float("HUD","projection","psHUD_FOV_multiplier",psHUD_FOV);kich_r4_telemetry::Float("HUD","projection","camera_fov_degrees",Device.fFOV);
        kich_r4_telemetry::Text("HUD","projection","usage","hands_projection_BOUND_FOR_hud_QUEUE_ONLY; WORLD_PROJECTION_RESTORED_AFTER_FLUSH");}
    for(u32 i=0;i<queue.size();++i){const Queued& q=queue[i];KichR4_NativeObjectLight(q.light_valid,q.hemi,q.sun);val_bHUD=q.hud;RCache.set_xform_world(q.world);if(q.hud){rmNear();RCache.set_xform_project(hands);}else{rmNormal();RCache.set_xform_project(original);}Submit(q.visual,NULL);}
    KichR4_NativeObjectLight(false,1,1);
    queue.clear();val_bHUD=FALSE;rmNormal();RCache.set_xform_project(original);
}
void CRender::Calculate(){queue.clear();shadow_views.clear();native_lod_shadow_views.clear();if(!loaded || !g_pGameLevel)return;ViewBase.CreateFromMatrix(Device.mFullTransform,FRUSTUM_P_ALL);View=&ViewBase;phase=PHASE_NORMAL;HOM.Prepare(ViewBase);
    // Camera and shadow subspaces are independent in the donor renderer. The
    // native path gathers their conservative union, then culls immutable model
    // commands against the actual pass matrix in the backend.
    KichR4LightingFrame lighting={};lighting.valid=1;
    KichR4_CopyEngineEnvironment(lighting);
    KichR4_FreezeLighting(Device.dwFrame,Device.dwTimeGlobal,&Device.vCameraPosition.x,lighting,ps_r2_ls_flags.test(R2FLAG_R1LIGHTS)!=0,&Device.mFullTransform._11);
    KichR4_CaptureLightingDiagnostics(&Device.vCameraPosition.x,lighting);
    if(g_render_snapshot_active){RenderSnapshot_U64("LIGHT_POLICY","R2FLAG_R1LIGHTS_requested",ps_r2_ls_flags.test(R2FLAG_R1LIGHTS)?1:0);
        RenderSnapshot_U64("LIGHT_POLICY","R2FLAG_R1LIGHTS_applied",lighting.r1_lights_applied);
        RenderSnapshot_U64("LIGHT_POLICY","level_file_records",native_level_light_records);RenderSnapshot_U64("LIGHT_POLICY","level_directional_records_skipped",native_level_directional_skips);
        RenderSnapshot_Text("LIGHT_POLICY","level_directional_skip_reason","NATIVE_LOADER_USES_ENVIRONMENT_SUN; EXISTING_POLICY");
        RenderSnapshot_Text("LIGHT_POLICY","native_level_light_policy","REGISTER_ALL_NONDIRECTIONAL_LEVEL_RECORDS; BSTATIC_REQUIRES_R2FLAG_R1LIGHTS; SOURCE_FRUSTUM_CULL; NO_TOP_8_LIGHT_LIMIT");}
    const bool sun_enabled=kich_r4_visibility::SunEnabled(lighting);
    kich_r4_scene::Matrix camera_vp;CopyMemory(camera_vp.m,Device.mFullTransform.m,sizeof(camera_vp.m));
    const kich_r4_visibility::SunCasterVolume sun_casters=kich_r4_visibility::BuildSunCasterVolume(camera_vp,lighting.sun_direction);
    if(sun_enabled){const kich_r4_scene::Matrix vp=kich_r4_lighting::SunShadow(&Device.vCameraPosition.x,lighting.sun_direction);
        const NativeLodView near_view={kich_r4_lighting::SunShadow(&Device.vCameraPosition.x,lighting.sun_direction,128.f),2048};native_lod_shadow_views.push_back(near_view);
        const NativeLodView far_view={vp,2048};native_lod_shadow_views.push_back(far_view);
        Fmatrix matrix;CopyMemory(&matrix,vp.m,sizeof(matrix));CFrustum view;view.CreateFromMatrix(matrix,FRUSTUM_P_ALL);shadow_views.push_back(view);}
    kich_r4_visibility::SelectShadowLights(lighting,true,selected_shadow_lights);
    for(u32 i=0;i<lighting.count;++i)if(selected_shadow_lights[i]){const u32 faces=lighting.lights[i].type==2?1:6;
        for(u32 face=0;face<faces;++face){const kich_r4_scene::Matrix vp=kich_r4_lighting::LocalShadow(lighting.lights[i],face,lighting.local_shadow_size);
            const NativeLodView lod_view={vp,lighting.local_shadow_size};native_lod_shadow_views.push_back(lod_view);
            Fmatrix matrix;CopyMemory(&matrix,vp.m,sizeof(matrix));CFrustum view;view.CreateFromMatrix(matrix,FRUSTUM_P_ALL);shadow_views.push_back(view);}}
    if(g_pGameLevel->pHUD){g_pGameLevel->pHUD->Render_First();g_pGameLevel->pHUD->Render_Last();}
    if(psDeviceFlags.test(rsDrawDynamic)){candidate_seen.Begin();u32 camera_sources=0,shadow_sources=0;
        xr_vector<ISpatial*>& objects=candidate_objects;
        for(u32 pass=0;pass<=shadow_views.size();++pass){objects.clear_not_free();
            g_SpatialSpace->q_frustum(objects,ISpatial_DB::O_ORDERED,STYPE_RENDERABLE,pass?shadow_views[pass-1]:ViewBase);
            for(u32 i=0;i<objects.size();++i){IRenderable* r=objects[i]->dcast_Renderable();if(!r)continue;
                if(pass==1 && sun_enabled && !sun_casters.VisibleSphere(&objects[i]->spatial.sphere.P.x,objects[i]->spatial.sphere.R))continue;
                if(!candidate_seen.Insert(r))continue;
                if(pass)++shadow_sources;else ++camera_sources;objects[i]->spatial_updatesector();set_Object(r);r->renderable_Render();}}
        set_Object(NULL);candidate_seen.Finish();objects.clear_not_free();
        if(objects.capacity()>65536)xr_vector<ISpatial*>().swap(objects);
        if(g_render_snapshot_active){RenderSnapshot_Text("MODEL_CANDIDATES","scope","SPATIAL_CAMERA_UNION_SELECTED_SHADOW_FRUSTUMS; UNIQUE_RENDERABLE_SOURCE");
            RenderSnapshot_U64("MODEL_CANDIDATES","camera_sources",camera_sources);RenderSnapshot_U64("MODEL_CANDIDATES","additional_shadow_sources",shadow_sources);
            RenderSnapshot_U64("MODEL_CANDIDATES","shadow_frustums",shadow_views.size());}}
}
void CRender::Render(){if(!g_pGamePersistent)return;
    // The main menu asks the renderer to invoke its PP-UI callback even without a level.
    if(g_pGamePersistent->OnRenderPPUI_query()){queue.clear();g_pGamePersistent->OnRenderPPUI_main();
        KichR4_NativeMenuPP(true);g_pGamePersistent->OnRenderPPUI_PP();KichR4_NativeMenuPP(false);return;}
    if(!loaded)return;flush();if(Details)Details->Render();g_pGamePersistent->Environment().RenderSky();g_pGamePersistent->Environment().RenderClouds();
    if(Wallmarks)Wallmarks->Render();ZeroMemory(&glow_stats,sizeof(glow_stats));for(u32 i=0;i<glows.size();++i)glows[i]->Draw();
    if(g_render_snapshot_active){RenderSnapshot_U64("GLOW_POLICY","active",glow_stats.active);RenderSnapshot_U64("GLOW_POLICY","frustum_rejected",glow_stats.frustum_rejected);
        RenderSnapshot_U64("GLOW_POLICY","facing_rejected",glow_stats.facing_rejected);RenderSnapshot_U64("GLOW_POLICY","queued",glow_stats.queued);
        RenderSnapshot_U64("GLOW_POLICY","representative_records",glow_stats.recorded);RenderSnapshot_Text("GLOW_POLICY","detail_budget","32_QUEUED_SOURCES; DRAW_SUBMISSION_UNLIMITED; PIXEL_VISIBILITY_NOT_MEASURED");}
    g_pGamePersistent->Environment().RenderFlares();g_pGamePersistent->Environment().RenderLast();
}
BOOL CRender::occ_visible(vis_data& v){if(ViewBase.testSphere_dirty(v.sphere.P,v.sphere.R))return TRUE;
    for(u32 i=0;i<shadow_views.size();++i)if(shadow_views[i].testSphere_dirty(v.sphere.P,v.sphere.R))return TRUE;return FALSE;}
BOOL CRender::occ_visible(Fbox& b){u32 mask=ViewBase.getMask();if(ViewBase.testAABB(&b.min.x,mask)!=fcvNone)return TRUE;
    for(u32 i=0;i<shadow_views.size();++i){mask=shadow_views[i].getMask();if(shadow_views[i].testAABB(&b.min.x,mask)!=fcvNone)return TRUE;}return FALSE;}
BOOL CRender::occ_visible(sPoly& p){if(p.empty())return FALSE;Fbox b;b.invalidate();for(u32 i=0;i<p.size();++i)b.modify(p[i]);return occ_visible(b);}
void CRender::add_StaticWallmark(ref_shader& s,const Fvector& p,float size,CDB::TRI* t,Fvector* v){if(Wallmarks && t && !t->suppress_wm)Wallmarks->AddStaticWallmark(t,v,p,s,size);else kich_wallmark_snapshot::Event("STATIC_REQUEST",t,"REJECTED",!Wallmarks?"NO_WALLMARK_ENGINE":!t?"NO_RECEIVER":"RECEIVER_SUPPRESS_WM");}
void CRender::clear_static_wallmarks(){if(Wallmarks)Wallmarks->clear();}
void CRender::add_SkeletonWallmark(intrusive_ptr<CSkeletonWallmark> w){if(Wallmarks)Wallmarks->AddSkeletonWallmark(w);}
void CRender::add_SkeletonWallmark(const Fmatrix* m,CKinematics* k,ref_shader& s,const Fvector& p,const Fvector& d,float size){if(Wallmarks)Wallmarks->AddSkeletonWallmark(m,k,s,p,d,size);else kich_wallmark_snapshot::Event("SKELETAL_REQUEST",k,"REJECTED","NO_WALLMARK_ENGINE");}
IRender_ObjectSpecific* CRender::ros_create(IRenderable* p){return xr_new<R4ROS>(p);}void CRender::ros_destroy(IRender_ObjectSpecific*& p){xr_delete(p);}
IRender_Light* CRender::light_create(){return xr_new<R4Light>();}IRender_Glow* CRender::glow_create(){return xr_new<R4Glow>();}
IRender_Visual* CRender::model_CreatePE(LPCSTR name){PS::CPEDef* d=PSLibrary.FindPED(name);R_ASSERT(d);return Models->CreatePE(d);}
IRender_Visual* CRender::model_CreateParticles(LPCSTR name){PS::CPEDef* d=PSLibrary.FindPED(name);if(d)return Models->CreatePE(d);PS::CPGDef* g=PSLibrary.FindPGD(name);R_ASSERT(g);return Models->CreatePG(g);}
IRender_DetailModel* CRender::model_CreateDM(IReader* f){CDetail* d=xr_new<CDetail>();d->Load(f);return d;}
IRender_Visual* CRender::model_Create(LPCSTR n,IReader* f){return Models->Create(n,f);}IRender_Visual* CRender::model_CreateChild(LPCSTR n,IReader* f){return Models->CreateChild(n,f);}
IRender_Visual* CRender::model_Duplicate(IRender_Visual* v){return Models->Instance_Duplicate(v);}void CRender::model_Delete(IRender_Visual*& v,BOOL discard){Models->Delete(v,discard);}
void CRender::model_Delete(IRender_DetailModel*& v){if(v)static_cast<CDetail*>(v)->Unload();xr_delete(v);}
void CRender::Screenshot(ScreenshotMode m,LPCSTR n){KichR4_NativeScreenshot(m,n);}
void CRender::rmNear(){KichR4_NativeViewport(0,.02f);}void CRender::rmFar(){KichR4_NativeViewport(.99999f,1);}void CRender::rmNormal(){KichR4_NativeViewport(0,1);}

void KichR4_NativePostprocess(float* p){CR4Target* t=static_cast<CR4Target*>(RImplementation.getTarget());Fcolor balance;Device.Gamma.GetIP(p[0],p[1],p[2],balance);p[3]=t->gray;p[4]=t->blur;p[5]=t->duality_h;p[6]=t->duality_v;p[7]=t->noise;
    static kich_r4_noise::Cadence noise_clock;p[8]=noise_clock.Advance(Device.dwFrame,Device.fTimeDelta,t->noise_fps);p[9]=t->noise_scale;
    const u32 values[3]={t->color_base,t->color_add,t->color_gray};for(u32 i=0;i<3;++i){p[10+i*3]=color_get_R(values[i])/255.f;p[11+i*3]=color_get_G(values[i])/255.f;p[12+i*3]=color_get_B(values[i])/255.f;}p[19]=balance.r;p[20]=balance.g;p[21]=balance.b;
}
