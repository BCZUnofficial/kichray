#include "../../xr_3da/KichR4RendererContract.h"
#include "../../xr_3da/KichR4BuildIdentity.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks=0,get_failure=0,header_failure=0,device_creates=0,devices=0,frames=0,resets=0,presents=0;
bool missing_export=false;
unsigned getter_calls=0;
void Require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>struct Stub;
template<class R,class... A>struct Stub<R (KICH_R4_CALL *)(A...)> {
    static R KICH_R4_CALL Call(A...){return R();}
};
int32_t KICH_R4_CALL Create(KichR4Handle* out){++device_creates;++devices;*out=77;return 0;}
int32_t KICH_R4_CALL Begin(KichR4Handle h){if(h!=77 || !devices || frames)return KICH_R4_E_STATE;frames=1;return 0;}
int32_t KICH_R4_CALL Present(KichR4Handle h,uint32_t){if(h!=77 || !frames)return KICH_R4_E_STATE;frames=0;++presents;return 0;}
int32_t KICH_R4_CALL Reset(KichR4Handle h,KichR4ResourceStats*,uint32_t){if(h!=77 || frames)return KICH_R4_E_STATE;++resets;return 0;}
int32_t KICH_R4_CALL Destroy(KichR4Handle h,KichR4GeometryReport*,uint32_t){if(h!=77 || !devices)return KICH_R4_E_STATE;--devices;return 0;}
#define BIND(field) a.field=&Stub<decltype(a.field)>::Call
#define HEADER(type,version) type a={};a.struct_size=sizeof(a);a.abi_version=version;a.pointer_size=sizeof(void*);++getter_calls
#define FINISH() if(header_failure)a.flags=8;if(get_failure)return KICH_R4_E_UNSUPPORTED;*out=a;return 0
}
extern "C" uint32_t KICH_R4_CALL KichR4_GetBridgeVersion(){return KICH_R4_BRIDGE_ABI_VERSION;}
extern "C" int32_t KICH_R4_CALL KichR4_GetFrameAPI(uint32_t,KichR4FrameAPI* out,uint32_t){
    HEADER(KichR4FrameAPI,KICH_R4_FRAME_ABI);
    a.create_device=Create;BIND(destroy_device);BIND(create_target);BIND(destroy_target);BIND(resize);
    a.begin_frame=Begin;BIND(clear);a.present=Present;BIND(get_stats);FINISH();
}
extern "C" int32_t KICH_R4_CALL KichR4_GetResourceAPI(uint32_t,KichR4ResourceAPI* out,uint32_t){
    HEADER(KichR4ResourceAPI,KICH_R4_RESOURCE_ABI);
    BIND(create_device);BIND(destroy_device);BIND(create_texture);BIND(create_buffer);BIND(compile_shader);
    BIND(get_info);BIND(get_binding);BIND(get_variable);BIND(release_resource);BIND(get_stats);FINISH();
}
extern "C" int32_t KICH_R4_CALL KichR4_GetGeometryAPI(uint32_t,KichR4GeometryAPI* out,uint32_t){
    HEADER(KichR4GeometryAPI,KICH_R4_GEOMETRY_ABI);
    BIND(create_pipeline);BIND(update_buffer);BIND(draw_indexed);BIND(get_stats);a.destroy_device=Destroy;FINISH();
}
extern "C" int32_t KICH_R4_CALL KichR4_GetLevelAPI(uint32_t,KichR4LevelAPI* out,uint32_t){
    HEADER(KichR4LevelAPI,KICH_R4_LEVEL_ABI);
    BIND(create_texture_dds);BIND(create_pipeline);BIND(draw_textured);FINISH();
}
extern "C" int32_t KICH_R4_CALL KichR4_GetStreamAPI(uint32_t,KichR4StreamAPI* out,uint32_t){
    HEADER(KichR4StreamAPI,KICH_R4_STREAM_ABI);BIND(create_pipeline);BIND(draw);FINISH();
}
extern "C" int32_t KICH_R4_CALL KichR4_GetDeferredAPI(uint32_t,KichR4DeferredAPI* out,uint32_t){
    HEADER(KichR4DeferredAPI,KICH_R4_DEFERRED_ABI);BIND(create_pipeline);BIND(begin_pass);BIND(draw);FINISH();
}
extern "C" int32_t KICH_R4_CALL KichR4_GetNativeToolsAPI(uint32_t,KichR4NativeToolsAPI* out,uint32_t){
    HEADER(KichR4NativeToolsAPI,KICH_R4_NATIVE_TOOLS_ABI);
    BIND(read_pixels);BIND(profile_mark);BIND(get_profile);a.reset_resources=Reset;BIND(set_snapshot_sink);BIND(upload_bgra8);FINISH();
}
extern "C" int32_t KICH_R4_CALL KichR4_GetDetailAPI(uint32_t,KichR4DetailAPI* out,uint32_t){
    HEADER(KichR4DetailAPI,KICH_R4_DETAIL_ABI);BIND(create_pipeline);BIND(create_instances);BIND(upload_instances);BIND(draw);FINISH();
}
extern "C" int32_t KICH_R4_CALL KichR4_GetModelAPI(uint32_t,KichR4ModelAPI* out,uint32_t){
    HEADER(KichR4ModelAPI,KICH_R4_MODEL_ABI);BIND(create_pipeline);BIND(create_pose);BIND(upload_pose);BIND(draw);FINISH();
}
#undef BIND
#undef HEADER
#undef FINISH

