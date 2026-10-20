/* The runner inserts the unchanged production bridge function at the marker. */
#include "../../xr_3da/KichR4Game.h"
#include <vector>
#include <string.h>
#include <stdexcept>
#include <stdio.h>
namespace {
int checks=0,prepared=0;bool armed=false,key=false;
KichR4GameFrame captured={};
struct Entity {} entity;
struct Level {bool bReady;Entity* view;Entity* CurrentViewEntity(){return view;}} level={true,&entity};
Level* g_pGameLevel=&level;
struct Menu {bool active;bool IsActive(){return active;}} menu={false};
struct Persistent {Menu* m_pMainMenu;} persistent={&menu};
Persistent* g_pGamePersistent=&persistent;
struct ConsoleType {bool bVisible;} console={false};ConsoleType* Console=&console;
struct EngineDevice {void* m_hWnd;unsigned dwWidth,dwHeight,dwFrame,dwPrecacheFrame;
    struct {float m[4][4];} mFullTransform;struct {float x,y,z;} vCameraPosition;int b_is_Active;bool paused;bool Paused(){return paused;}} Device={};
std::vector<int> g_loading_events;
const int FALSE=0,VK_F8=119;
short GetAsyncKeyState(int which){return which==VK_F8 && key?static_cast<short>(0x8000):0;}
void Require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
}
bool KichR4_GameWindowed(){return armed;}
void KichR4_GamePrepare(const KichR4GameFrame& in){++prepared;captured=in;}
// PRODUCTION_FUNCTION
int main(){try {
    KichR4_GamePrepareEngineFrame();Require(!prepared,"unarmed engine does not touch bridge input");armed=true;
    Device.m_hWnd=reinterpret_cast<void*>(1);Device.dwWidth=1280;Device.dwHeight=720;Device.dwFrame=123;Device.b_is_Active=1;
    for(unsigned i=0;i<16;++i)(&Device.mFullTransform.m[0][0])[i]=static_cast<float>(i)+.25f;
    Device.vCameraPosition={1.f,2.f,3.f};key=true;KichR4_GamePrepareEngineFrame();
#if defined(DEDICATED_SERVER) || defined(_EDITOR)
    Require(!prepared,"dedicated and editor builds skip experimental game input");
    (void)&GetAsyncKeyState;(void)g_pGameLevel;(void)g_pGamePersistent;(void)Console;(void)g_loading_events;
#else
    Require(prepared==1 && captured.allowed && captured.active && captured.toggle_down,"playable input and F8 sampled");
    Require(captured.width==1280 && captured.height==720 && captured.engine_frame==123 && captured.window==Device.m_hWnd,"game window and frame dimensions forwarded");
    Require(!memcmp(captured.view_projection,Device.mFullTransform.m,64) && captured.position[0]==1 && captured.position[2]==3,"camera matrix passed byte-for-byte without a viewer camera");
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
