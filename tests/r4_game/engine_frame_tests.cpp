/* The runner inserts the unchanged production bridge function at the marker. */
#include "../../xr_3da/KichR4Game.h"
#include <vector>
#include <string.h>
#include <stdexcept>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
namespace {
int checks=0,prepared=0,native_presented=0;bool armed=false,key=false;
KichR4GameFrame captured={},native_captured={};
struct Entity {} entity;
struct Level {bool bReady;Entity* view;Entity* CurrentViewEntity(){return view;}} level={true,&entity};
Level* g_pGameLevel=&level;
struct Menu {bool active;bool IsActive(){return active;}} menu={false};
struct WeatherVector {float x,y,z,w;};
struct CEnvDescriptorMixer {WeatherVector sun_dir,sun_color,ambient,hemi_color,fog_color;};
struct EnvironmentType {CEnvDescriptorMixer CurrentEnv;} environment={};
struct Persistent {Menu* m_pMainMenu;EnvironmentType& Environment(){return environment;}bool OnRenderPPUI_query(){return false;}} persistent={&menu};
Persistent* g_pGamePersistent=&persistent;
struct ConsoleType {bool bVisible;} console={false};ConsoleType* Console=&console;
struct EngineDevice {void* m_hWnd;unsigned dwWidth,dwHeight,dwFrame,dwPrecacheFrame;float fTimeGlobal,fFOV;
    struct {float m[4][4];} mFullTransform;struct {float x,y,z;} vCameraPosition;int b_is_Active;bool paused;bool Paused(){return paused;}} Device={};
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
WeatherVector ps_r__Tree_Wave={.1f,.01f,.11f,0};
typedef uint32_t u32;
#define CopyMemory memcpy
#define R_ASSERT2(condition,message) Require((condition),(message))
void Msg(const char*,...){}
double KichR4_NativeQueueTime(){return 1.5;}
void KichR4_NativePostprocess(float* output){memset(output,0,24*sizeof(float));}
bool KichR4_NativePresent(const KichR4GameFrame& input){++native_presented;native_captured=input;return true;}
void KichR4_CaptureLighting(const float*,KichR4LightingFrame& out,bool r1_lights_enabled,const float* camera_vp){Require(camera_vp && !memcmp(camera_vp,Device.mFullTransform.m,64),"both engine frame adapters provide the current camera for light culling");out.count=0;out.r1_lights_requested=out.r1_lights_applied=r1_lights_enabled?1u:0u;}
void KichR4_GamePrepare(const KichR4GameFrame& in){++prepared;captured=in;}
// PRODUCTION_FUNCTION
int main(){try {
    KichR4_GamePrepareEngineFrame();Require(!prepared,"unarmed engine does not touch bridge input");armed=true;
    Device.m_hWnd=reinterpret_cast<void*>(1);Device.dwWidth=1280;Device.dwHeight=720;Device.dwFrame=123;Device.b_is_Active=1;Device.fFOV=67.5f;
    for(unsigned i=0;i<16;++i)(&Device.mFullTransform.m[0][0])[i]=static_cast<float>(i)+.25f;
    Device.vCameraPosition={1.f,2.f,3.f};key=true;KichR4_GamePrepareEngineFrame();
#if defined(DEDICATED_SERVER) || defined(_EDITOR)
    Require(!prepared,"dedicated and editor builds skip experimental game input");
    (void)&GetAsyncKeyState;(void)g_pGameLevel;(void)g_pGamePersistent;(void)Console;(void)g_loading_events;
#else
    Require(prepared==1 && captured.allowed && captured.active && captured.toggle_down,"playable input and F8 sampled");
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
    KichR4_NativePresentEngineFrame(false);
    Require(native_presented==1 && !memcmp(native_captured.tree_wave,captured.tree_wave,16) && !memcmp(native_captured.tree_wind,captured.tree_wind,16) && native_captured.tree_sbc==captured.tree_sbc,"native presenter and prepared path share exact tree frame parameters");
    Require(!memcmp(native_captured.geometry_lod,captured.geometry_lod,sizeof(captured.geometry_lod)),"native and prepared frame paths share LOD policy");
    Device.fTimeGlobal=5;ps_r__Tree_w_amp=.5f;KichR4_NativePresentEngineFrame(true);
    Require(native_presented==2 && fabsf(native_captured.tree_wind[2]+.5f)<1.e-6f && native_captured.tree_wave[3]!=captured.tree_wave[3],"live tree time and settings are resampled without per-frame geometry generation");
    for(unsigned blocked=0;blocked<7;++blocked) {
        level.bReady=true;level.view=&entity;menu.active=false;console.bVisible=false;Device.dwPrecacheFrame=0;Device.paused=false;g_loading_events.clear();
        switch(blocked) {case 0:level.bReady=false;break;case 1:level.view=NULL;break;case 2:menu.active=true;break;case 3:console.bVisible=true;break;
            case 4:Device.dwPrecacheFrame=1;break;case 5:g_loading_events.push_back(1);break;case 6:Device.paused=true;break;}
        KichR4_GamePrepareEngineFrame();Require(!captured.allowed,"loading/console/menu/no player camera prevents R4 output");
    }
    g_loading_events.clear();Device.b_is_Active=0;KichR4_GamePrepareEngineFrame();Require(!captured.active,"inactive app forwarded");
#endif
    printf("B4.3a production engine camera/gating seam: %d checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
