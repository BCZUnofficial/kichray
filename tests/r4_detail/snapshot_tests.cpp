#include "../../xr_3da/RenderSnapshot.h"
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <stdio.h>
#include <string.h>
unsigned checks=0;
void Require(bool good,const char* why){++checks;if(!good)throw std::runtime_error(why);}
std::string Read(const char* path){std::ifstream file(path);std::ostringstream out;out<<file.rdbuf();return out.str();}
int main(int argc,char** argv){try{Require(argc==2,"output directory");
    RenderSnapshotInfo info={};info.frame=77;info.width=1920;info.height=1080;info.position[0]=123.125f;info.fov=75;
    strcpy(info.renderer,"R4");strcpy(info.api,"D3D11");strcpy(info.scene,"GAME");strcpy(info.level,"l01_escape");strncpy(info.directory,argv[1],sizeof(info.directory)-1);
    RenderSnapshot_Begin(info);Require(!g_render_snapshot_active && !RenderSnapshot_Wanted(),"ordinary frame does not capture");
    Require(RenderSnapshot_Request() && RenderSnapshot_Request(),"two requests queued independently");
    RenderSnapshot_Begin(info);Require(g_render_snapshot_active,"begins at complete frame boundary");
    RenderSnapshot_Pass("GBUFFER");RenderSnapshot_Context("DETAIL","mesh_0","details\\set");RenderSnapshot_Skin("NONE");RenderSnapshot_Texture(0,"grass");
    RenderSnapshot_ShaderStage(true,"VSDetail");RenderSnapshot_ShaderStage(false,"PSGeometry");
    RenderSnapshotDraw draw={};draw.struct_size=sizeof(draw);draw.submitted=1;draw.topology=4;draw.index_count=6;draw.instance_count=7;
    RenderSnapshot_Draw(&draw);RenderSnapshot_Value("DETAIL_RENDERER","legacy_capture_vertices",0);
    RenderSnapshot_Note("CULLED","STATIC","mesh_1","FRUSTUM");RenderSnapshot_End("COMPLETE");
    const std::string path=RenderSnapshot_LastPath(),first=Read(path.c_str());Require(!g_render_snapshot_active,"stops after exactly one frame");
    Require(first.find("frame=77")!=std::string::npos && first.find("triangles_submitted=14")!=std::string::npos,"identified frame, instanced triangle count");
    Require(first.find("category=DETAIL")!=std::string::npos && first.find("vertex_shader=VSDetail")!=std::string::npos && first.find("texture0=grass")!=std::string::npos,"material and resource provenance");
    Require(first.find("status=COMPLETE")!=std::string::npos && first.find("visible_objects=UNKNOWN")!=std::string::npos && first.find("gpu_ms=NOT_INSTRUMENTED")!=std::string::npos,"does not invent visibility or GPU timing");
    RenderSnapshot_Draw(&draw);Require(Read(path.c_str())==first,"ordinary frames cannot append");
    info.frame=78;strcpy(info.renderer,"R2");strcpy(info.api,"D3D9");strcpy(info.scene,"MENU");RenderSnapshot_Begin(info);
    RenderSnapshot_End("COMPLETE");Require(std::string(RenderSnapshot_LastPath())!=path && Read(path.c_str())==first,"exclusive filename never overwrites prior snapshot");
    const std::string second=Read(RenderSnapshot_LastPath());Require(second.find("frame=78")!=std::string::npos && second.find("renderer=R2")!=std::string::npos && second.find("scene=MENU")!=std::string::npos,"same schema for menu and R2");
    Require(!RenderSnapshot_Wanted(),"queued requests consumed one per frame");
    RenderSnapshot_Request();RenderSnapshot_Begin(info);for(unsigned i=0;i<40000;++i)RenderSnapshot_Draw(&draw);RenderSnapshot_End("COMPLETE");
    const std::string overflow=Read(RenderSnapshot_LastPath());Require(overflow.find("status=INCOMPLETE")!=std::string::npos && overflow.find("overflow=1")!=std::string::npos && overflow.size()<9u*1024u*1024u,"bounded capture reports overflow");
    RenderSnapshot_Request();RenderSnapshot_Begin(info);draw.submitted=0;RenderSnapshot_Draw(&draw);RenderSnapshot_End("COMPLETE");
    Require(Read(RenderSnapshot_LastPath()).find("status=INCOMPLETE")!=std::string::npos,"failed submission never reports a complete capture");
    strcpy(info.directory,"/does/not/exist/kich-snapshot");RenderSnapshot_Request();RenderSnapshot_Begin(info);RenderSnapshot_End("COMPLETE");
    Require(!g_render_snapshot_active && std::string(RenderSnapshot_LastPath()).find("ERROR:")==0,"I/O failure does not retain capture or throw into renderer");
    printf("G3a-S real snapshot recorder: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
