/* The runner inserts the unchanged production bridge function at the marker. */
#include "../../xr_3da/KichR4Game.h"
#include "../../xr_3da/KichR4RecordControl.h"
#include <vector>
#include <string.h>
#include <stdexcept>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
namespace {
int checks=0,prepared=0,native_presented=0,light_copies=0,light_freezes=0,light_captures=0;bool armed=false,key=false,frozen_available=false,g_appLoaded=true;
int observation_reads=0;
KichR4LightingFrame frozen_lighting;
KichR4GameFrame captured={},native_captured={};
struct WeatherVector {float x,y,z,w;};
struct CObject {uint32_t id;WeatherVector position;
    uint32_t ID()const{++observation_reads;return id;}
    const WeatherVector& Position()const{++observation_reads;return position;}
} entity={17,{11,12,13,0}},other_entity={29,{21,22,23,0}};
enum {cefDemo=0};
struct CameraManagerType {void* demo;void* GetCamEffector(int type){++observation_reads;return type==cefDemo?demo:NULL;}} cameras={NULL};
struct Level {bool bReady;CObject* view;CObject* current;
    CObject* CurrentViewEntity(){return view;}
    CObject* CurrentEntity(){++observation_reads;return current;}
    CameraManagerType& Cameras(){++observation_reads;return cameras;}
} level={true,&entity,&entity};
Level* g_pGameLevel=&level;
struct Menu {bool active;bool IsActive(){return active;}} menu={false};
struct CEnvDescriptorMixer {WeatherVector sun_dir,sun_color,ambient,hemi_color,fog_color;};
struct EnvironmentType {CEnvDescriptorMixer CurrentEnv;} environment={};
struct Persistent {Menu* m_pMainMenu;bool ppui;EnvironmentType& Environment(){return environment;}bool OnRenderPPUI_query(){return ppui;}} persistent={&menu,false};
Persistent* g_pGamePersistent=&persistent;
struct ConsoleType {bool bVisible;} console={false};ConsoleType* Console=&console;
struct EngineDevice {void* m_hWnd;unsigned dwWidth,dwHeight,dwFrame,dwPrecacheFrame,dwTimeGlobal;float fTimeGlobal,fFOV;
    struct {float m[4][4];} mFullTransform;struct {float x,y,z;} vCameraPosition,vCameraDirection;int b_is_Active;bool paused;bool Paused(){return paused;}} Device={};
std::vector<int> g_loading_events;
const int FALSE=0,VK_F8=119,VK_F9=120;
short GetAsyncKeyState(int which){return which==VK_F8 && key?static_cast<short>(0x8000):0;}
void Require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
}
bool KichR4_GameWindowed(){return armed;}
struct LightFlags {bool enabled;bool test(unsigned)const{return enabled;}} ps_r2_ls_flags={false};
enum {R2FLAG_R1LIGHTS=1u<<10};
const float PI_MUL_2=6.28318530718f;
float _sin(float x){return sinf(x);}float _cos(float x){return cosf(x);}float _sqrt(float x){return sqrtf(x);}
float ps_r__Tree_w_rot=10,ps_r__Tree_w_speed=1,ps_r__Tree_w_amp=.005f,ps_r__Tree_SBC=1.5f;
float ps_r__LOD=.9f,ps_r__GLOD_ssa_start=320.f,ps_r__GLOD_ssa_end=48.f;
float ps_r__Detail_radius=60.f;
WeatherVector ps_r__Tree_Wave={.1f,.01f,.11f,0};
typedef uint32_t u32;
#define CopyMemory memcpy
#define R_ASSERT2(condition,message) Require((condition),(message))
void Msg(const char*,...){}
double KichR4_NativeQueueTime(){return 1.5;}
void KichR4_NativePostprocess(float* output){memset(output,0,24*sizeof(float));}
bool KichR4_NativePresent(const KichR4GameFrame& input){++native_presented;native_captured=input;return true;}
void KichR4_CaptureLighting(const float*,KichR4LightingFrame& out,bool r1_lights_enabled,const float* camera_vp){++light_captures;Require(camera_vp && !memcmp(camera_vp,Device.mFullTransform.m,64),"both engine frame adapters provide the current camera for light culling");out.count=0;out.r1_lights_requested=out.r1_lights_applied=r1_lights_enabled?1u:0u;}
bool KichR4_CopyFrozenLighting(uint32_t frame,const float* camera,const float* vp,KichR4LightingFrame& out){++light_copies;Require(frame==Device.dwFrame && camera && vp && !memcmp(vp,Device.mFullTransform.m,64),"frozen snapshot uses this engine frame and camera");if(!frozen_available)return false;out=frozen_lighting;return true;}
void KichR4_FreezeLighting(uint32_t frame,uint32_t now,const float* camera,KichR4LightingFrame& out,bool r1,const float* vp){++light_freezes;Require(frame==Device.dwFrame && now==Device.dwTimeGlobal,"native frame freezes at engine time");KichR4_CaptureLighting(camera,out,r1,vp);out.selection_frame=frame;out.local_shadow_size=512;}
void KichR4_GamePrepare(const KichR4GameFrame& in){++prepared;captured=in;}
void KichR4_NativePresentEngineFrame(bool loading,bool precaching);
LightFlags psDeviceFlags={true};enum {rsVSync=1};
void KichR4_NativeSetInterval(bool){}
struct {unsigned ends=0;void OnFrameEnd(){++ends;}} RCache;
struct NativeEndState {bool loading=false,precaching=false;
// NATIVE_PREPARE_FRAME_END
// NATIVE_END_FRAME
};
// PRODUCTION_FUNCTION
int main(){try {
    KichR4_GamePrepareEngineFrame();Require(!prepared,"unarmed engine does not touch bridge input");armed=true;
    Device.m_hWnd=reinterpret_cast<void*>(1);Device.dwWidth=1280;Device.dwHeight=720;Device.dwFrame=123;Device.b_is_Active=1;Device.fFOV=67.5f;
    for(unsigned i=0;i<16;++i)(&Device.mFullTransform.m[0][0])[i]=static_cast<float>(i)+.25f;
    Device.vCameraPosition={1.f,2.f,3.f};key=true;KichR4_GamePrepareEngineFrame();
#if defined(DEDICATED_SERVER) || defined(_EDITOR)
    Require(!prepared,"dedicated and editor builds skip experimental game input");
    (void)&GetAsyncKeyState;(void)g_pGameLevel;(void)g_pGamePersistent;(void)Console;(void)g_loading_events;(void)other_entity;
#else
    Require(prepared==1 && captured.allowed && captured.active && captured.toggle_down,"playable input and F8 sampled");
    Require(!captured.observation.valid && observation_reads==0,"recording defaults off and does not query entity or demo state");
    Require(captured.width==1280 && captured.height==720 && captured.engine_frame==123 && captured.window==Device.m_hWnd,"game window and frame dimensions forwarded");
    Require(captured.geometry_lod[0]==ps_r__LOD && captured.geometry_lod[1]==ps_r__GLOD_ssa_start && captured.geometry_lod[2]==ps_r__GLOD_ssa_end && captured.geometry_lod[3]==Device.fFOV,"progressive detail follows current R2 console settings and camera FOV");
    Require(!memcmp(captured.view_projection,Device.mFullTransform.m,64) && captured.position[0]==1 && captured.position[2]==3,"camera matrix passed byte-for-byte without a viewer camera");
    environment.CurrentEnv.sun_dir={.1f,-.9f,.2f,0};environment.CurrentEnv.sun_color={.5f,.6f,.7f,0};
    environment.CurrentEnv.ambient={.04f,.05f,.06f,0};environment.CurrentEnv.hemi_color={.2f,.3f,.4f,1};environment.CurrentEnv.fog_color={.6f,.5f,.4f,0};
    KichR4_GamePrepareEngineFrame();Require(captured.lighting.valid && captured.lighting.sun_direction[1]==-.9f && captured.lighting.sun_color[2]==.7f,"weather sun copied from engine");
    Require(captured.lighting.ambient[1]==.05f && captured.lighting.hemi[2]==.4f && captured.lighting.fog[0]==.6f,"ambient/hemi/fog copied without alpha padding");
    Require(!captured.lighting.r1_lights_applied,"engine forwards disabled static light policy");ps_r2_ls_flags.enabled=true;KichR4_GamePrepareEngineFrame();
    Require(captured.lighting.r1_lights_requested && captured.lighting.r1_lights_applied,"engine forwards current R1LIGHTS value on the next frame");
    Device.fTimeGlobal=2.5f;ps_r__Tree_w_amp=.25f;ps_r__Tree_w_speed=3;ps_r__Tree_SBC=2;
    ps_r__Tree_Wave={.3f,.2f,.1f,0};KichR4_GamePrepareEngineFrame();
    Require(fabsf(captured.tree_wind[0]-.25f)<1.e-6f && fabsf(captured.tree_wind[2])<1.e-6f && captured.tree_wind[1]==0,"tree wind follows source normalized sine/cosine rotation and console amplitude");
    Require(fabsf(captured.tree_wave[0]-.3f/PI_MUL_2)<1.e-6f && fabsf(captured.tree_wave[3]-7.5f/PI_MUL_2)<1.e-6f && captured.tree_sbc==2*1.3333f,"tree spatial/time phase and R2 color correction copied from current console settings");
    KichR4_NativePresentEngineFrame(false,false);
    Require(native_presented==1 && !memcmp(native_captured.tree_wave,captured.tree_wave,16) && !memcmp(native_captured.tree_wind,captured.tree_wind,16) && native_captured.tree_sbc==captured.tree_sbc,"native presenter and prepared path share exact tree frame parameters");
    Require(!memcmp(native_captured.geometry_lod,captured.geometry_lod,sizeof(captured.geometry_lod)),"native and prepared frame paths share LOD policy");
    Device.fTimeGlobal=5;ps_r__Tree_w_amp=.5f;KichR4_NativePresentEngineFrame(true,false);
    Require(native_presented==2 && fabsf(native_captured.tree_wind[2]+.5f)<1.e-6f && native_captured.tree_wave[3]!=captured.tree_wave[3],"live tree time and settings are resampled without per-frame geometry generation");
    Require(!native_captured.allowed && !native_captured.warmup,"ordinary loading remains UI only");
    NativeEndState end_state;
    Device.dwPrecacheFrame=1;end_state.PrepareFrameEnd(0,0);Device.dwPrecacheFrame=0;
    Require(end_state.loading && end_state.precaching,"native gate preserves the last precache frame before End decrements it");
    frozen_available=true;frozen_lighting.valid=1;frozen_lighting.local_shadow_size=256;frozen_lighting.hemi[0]=.75f;
    const int freezes_before=light_freezes,captures_before=light_captures;
    end_state.EndFrame();
    Require(native_captured.warmup && !native_captured.allowed,"final precache frame warms the scene without becoming gameplay");
    Require(RCache.ends==1,"native EndFrame forwards captured flags before completing cache frame");
    Require(native_captured.lighting.local_shadow_size==256 && native_captured.lighting.hemi[0]==.75f && light_freezes==freezes_before && light_captures==captures_before,"warmup reuses frozen geometry lighting and does not reselect sources");
    KichR4_GamePrepareEngineFrame();Require(!captured.warmup,"R2 bridge never opts into native precache warmup");
    frozen_available=false;
    for(unsigned blocked=0;blocked<6;++blocked){
        level.bReady=true;level.view=&entity;g_pGameLevel=&level;g_appLoaded=true;g_loading_events.clear();persistent.ppui=false;Device.dwPrecacheFrame=1;
        switch(blocked){case 0:g_appLoaded=false;break;case 1:g_loading_events.push_back(1);break;case 2:level.bReady=false;break;case 3:level.view=NULL;break;case 4:persistent.ppui=true;break;case 5:g_pGameLevel=NULL;break;}
        end_state.PrepareFrameEnd(0,0);end_state.EndFrame();
        Require(!native_captured.warmup,"loading events, absent level/camera and menu postprocess block world warmup");
    }
    level.bReady=true;level.view=&entity;g_pGameLevel=&level;g_appLoaded=true;g_loading_events.clear();persistent.ppui=false;Device.dwPrecacheFrame=0;
    end_state.PrepareFrameEnd(0,0);Require(!end_state.loading && !end_state.precaching,"normal gameplay clears both captured flags");
    end_state.EndFrame();Require(native_captured.allowed && !native_captured.warmup,"normal gameplay returns to its original presentation path");
    for(unsigned blocked=0;blocked<7;++blocked) {
        level.bReady=true;level.view=&entity;menu.active=false;console.bVisible=false;Device.dwPrecacheFrame=0;Device.paused=false;g_loading_events.clear();
        switch(blocked) {case 0:level.bReady=false;break;case 1:level.view=NULL;break;case 2:menu.active=true;break;case 3:console.bVisible=true;break;
            case 4:Device.dwPrecacheFrame=1;break;case 5:g_loading_events.push_back(1);break;case 6:Device.paused=true;break;}
        KichR4_GamePrepareEngineFrame();Require(!captured.allowed,"loading/console/menu/no player camera prevents R4 output");
    }
    g_loading_events.clear();Device.b_is_Active=0;KichR4_GamePrepareEngineFrame();Require(!captured.active,"inactive app forwarded");

    // Record context follows the actual camera, controlled object and view
    // object independently. No xrGame actor cast or Debug-only teleport logic.
    level.bReady=true;level.current=&entity;level.view=&other_entity;g_pGameLevel=&level;
    Device.vCameraPosition={101,102,103};Device.vCameraDirection={.25f,-.5f,.75f};
    Device.paused=true;console.bVisible=true;ps_r__Detail_radius=55.5f;cameras.demo=reinterpret_cast<void*>(1);
    kich_r4_record::Request(true);kich_r4_record::BeginFrame(Device.dwFrame);
    KichR4_GamePrepareEngineFrame();
    Require(captured.observation.valid && captured.observation.demo_active,"recording detects the real demo effector independently of object distance");
    Require(captured.observation.paused && captured.observation.console_visible && captured.observation.detail_radius==55.5f,"pause, console and effective detail radius accompany the observed frame");
    Require(captured.position[0]==101 && captured.observation.camera_direction[0]==.25f && captured.observation.camera_direction[2]==.75f,"flight camera pose stays independent of controlled and view objects");
    Require(captured.observation.current_entity_valid && captured.observation.current_entity_id==17 && captured.observation.current_entity_position[0]==11 && captured.observation.current_entity_position[2]==13,"CurrentEntity is identified and located without calling it a type-checked actor");
    Require(captured.observation.view_entity_valid && captured.observation.view_entity_id==29 && captured.observation.view_entity_position[0]==21 && captured.observation.view_entity_position[2]==23,"CurrentViewEntity is preserved separately from CurrentEntity");
    KichR4_NativePresentEngineFrame(false,false);
    Require(native_captured.observation.valid && native_captured.observation.demo_active && native_captured.observation.current_entity_id==17 && native_captured.observation.view_entity_id==29,"native presentation carries the same camera/entity observation contract");
    Require(native_captured.observation.paused && native_captured.observation.console_visible && native_captured.observation.detail_radius==captured.observation.detail_radius,"native context retains pause/console flags instead of inferring them from allowed");
    cameras.demo=NULL;level.current=NULL;level.view=NULL;KichR4_NativePresentEngineFrame(true,false);
    Require(native_captured.observation.valid && !native_captured.observation.demo_active && !native_captured.observation.current_entity_valid && !native_captured.observation.view_entity_valid,"missing objects and inactive demo are explicit, without stale observations");
    level.current=&entity;level.view=&other_entity;level.bReady=false;const int before_unready=observation_reads;
    KichR4_NativePresentEngineFrame(true,false);
    Require(observation_reads==before_unready && !native_captured.observation.current_entity_valid && !native_captured.observation.view_entity_valid,"unready level never dereferences object or camera-manager state");
    g_pGameLevel=NULL;KichR4_NativePresentEngineFrame(true,false);
    Require(native_captured.observation.valid && !native_captured.observation.demo_active && !native_captured.observation.current_entity_valid,"no-level UI still records camera and flags without a level lookup");
    g_pGameLevel=&level;level.bReady=true;level.current=&entity;level.view=&other_entity;
    kich_r4_record::Request(false);kich_r4_record::BeginFrame(Device.dwFrame+1);const int before_off=observation_reads;
    KichR4_GamePrepareEngineFrame();KichR4_NativePresentEngineFrame(false,false);
    Require(observation_reads==before_off && !captured.observation.valid && !native_captured.observation.valid,"switching off removes optional object/demo reads in both frame adapters");
#endif
    printf("B4.3a production engine camera/gating seam: %d checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