namespace {
int loader_mode=0;
std::vector<std::string> messages;
struct Native {void* module;KichR4RendererAPI api;} native={};
void Write(const char* format,...){char text[2048];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);messages.push_back(text);}
int32_t KICH_R4_CALL LoadAPI(uint32_t abi,KichR4RendererAPI* out,uint32_t bytes){
    int32_t hr=KichR4_GetRendererAPI(abi,out,bytes);
    if(hr)return hr;
    if(loader_mode==1)out->build.source_id[0]='a';
    if(loader_mode==2)out->build.shader_id[0]='0';
    if(loader_mode==3)out->frame.present=NULL;
    if(loader_mode==4)return KICH_R4_E_UNSUPPORTED;
    return hr;
}
KichR4GetRendererAPIFn GetProcAddress(void*,const char* name){
    Require(std::string(name)==KICH_R4_EXPORT_RENDERER,"loader uses single G1 entry point");
    return missing_export?NULL:LoadAPI;
}
// Generated directly from the production presenter, not a copy of its loader.
#include "loader_under_test.h"

void Adapter(){
    KichR4RendererAPI a={};
    Require(KichR4_GetRendererAPI(KICH_R4_RENDERER_ABI,&a,sizeof(a))==0,"provider negotiates real service tables");
    Require(!KichR4_ValidateRendererAPI(&a) && !device_creates && getter_calls==9,"negotiation creates no device");
    const KichR4RendererAPI saved=a;
    Require(KichR4_GetRendererAPI(0,&a,sizeof(a))<0 && !memcmp(&a,&saved,sizeof(a)),"wrong version preserves output");
    Require(KichR4_GetRendererAPI(KICH_R4_RENDERER_ABI,&a,sizeof(a)-1)<0 && !memcmp(&a,&saved,sizeof(a)),"short output preserved");
    Require(KichR4_GetRendererAPI(KICH_R4_RENDERER_ABI,NULL,sizeof(a))<0,"null output rejected");
    get_failure=1;Require(KichR4_GetRendererAPI(KICH_R4_RENDERER_ABI,&a,sizeof(a))<0 && !memcmp(&a,&saved,sizeof(a)),"old service failure never publishes partial table");get_failure=0;
    header_failure=1;Require(KichR4_GetRendererAPI(KICH_R4_RENDERER_ABI,&a,sizeof(a))<0 && !memcmp(&a,&saved,sizeof(a)),"invalid old table rejected atomically");header_failure=0;
#define BAD(field,value) a=saved;a.field=value;Require(KichR4_ValidateRendererAPI(&a)!=NULL,"reject " #field)
    BAD(struct_size,0);BAD(abi_version,0);BAD(pointer_size,1);BAD(flags,0);BAD(flags,8);
    BAD(build.struct_size,0);BAD(build.pointer_size,1);BAD(build.dirty,3);BAD(build.source_id[0],'x');
    a=saved;memset(a.build.revision,'x',sizeof(a.build.revision));Require(KichR4_ValidateRendererAPI(&a)!=NULL,"unterminated identity rejected");
#define TABLE(member) BAD(member.struct_size,0);BAD(member.abi_version,0);BAD(member.pointer_size,1);BAD(member.flags,1)
    TABLE(frame);TABLE(resources);TABLE(geometry);TABLE(level);TABLE(stream);TABLE(deferred);TABLE(tools);TABLE(details);TABLE(models);
#undef TABLE
    BAD(frame.create_device,NULL);BAD(frame.destroy_device,NULL);BAD(frame.create_target,NULL);BAD(frame.destroy_target,NULL);
    BAD(frame.resize,NULL);BAD(frame.begin_frame,NULL);BAD(frame.clear,NULL);BAD(frame.present,NULL);BAD(frame.get_stats,NULL);
    BAD(resources.create_device,NULL);BAD(resources.destroy_device,NULL);BAD(resources.create_texture,NULL);BAD(resources.create_buffer,NULL);
    BAD(resources.compile_shader,NULL);BAD(resources.get_info,NULL);BAD(resources.get_binding,NULL);BAD(resources.get_variable,NULL);
    BAD(resources.release_resource,NULL);BAD(resources.get_stats,NULL);
    BAD(geometry.create_pipeline,NULL);BAD(geometry.update_buffer,NULL);BAD(geometry.draw_indexed,NULL);BAD(geometry.get_stats,NULL);BAD(geometry.destroy_device,NULL);
    BAD(level.create_texture_dds,NULL);BAD(level.create_pipeline,NULL);BAD(level.draw_textured,NULL);
    BAD(stream.create_pipeline,NULL);BAD(stream.draw,NULL);BAD(deferred.create_pipeline,NULL);BAD(deferred.begin_pass,NULL);BAD(deferred.draw,NULL);
    BAD(tools.read_pixels,NULL);BAD(tools.profile_mark,NULL);BAD(tools.get_profile,NULL);BAD(tools.reset_resources,NULL);
    BAD(tools.set_snapshot_sink,NULL);BAD(details.create_pipeline,NULL);BAD(details.create_instances,NULL);BAD(details.upload_instances,NULL);BAD(details.draw,NULL);BAD(models.create_pipeline,NULL);BAD(models.create_pose,NULL);BAD(models.upload_pose,NULL);BAD(models.draw,NULL);
#undef BAD
    a=saved;
    KichR4Handle device=0;Require(a.frame.create_device(&device)==0 && device==77,"frame owns the single device");
    Require(a.frame.begin_frame(device)==0 && a.tools.reset_resources(device,NULL,0)<0,"scene reset refuses an open frame");
    Require(a.frame.present(device,1)==0 && a.tools.reset_resources(device,NULL,0)==0,"scene reset after Present retains owner");
    Require(a.frame.begin_frame(device)==0 && a.frame.present(device,1)==0,"same device presents after scene reset");
    Require(a.geometry.destroy_device(device,NULL,0)==0 && !devices && resets==1 && presents==2,"one final geometry owner destroys device");
}
void Loader(){
    const int initial_creates=device_creates;
    Require(ReadAPIs(),"actual EXE loader accepts matching source builds");
    const KichR4RendererAPI saved=native.api;
    for(loader_mode=1;loader_mode<=4;++loader_mode){
        Require(!ReadAPIs(),"actual EXE loader rejects mismatch/malformed table/getter failure");
        Require(!memcmp(&native.api,&saved,sizeof(saved)) && device_creates==initial_creates,"failed negotiation publishes no table or device");
    }
    loader_mode=0;missing_export=true;Require(!ReadAPIs(),"old DLL rejected with explicit missing-entry diagnosis");missing_export=false;
    bool saw_mismatch=false,saw_missing=false,saw_exe=false,saw_dll=false;
    for(const auto& m:messages){saw_mismatch|=m.find("identity mismatch")!=std::string::npos;saw_missing|=m.find("missing KichR4_GetRendererAPI")!=std::string::npos;
        saw_exe|=m.find("XR_3DA.exe revision=")!=std::string::npos;saw_dll|=m.find("xrRender_R4.dll revision=")!=std::string::npos;}
    Require(saw_mismatch && saw_missing && saw_exe && saw_dll,"both module IDs and actionable failures are logged");
    KichR4BuildInfo archive=KichR4_LocalBuildInfo(),git=archive;
    strcpy(archive.revision,"archive");archive.dirty=2;strcpy(git.revision,"0123456789abcdef0123456789abcdef01234567");git.dirty=0;
    Require(KichR4_BuildsMatch(&archive,&git),"archive and Git match by source content, not revision/date");
}
}
int main(){try{Adapter();Loader();printf("G0/G1 provider, production EXE loader and ownership: %d checks passed\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
