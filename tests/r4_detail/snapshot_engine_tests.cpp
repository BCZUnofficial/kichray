#include "snapshot_engine_fake.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
SnapshotDevice Device;
SnapshotGPU gpu;FakeD3D d3d;SnapshotHW HW={&d3d,&gpu,0};CResourceManager resources;
SnapshotFlags psHUD_Flags,psDeviceFlags;SnapshotConsole console;SnapshotConsole* Console=&console;
IRender_interface renderer;IRender_interface* Render=&renderer;SnapshotPersistent persistent;SnapshotPersistent* g_pGamePersistent=&persistent;
SnapshotLevel level;SnapshotLevel* g_pGameLevel=&level;SnapshotCache RCache;SnapshotFS FS;std::vector<int> g_loading_events;bool native=false;
#include "snapshot_engine_under_test.cpp"
unsigned checks=0;
void Require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
std::string Read(){std::ifstream in(RenderSnapshot_LastPath());std::ostringstream out;out<<in.rdbuf();return out.str();}
int main(int argc,char** argv){try{Require(argc==2,"directory");FS.directory=argv[1];Device.Resources=&resources;
    RenderSnapshot_EngineBegin();Require(!g_render_snapshot_active,"idle engine hook is inert");RenderSnapshot_Request();RenderSnapshot_EngineBegin();Require(g_render_snapshot_active,"request starts at device Begin");
    RenderSnapshot_Pass("GBUFFER");RenderSnapshot_Context("DETAIL","mesh","detail");RenderSnapshot_D3D9Draw(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),20,4,3,0,1,true,0);
    RenderSnapshot_EngineEnd();const auto first=Read();Require(first.find("renderer=R2")!=std::string::npos && first.find("adapter=test GPU")!=std::string::npos,"actual engine metadata");
    Require(first.find("game_time_seconds=32000")!=std::string::npos && first.find("console_visible=1")!=std::string::npos,"game time and console retained");
    Require(first.find("vertex_shader=bound_vs")!=std::string::npos && first.find("ALPHAREF=200")!=std::string::npos && first.find("texture0=grass")!=std::string::npos,"queries real backend state/resources, not simulated capture state");
    Require(gpu.vs.refs==0 && gpu.ps.refs==0 && gpu.texture.refs==0,"GetShader/GetTexture COM references released");
    ++Device.dwFrame;gpu.fail=true;RenderSnapshot_Request();RenderSnapshot_EngineBegin();RenderSnapshot_D3D9Draw(NULL,NULL,20,5,0,0,2,false,0);RenderSnapshot_EngineEnd();
    Require(Read().find("ZENABLE=NOT_AVAILABLE")!=std::string::npos && Read().find("s1:NOT_AVAILABLE")!=std::string::npos,"failed state queries never invent zeros");
    native=true;persistent.menu=true;++Device.dwFrame;RenderSnapshot_Request();RenderSnapshot_EngineBegin();RenderSnapshot_EngineEnd();
    Require(Read().find("renderer=R4")!=std::string::npos && Read().find("scene=MENU")!=std::string::npos,"native/menu metadata uses same recorder");
    printf("G3a-S production engine/D3D9 adapter: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
