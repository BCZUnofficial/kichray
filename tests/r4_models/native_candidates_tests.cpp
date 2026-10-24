#include "../../xr_3da/KichR4Visibility.h"
#include "../../xr_3da/KichR4CandidateScratch.h"
#include "../../xr_3da/RenderSnapshot.h"
#include <vector>
#include <set>
#include <cstring>
#include <stdexcept>
#include <stdio.h>
typedef uint32_t u32;
template<class T>struct xr_vector:std::vector<T>{void clear_not_free(){this->erase(this->begin(),this->end());}};
#define CopyMemory std::memcpy
#define FRUSTUM_P_ALL 63
#define STYPE_RENDERABLE 1
#define rsDrawDynamic 1
struct Fvector {float x,y,z;};
struct Fmatrix {union {float m[16];float _11;};};
struct CFrustum {
    kich_r4_scene::Matrix vp;
    void CreateFromMatrix(const Fmatrix& m,unsigned){std::memcpy(vp.m,m.m,64);}
};
struct IRenderable {unsigned rendered;IRenderable():rendered(0){}void renderable_Render(){++rendered;}};
struct ISpatial {
    IRenderable* renderable;float low[3],high[3];unsigned updates;
    struct Spatial {struct Sphere {Fvector P;float R;} sphere;} spatial;
    IRenderable* dcast_Renderable(){return renderable;}void spatial_updatesector(){++updates;}
};
struct ISpatial_DB {
    enum{O_ORDERED=1};std::vector<ISpatial*> objects;unsigned queries;
    ISpatial_DB():queries(0){}
    void q_frustum(std::vector<ISpatial*>& out,unsigned,unsigned,const CFrustum& view){++queries;
        for(size_t i=0;i<objects.size();++i)if(kich_r4_visibility::Visible(objects[i]->low,objects[i]->high,view.vp))out.push_back(objects[i]);}
} spatial;
ISpatial_DB* g_SpatialSpace=&spatial;
struct HUD {unsigned calls;HUD():calls(0){}void Render_First(){++calls;}void Render_Last(){++calls;}} hud;
struct Level {HUD* pHUD;} level={&hud};Level* g_pGameLevel=&level;
struct CEnvDescriptor {Fvector sun_dir,sun_color,ambient,hemi_color,fog_color;float fog_near,fog_far;};
struct Weather {CEnvDescriptor CurrentEnv;} weather={{{0,-1,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},35,160}};
struct Persistent {Weather& Environment(){return weather;}} persistent;Persistent* g_pGamePersistent=&persistent;
struct DeviceState {Fmatrix mFullTransform;Fvector vCameraPosition;u32 dwFrame,dwTimeGlobal;} Device;
struct Flags {bool enabled;bool test(unsigned){return enabled;}} psDeviceFlags={true};
Flags ps_r2_ls_flags={false};enum{R2FLAG_R1LIGHTS=1u<<10};u32 native_level_light_records=0,native_level_directional_skips=0;
KichR4LightingFrame lights={};bool light_visibility_requested=false,environment_fog_received=false;
void KichR4_CaptureLightingDiagnostics(const float*,const KichR4LightingFrame&){}
void KichR4_FreezeLighting(uint32_t,uint32_t,const float*,KichR4LightingFrame& out,bool r1_lights_enabled,const float* camera_vp){light_visibility_requested=camera_vp && !memcmp(camera_vp,Device.mFullTransform.m,64);environment_fog_received=out.fog_valid && out.fog_near==35 && out.fog_far==160;out=lights;out.r1_lights_requested=out.r1_lights_applied=r1_lights_enabled?1u:0u;}
class CRender {
public:
    enum{PHASE_NORMAL=0};unsigned phase;bool loaded;std::vector<int> queue;std::vector<CFrustum> shadow_views;CFrustum ViewBase,*View;
    xr_vector<ISpatial*> candidate_objects;kich_r4_candidates::UniquePointers<IRenderable> candidate_seen;
    std::vector<uint8_t> selected_shadow_lights;
    IRenderable* object;CRender():phase(0),loaded(true),View(NULL),object(NULL){}
    struct FakeHOM {void Prepare(CFrustum&){} } HOM;
    void set_Object(IRenderable* p){object=p;}void Calculate();
};
struct NativeLodView {kich_r4_scene::Matrix vp;uint32_t resolution;};
std::vector<NativeLodView> native_lod_shadow_views;
#include "native_calculate_under_test.cpp"
namespace {unsigned checks=0;void Require(bool good,const char* message){++checks;if(!good)throw std::runtime_error(message);}}
int main(){try{
    const kich_r4_scene::Matrix identity=kich_r4_scene::Identity();std::memcpy(Device.mFullTransform.m,identity.m,64);Device.vCameraPosition={0,0,0};
    lights.count=1;lights.shadow_mask=1;lights.lights[0].type=1;lights.lights[0].range=10;
    IRenderable inside,shadow,outside;
    ISpatial camera={&inside,{-.1f,-.1f,.4f},{.1f,.1f,.6f},0,{{{0,0,.5f},.2f}}};
    ISpatial caster={&shadow,{4.f,-.1f,-.1f},{5.f,.1f,.1f},0,{{{4.5f,0,0},.6f}}};
    ISpatial duplicate=caster;
    ISpatial distant={&outside,{20.f,-.1f,-.1f},{21.f,.1f,.1f},0,{{{20.5f,0,0},.6f}}};
    spatial.objects={&camera,&caster,&duplicate,&distant};CRender renderer;renderer.Calculate();
    Require(light_visibility_requested,"native selection receives the actual game camera frustum");
    Require(environment_fog_received,"early native selection freezes complete mixed environment before presentation can copy it");
    Require(spatial.queries==7 && renderer.shadow_views.size()==6,"camera plus all selected point faces queried");
    Require(inside.rendered==1 && shadow.rendered==1 && outside.rendered==0,"production Calculate retains off-camera caster and culls outside all volumes");
    Require(camera.updates==1 && caster.updates+duplicate.updates==1,"renderable identity deduplicates across views and spatial aliases");
    Require(renderer.object==NULL && renderer.View==&renderer.ViewBase && hud.calls==2,"camera View and HUD path preserved");
    const size_t object_capacity=renderer.candidate_objects.capacity(),seen_capacity=renderer.candidate_seen.Capacity();
    lights.shadow_mask=0;renderer.Calculate();
    Require(inside.rendered==2 && shadow.rendered==1 && renderer.shadow_views.empty(),"disabled shadow source does not retain off-camera model");
    Require(renderer.candidate_objects.capacity()==object_capacity && renderer.candidate_seen.Capacity()==seen_capacity,"next frame reuses spatial and deduplication capacity");
    Require(renderer.candidate_objects.empty() && renderer.candidate_seen.Size()==1,"previous frame identities/results are not reused as visibility");
    psDeviceFlags.enabled=false;renderer.Calculate();Require(inside.rendered==2 && hud.calls==6,"dynamic toggle leaves HUD calls active");
    renderer.loaded=false;renderer.queue.push_back(1);renderer.Calculate();Require(renderer.queue.empty() && renderer.shadow_views.empty(),"inactive level clears old candidate volumes");
    printf("G3b production native candidate collection: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
