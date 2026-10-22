/* Native EXE presentation owner: no engine PCH, D3D9 device, or fallback frame. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <fenv.h>
#include <stdio.h>
#include <stdarg.h>
#include <mutex>
#include <memory>
#include "KichR4Native.h"
#include "KichR4NativeCPU.h"
#include "KichR4GameSession.h"
#include "KichR4RendererContract.h"
#include "KichR4BuildIdentity.h"
#include "RenderSnapshot.h"
#include "KichR4Telemetry.h"
bool KichR4_NativeTerrain(const char* shader,char (*layers)[128]);
bool KichR4_NativeMaterialState(const char*,const char*,kich_r4_capture::Style&);
bool KichR4_NativeNeedScreenshot();
void KichR4_NativeWriteScreenshot(const unsigned char*,uint32_t,uint32_t);
namespace {
struct Runtime {void* texture;std::string key;std::vector<unsigned char> pixels;uint32_t width,height;uint64_t revision;Runtime():texture(NULL),width(0),height(0),revision(0){};};
std::mutex runtime_mutex;
std::map<void*,Runtime> runtime;
std::map<std::string,Runtime> retired;
uint64_t texture_serial=0;
struct Uploaded {KichR4Handle handle;uint64_t revision,last_engine_frame,last_present_attempt;Uploaded():handle(0),revision(0),last_engine_frame(0),last_present_attempt(0){}};
struct Context {
    KichR4FrameSession frame;KichR4LevelSession level;KichR4StreamSession stream;KichR4DeferredSession deferred;
    std::map<std::string,Uploaded> uploads;
    bool loaded;std::string name;
    Context():loaded(false){memset(&frame,0,sizeof(frame));}
};
struct NativeState {
    HMODULE module;KichR4RendererAPI api;KichR4LevelFiles files;KichR4TelemetryAPI telemetry;
    KichR4TelemetryContext pending_timings[16];unsigned pending_timing_count;
    uint64_t current_engine_frame,device_generation,level_generation,present_frame,successful_presents,runtime_uploads,runtime_reuses,runtime_releases,runtime_upload_bytes,telemetry_cpu_ticks;
    std::unique_ptr<Context> context;
    void (*log)(const char*);void* window;uint32_t width,height,color,debug,interval;bool debug_key;std::string desired;
    NativeState():module(NULL),log(NULL),window(NULL),width(0),height(0),color(0xff000000),debug(0),interval(1),debug_key(false){memset(&api,0,sizeof(api));memset(&files,0,sizeof(files));memset(&telemetry,0,sizeof(telemetry));pending_timing_count=0;current_engine_frame=0;device_generation=level_generation=present_frame=successful_presents=runtime_uploads=runtime_reuses=runtime_releases=runtime_upload_bytes=telemetry_cpu_ticks=0;}
} native;
#if defined(__GNUC__)
__attribute__((format(gnu_printf,1,2)))
#endif
void Write(const char* format,...){if(!native.log)return;char text[1024];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);text[sizeof(text)-1]=0;native.log(text);}
void LogBuild(const char* module,const KichR4BuildInfo& b){
    Write("[R4 G0] %s revision=%s dirty=%u source_sha256=%s shaders_sha256=%s compiled=%s config=%s compiler=%u pointer_size=%u",
        module,b.revision,b.dirty,b.source_id,b.shader_id,b.compiled,b.configuration,b.compiler_version,b.pointer_size);
}
void ReadTelemetryAPI(){
    memset(&native.telemetry,0,sizeof(native.telemetry));
    const KichR4GetTelemetryAPIFn get=reinterpret_cast<KichR4GetTelemetryAPIFn>(GetProcAddress(native.module,KICH_R4_EXPORT_TELEMETRY));
    KichR4TelemetryAPI api={};
    if(get && get(KICH_R4_TELEMETRY_ABI,&api,sizeof(api))==0 && api.struct_size==sizeof(api) && api.abi_version==KICH_R4_TELEMETRY_ABI && api.pointer_size==sizeof(void*) && !api.flags && api.set_capture && api.poll_timing)native.telemetry=api;
    else Write("[R4 G3b.1] optional telemetry API unavailable; rendering contract remains valid");
}
void KICH_R4_CALL TelemetryRecord(const KichR4TelemetryRecord* r){
    if(!r || r->struct_size!=sizeof(*r) || r->reserved)return;
    LARGE_INTEGER start,end;QueryPerformanceCounter(&start);
    RenderSnapshot_Record(r->category,r->id,r->key,r->value);
    QueryPerformanceCounter(&end);native.telemetry_cpu_ticks+=static_cast<uint64_t>(end.QuadPart-start.QuadPart);
}
void ForgetTiming(uint64_t id){for(unsigned i=0;i<native.pending_timing_count;++i)if(native.pending_timings[i].capture_id==id){--native.pending_timing_count;memmove(native.pending_timings+i,native.pending_timings+i+1,(native.pending_timing_count-i)*sizeof(native.pending_timings[0]));return;}}
void CancelTimings(const char* status){
    for(unsigned i=0;i<native.pending_timing_count;++i){const KichR4TelemetryContext& p=native.pending_timings[i];RenderSnapshot_GPUTiming(p.capture_id,p.engine_frame,p.render_frame,status,NULL,0);}
    native.pending_timing_count=0;
}
void PollTimings(){
    if(!native.context || !native.telemetry.poll_timing)return;
    KichR4TelemetryTiming result={};const int32_t hr=native.telemetry.poll_timing(native.context->frame.device,&result,sizeof(result));
    if(hr<0){CancelTimings("DEVICE_RESET");return;}
    if(!result.status)return;
    unsigned pending=0;for(;pending<native.pending_timing_count && native.pending_timings[pending].capture_id!=result.capture_id;++pending){}
    if(pending==native.pending_timing_count || native.pending_timings[pending].engine_frame!=result.engine_frame || native.pending_timings[pending].render_frame!=result.render_frame)return;
    const char* status=result.status==KICH_R4_TIMING_READY?"READY":result.status==KICH_R4_TIMING_DISJOINT?"DISJOINT":result.status==KICH_R4_TIMING_TIMEOUT?"TIMEOUT":result.status==KICH_R4_TIMING_ABORTED?"CANCELLED":"UNSUPPORTED";
    RenderSnapshot_GPUTiming(result.capture_id,result.engine_frame,result.render_frame,status,result.milliseconds,result.valid_mask);
    ForgetTiming(result.capture_id);
}
void BeginTelemetry(const KichR4GameFrame& input,bool menu){
    KichR4TelemetryContext capture={};capture.struct_size=sizeof(capture);
    capture.flags=(RenderSnapshot_Detailed()?KICH_R4_TELEMETRY_DETAILED:0)|(menu?KICH_R4_TELEMETRY_MENU:0);
    native.current_engine_frame=input.engine_frame;capture.capture_id=g_render_snapshot_active?RenderSnapshot_CaptureID():0;capture.engine_frame=input.engine_frame;
    capture.render_frame=g_render_snapshot_active?RenderSnapshot_RenderFrameID():0;capture.present_frame=++native.present_frame;
    capture.device_generation=native.device_generation;capture.level_generation=native.level_generation;native.telemetry_cpu_ticks=0;
    if(capture.capture_id){
        if(native.pending_timing_count>=16){const KichR4TelemetryContext first=native.pending_timings[0];RenderSnapshot_GPUTiming(first.capture_id,first.engine_frame,first.render_frame,"TIMEOUT",NULL,0);ForgetTiming(first.capture_id);}
        RenderSnapshot_GPUTiming(capture.capture_id,capture.engine_frame,capture.render_frame,native.telemetry.set_capture?"NOT_READY":"UNSUPPORTED",NULL,menu?1:127);
        if(native.telemetry.set_capture)native.pending_timings[native.pending_timing_count++]=capture;
        RenderSnapshot_Text("GPU_TIMING","stage_mapping",menu?"0=MENU_RENDER_TOTAL;1..6=NOT_APPLICABLE":"0=RESOURCE_INIT_UPLOAD_SHADOWS;1=GBUFFER;2=LIGHTING;3=COMBINE;4=FORWARD_DISTORT_HUD;5=POSTPROCESS_UI;6=END_MARKER_GAP");
        RenderSnapshot_Text("GPU_TIMING","poll_policy","DONOTFLUSH; NO_WAIT; COMPLETION_TO_ORIGINAL_CAPTURE; MAX_120_RENDER_POLLS");
        RenderSnapshot_Text("GPU_TIMING","paused_render","PENDING_UNTIL_RENDER_RESUMES_OR_CONTEXT_CLOSES; NO_SPIN_WAIT");
        RenderSnapshot_Text("FRAME_PASSPORT","backend_telemetry_api",native.telemetry.set_capture?"AVAILABLE_ABI_1":"UNSUPPORTED");
        RenderSnapshot_Text("FRAME_PASSPORT","r4_dll_revision",native.api.build.revision);RenderSnapshot_Text("FRAME_PASSPORT","r4_dll_source_sha256",native.api.build.source_id);
        RenderSnapshot_Text("FRAME_PASSPORT","r4_dll_shader_sha256",native.api.build.shader_id);RenderSnapshot_Text("FRAME_PASSPORT","r4_dll_compiled",native.api.build.compiled);
        RenderSnapshot_U64("FRAME_PASSPORT","native_present_attempt_ordinal",capture.present_frame);RenderSnapshot_U64("FRAME_PASSPORT","native_device_generation",capture.device_generation);RenderSnapshot_U64("FRAME_PASSPORT","native_level_generation",capture.level_generation);
    }
    if(native.telemetry.set_capture){const int32_t hr=native.telemetry.set_capture(native.context->frame.device,&capture,RenderSnapshot_Detailed()?TelemetryRecord:NULL);
        if(hr<0 && capture.capture_id){RenderSnapshot_GPUTiming(capture.capture_id,capture.engine_frame,capture.render_frame,"UNSUPPORTED",NULL,0);ForgetTiming(capture.capture_id);}}
}
struct TelemetryGuard {
    KichR4Handle device;bool enabled;
    explicit TelemetryGuard(KichR4Handle value):device(value),enabled(native.telemetry.set_capture!=NULL){}
    void Reset(){if(enabled){native.telemetry.set_capture(device,NULL,NULL);enabled=false;}}
    ~TelemetryGuard(){Reset();}
};
bool ReadAPIs(){
    const KichR4BuildInfo exe=KichR4_LocalBuildInfo();LogBuild("XR_3DA.exe",exe);
    const KichR4GetRendererAPIFn get=reinterpret_cast<KichR4GetRendererAPIFn>(GetProcAddress(native.module,KICH_R4_EXPORT_RENDERER));
    if(!get){Write("[R4 G1] missing %s: rebuild/copy XR_3DA.exe and xrRender_R4.dll together",KICH_R4_EXPORT_RENDERER);return false;}
    KichR4RendererAPI api={};const int32_t hr=get(KICH_R4_RENDERER_ABI,&api,sizeof(api));
    if(hr!=KICH_R4_OK){Write("[R4 G1] renderer contract rejected: ABI=0x%08X bytes=%u HRESULT=0x%08X",
        KICH_R4_RENDERER_ABI,static_cast<uint32_t>(sizeof(api)),static_cast<uint32_t>(hr));return false;}
    const char* invalid=KichR4_ValidateRendererAPI(&api);
    if(invalid){Write("[R4 G1] invalid renderer contract: %s",invalid);return false;}
    LogBuild("xrRender_R4.dll",api.build);
    if(!KichR4_BuildsMatch(&exe,&api.build)){
        Write("[R4 G1] EXE/DLL source or shader identity mismatch; copy both files from the same source build");return false;}
    native.api=api;
    Write("[R4 G1] renderer ABI=0x%08X verified; transitional services; CPU capture remains until G3",api.abi_version);
    return true;
}

void StopContext(){if(!native.context)return;CancelTimings("CANCELLED");Context& c=*native.context;if(c.frame.device){KichR4GeometryReport report={};const int32_t hr=c.level.bridge.Stop(c.frame,&report);
    Write("[R4 native B5] cleanup: HRESULT=0x%08X resources=%u/%u live=%u payload=%llu views=%u/%u",static_cast<uint32_t>(hr),report.resources.created,report.resources.released,report.resources.live_resources,static_cast<unsigned long long>(report.resources.payload_bytes),report.frame.views_created,report.frame.views_released);}
    native.context.reset();
}
KichR4Handle OverrideTexture(void* context,const std::string& key,bool& handled){Context& c=*static_cast<Context*>(context);handled=false;
    if(key.compare(0,12,"r4_runtime__"))return 0;
    handled=true;
    std::lock_guard<std::mutex> lock(runtime_mutex);Runtime* source=NULL;
    for(std::map<void*,Runtime>::iterator i=runtime.begin();i!=runtime.end();++i)if(i->second.key==key){source=&i->second;break;}
    if(!source){const auto found=retired.find(key);if(found!=retired.end())source=&found->second;}
    if(!source)return c.level.checker;
    uint32_t w=source->width,h=source->height;uint64_t revision=source->revision;
    const unsigned char* pixels=source->texture?KichR4_CPUTexturePixels(source->texture,w,h,revision):source->pixels.data();if(!pixels || !w || !h)return c.level.checker;
    Uploaded& uploaded=c.uploads[key];if(uploaded.handle && uploaded.revision==revision){++native.runtime_reuses;
        if(RenderSnapshot_Detailed()){RenderSnapshot_Record("DYNAMIC_TEXTURE",key.c_str(),"action","REUSED_GPU_TEXTURE");RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"handle",uploaded.handle);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"revision",revision);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"last_upload_engine_frame",uploaded.last_engine_frame);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"last_upload_present_attempt",uploaded.last_present_attempt);}return uploaded.handle;}
    if(uint64_t(w)*h*4>32u*1024u*1024u)return c.level.checker;
    std::vector<unsigned char> dds(128+size_t(w)*h*4,0);KichR4LevelSession::Put32(dds,0,0x20534444);KichR4LevelSession::Put32(dds,4,124);KichR4LevelSession::Put32(dds,8,0x100f);
    KichR4LevelSession::Put32(dds,12,h);KichR4LevelSession::Put32(dds,16,w);KichR4LevelSession::Put32(dds,20,w*4);KichR4LevelSession::Put32(dds,76,32);KichR4LevelSession::Put32(dds,80,0x41);
    KichR4LevelSession::Put32(dds,88,32);KichR4LevelSession::Put32(dds,92,0xff0000);KichR4LevelSession::Put32(dds,96,0xff00);KichR4LevelSession::Put32(dds,100,0xff);KichR4LevelSession::Put32(dds,104,0xff000000);KichR4LevelSession::Put32(dds,108,0x1000);memcpy(dds.data()+128,pixels,size_t(w)*h*4);
    KichR4Handle next=0;const KichR4DdsDesc desc={sizeof(desc),16384,0};const int32_t hr=c.level.api.create_texture_dds(c.frame.device,&desc,dds.data(),static_cast<uint32_t>(dds.size()),&next);
    if(hr<0){c.frame.Record("native video/user texture upload",hr);return c.level.checker;}
    ++native.runtime_uploads;native.runtime_upload_bytes+=uint64_t(w)*h*4;uploaded.last_engine_frame=native.current_engine_frame;uploaded.last_present_attempt=native.present_frame;
    if(RenderSnapshot_Detailed()){RenderSnapshot_Record("DYNAMIC_TEXTURE",key.c_str(),"action","CPU_RGBA_UPLOAD_TO_IMMUTABLE_DDS_TEXTURE");RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"handle",next);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"revision",revision);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"width",w);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"height",h);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"upload_bytes",uint64_t(w)*h*4);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"last_upload_engine_frame",uploaded.last_engine_frame);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"last_upload_present_attempt",uploaded.last_present_attempt);}
    if(uploaded.handle){c.level.bridge.resources.release_resource(c.frame.device,uploaded.handle);++native.runtime_releases;}uploaded.handle=next;uploaded.revision=revision;return next;
}
void ConfigureContext(Context& c){c.frame.api=native.api.frame;c.level.bridge.resources=native.api.resources;c.level.bridge.api=native.api.geometry;
    c.level.api=native.api.level;c.level.files=native.files;c.level.log=native.log;c.stream.api=native.api.stream;c.deferred.api=native.api.deferred;c.deferred.native_mode=true;c.deferred.tools=native.api.tools;
    c.deferred.native_models.api=native.api.models;c.deferred.details.api=native.api.details;c.deferred.details.source=&KichR4_DetailFrame();
    c.level.keep_going=[](void*)->bool{KichR4_NativePumpLoading();return true;};
    c.level.texture_override=OverrideTexture;c.level.texture_context=&c;
}
bool CreatePlaceholders(Context& c){return c.level.Placeholder(c.frame,false,&c.level.white)==0 && c.level.Placeholder(c.frame,true,&c.level.checker)==0;}
bool CreateContext(){native.context.reset(new Context());Context& c=*native.context;ConfigureContext(c);
    if(c.frame.Start(native.window,native.width,native.height)<0)return false;
    ++native.device_generation;++native.level_generation;
    return CreatePlaceholders(c);
}
bool ResetContext(){CancelTimings("DEVICE_RESET");Context& previous=*native.context;
    std::unique_ptr<Context> next(new Context());next->frame=previous.frame;next->name=native.desired;ConfigureContext(*next);
    KichR4ResourceStats report={};const int32_t hr=native.api.tools.reset_resources(previous.frame.device,&report,sizeof(report));
    if(hr!=0){previous.frame.Record("native reset scene resources",hr<0?hr:KICH_R4_E_STATE);
        Write("[R4 native B5] scene reset failed: HRESULT=0x%08X",static_cast<uint32_t>(hr));return false;}
    Write("[R4 native B5] scene switch: %s -> %s; preserving device=%llu window target and %u presented frames; resources=%u/%u live=%u",
        previous.name.empty()?"<menu>":previous.name.c_str(),next->name.empty()?"<menu>":next->name.c_str(),
        static_cast<unsigned long long>(previous.frame.device),previous.frame.rendered,report.created,report.released,report.live_resources);
    native.context=std::move(next);++native.level_generation;
    return CreatePlaceholders(*native.context);
}
}
void KichR4_NativeTextureChanged(void* texture){std::lock_guard<std::mutex> lock(runtime_mutex);Runtime& r=runtime[texture];if(r.texture)return;r.texture=texture;char key[64];snprintf(key,sizeof(key),"r4_runtime__%llu",static_cast<unsigned long long>(++texture_serial));r.key=key;}
void KichR4_NativeForgetTexture(void* texture){std::lock_guard<std::mutex> lock(runtime_mutex);const auto found=runtime.find(texture);if(found==runtime.end())return;
    try{Runtime saved=std::move(found->second);const unsigned char* pixels=KichR4_CPUTexturePixels(texture,saved.width,saved.height,saved.revision);
        if(pixels && uint64_t(saved.width)*saved.height*4<=32u*1024u*1024u){saved.pixels.assign(pixels,pixels+size_t(saved.width)*saved.height*4);saved.texture=NULL;retired[saved.key]=std::move(saved);}}
    catch(...){ }runtime.erase(found);
}
const char* KichR4_NativeTextureName(void* texture){static thread_local std::string value;{std::lock_guard<std::mutex> lock(runtime_mutex);std::map<void*,Runtime>::const_iterator i=runtime.find(texture);if(i!=runtime.end()){value=i->second.key;return value.c_str();}}
    return KichR4_CPUTextureName(texture);
}
bool KichR4_NativeCreate(void* window,uint32_t width,uint32_t height,const KichR4LevelFiles& files,void (*log)(const char*)){native.log=log;native.window=window;native.width=width;native.height=height;native.files=files;
    try{wchar_t path[MAX_PATH];const DWORD n=GetModuleFileNameW(NULL,path,MAX_PATH);if(!n || n>=MAX_PATH)return false;wchar_t* leaf=wcsrchr(path,L'\\');if(!leaf || static_cast<size_t>(leaf-path)+18>=MAX_PATH)return false;wcscpy(leaf+1,L"xrRender_R4.dll");native.module=LoadLibraryW(path);
        if(!native.module || !ReadAPIs()){Write("[R4 native B5] matching xrRender_R4 DLL/API unavailable; Win32=%lu",GetLastError());KichR4_NativeDestroy();return false;}
        ReadTelemetryAPI();
        if(!CreateContext()){Write("[R4 native B5] device initialization failed at %s HRESULT=0x%08X",native.context->frame.step,static_cast<uint32_t>(native.context->frame.failure));KichR4_NativeDestroy();return false;}
        Write("[R4 native B5] device=DX11 from startup; legacy modules/devices/frames=0; menus/loading/UI native; F9 buffers; F8 requires a separate R2 launch");return true;
    }catch(const std::exception& e){Write("[R4 native B5] create exception: %s",e.what());KichR4_NativeDestroy();return false;}
}
bool KichR4_NativeResize(uint32_t w,uint32_t h){native.width=w;native.height=h;return native.context && native.context->frame.Resize(w,h)>=0;}
void KichR4_NativeLevel(const char* name){native.desired=name?name:"";}
void KichR4_NativeClear(uint32_t color){native.color=color;}
void KichR4_NativeSetInterval(bool vsync){native.interval=vsync?1:0;}
bool KichR4_NativeReadPixels(std::vector<unsigned char>& out,uint32_t& w,uint32_t& h){if(!native.context)return false;Context& c=*native.context;
    if(native.api.tools.read_pixels(c.frame.device,NULL,0,&w,&h)<0 || !w || !h || uint64_t(w)*h*4>256u*1024u*1024u)return false;
    out.resize(size_t(w)*h*4);
    return native.api.tools.read_pixels(c.frame.device,out.data(),static_cast<uint32_t>(out.size()),&w,&h)==0;
}
bool KichR4_NativePresent(const KichR4GameFrame& input){if(!native.context)return false;PollTimings();Context* context=native.context.get();
    struct SnapshotFailureGuard {bool complete;SnapshotFailureGuard():complete(false){}~SnapshotFailureGuard(){if(!complete && g_render_snapshot_active){RenderSnapshot_PresentID(native.successful_presents,false);RenderSnapshot_End("NATIVE_FRAME_FAILED");}}} snapshot_guard;
    if(g_render_snapshot_active){RenderSnapshot_Text("MEMORY","registered_gpu_payload_bytes","NOT_AVAILABLE");RenderSnapshot_Text("MEMORY","resource_stats_status","NOT_QUERIED");
        RenderSnapshot_Text("MEMORY","registered_gpu_payload_scope","DEVICE_RESOURCE_REGISTRY_PAYLOAD; NOT_VRAM_RESIDENCY");RenderSnapshot_Text("MEMORY","cpu_capture_bytes","NOT_AVAILABLE");
        RenderSnapshot_Text("MODEL_RENDERER","frame_status","NOT_SUBMITTED");
        context->deferred.details.SnapshotSummary(input.engine_frame,false);}
    try{if(context->name!=native.desired){if(!ResetContext())return false;context=native.context.get();}
        Context& c=*context;BeginTelemetry(input,c.name.empty() || !input.allowed);TelemetryGuard telemetry_guard(c.frame.device);
        LARGE_INTEGER start,end,submitted,frequency;QueryPerformanceCounter(&start);const uint32_t shadows_before=c.deferred.shadow_passes;QueryPerformanceFrequency(&frequency);
        if(!c.loaded && !c.name.empty() && input.allowed){if(c.level.Start(c.frame,c.name.c_str())!=0)return false;c.loaded=true;
            for(size_t i=0;i<c.level.scene.materials.size();++i){kich_r4_level::Material& m=c.level.scene.materials[i];char layers[4][128]={};if(KichR4_NativeTerrain(m.shader.c_str(),layers)){m.kind=1;for(unsigned j=0;j<4;++j)m.layers[j]=layers[j];}
                if(m.shader.find("water")!=std::string::npos)m.kind=2;
                if(!m.shader.empty()){kich_r4_capture::Style style;if(KichR4_NativeMaterialState(m.shader.c_str(),m.texture_list.c_str(),style)){
                    m.blend=style.blend;m.source=style.src;m.destination=style.dst;m.alpha_func=style.alpha_func;m.alpha_ref=style.alpha_ref;if(style.blend && m.kind!=2)m.kind=3;}}}
        }
        const kich_r4_capture::Batch* batch=KichR4_CapturedFrame();if(!batch || !batch->Ready()){Write("[R4 native B5] rejected CPU frame: %s",batch?batch->error.c_str():"queue unavailable");return false;}
        if(input.debug_down && !native.debug_key){native.debug=(native.debug+1)%6;}native.debug_key=input.debug_down;
        if(!c.frame.rendered){const float* p=input.postprocess;Write("[R4 native B5] output: gamma=%.3f brightness=%.3f contrast=%.3f balance=[%.3f %.3f %.3f] PP_base=[%.3f %.3f %.3f] PP_add=[%.3f %.3f %.3f]",
            p[0],p[1],p[2],p[19],p[20],p[21],p[10],p[11],p[12],p[13],p[14],p[15]);}
        const KichR4FrameColor color={sizeof(color),{((native.color>>16)&255)/255.f,((native.color>>8)&255)/255.f,(native.color&255)/255.f,1}};
        const bool tracing=g_render_snapshot_active;
        struct SinkGuard {KichR4NativeToolsAPI& api;KichR4Handle device;bool enabled;
            void Reset(){if(enabled){api.set_snapshot_sink(device,NULL);enabled=false;}}~SinkGuard(){Reset();}} sink_guard={native.api.tools,c.frame.device,tracing};
        if(tracing)native.api.tools.set_snapshot_sink(c.frame.device,RenderSnapshot_Draw);
        int32_t hr=c.frame.BeginClear(color);if(hr==KICH_R4_FRAME_OCCLUDED || hr==KICH_R4_FRAME_SUSPENDED){sink_guard.Reset();telemetry_guard.Reset();
            if(g_render_snapshot_active){RenderSnapshot_GPUTiming(RenderSnapshot_CaptureID(),input.engine_frame,RenderSnapshot_RenderFrameID(),"CANCELLED",NULL,0);ForgetTiming(RenderSnapshot_CaptureID());}
            RenderSnapshot_PresentID(native.successful_presents,false);RenderSnapshot_End("PRESENT_SUSPENDED");snapshot_guard.complete=true;return true;}
        if(hr==0 && c.loaded && input.allowed)hr=c.deferred.Draw(c.frame,c.level,c.stream,input,batch,native.debug);
        else if(hr==0){if(native.telemetry.set_capture && tracing)native.api.tools.profile_mark(c.frame.device,0);hr=c.deferred.DrawMenu(c.frame,c.level,c.stream,input,*batch);
            if(native.telemetry.set_capture && tracing)for(uint32_t marker=1;marker<8;++marker)native.api.tools.profile_mark(c.frame.device,marker);}
        if(hr==0 && KichR4_NativeNeedScreenshot()){std::vector<unsigned char> pixels;uint32_t w=0,h=0;if(KichR4_NativeReadPixels(pixels,w,h))KichR4_NativeWriteScreenshot(pixels.data(),w,h);}
        QueryPerformanceCounter(&submitted);
        if(hr==0){hr=c.frame.api.present(c.frame.device,native.interval);if(!hr){++c.frame.rendered;++native.successful_presents;}}
        QueryPerformanceCounter(&end);
        RenderSnapshot_PresentID(native.successful_presents,hr==0);
        sink_guard.Reset();telemetry_guard.Reset();
        if(g_render_snapshot_active){
            RenderSnapshot_Text("MEMORY","sample_point","AFTER_PRESENT_BEFORE_RUNTIME_TEXTURE_RETIREMENT");
            RenderSnapshot_U64("MEMORY","cpu_capture_bytes",batch->vertices.size()*sizeof(kich_r4_capture::Vertex)+batch->indices.size()*4);
            RenderSnapshot_U64("MEMORY","cpu_capture_capacity_bytes",batch->vertices.capacity()*sizeof(kich_r4_capture::Vertex)+batch->indices.capacity()*4);
            RenderSnapshot_U64("MEMORY","cpu_level_geometry_payload_bytes",c.level.scene.cpu_bytes);
            c.deferred.details.SnapshotSummary(input.engine_frame,c.loaded && input.allowed);
            c.deferred.native_models.SnapshotSummary();RenderSnapshot_Text("MODEL_RENDERER","frame_status",hr<0?"SUBMISSION_FAILED":"SUBMITTED");
            RenderSnapshot_U64("MEMORY","cpu_model_pose_payload_bytes",batch->pose_bytes);
            KichR4ResourceStats memory={};const int32_t memory_result=native.api.resources.get_stats(c.frame.device,&memory,sizeof(memory));
            if(memory_result==0){RenderSnapshot_U64("MEMORY","registered_gpu_payload_bytes",memory.payload_bytes);RenderSnapshot_Text("MEMORY","resource_stats_status","AVAILABLE");RenderSnapshot_U64("MEMORY","registered_gpu_resources",memory.live_resources);RenderSnapshot_U64("MEMORY","registered_gpu_created_total",memory.created);RenderSnapshot_U64("MEMORY","registered_gpu_released_total",memory.released);}
            else{RenderSnapshot_Text("MEMORY","registered_gpu_payload_bytes","NOT_AVAILABLE");RenderSnapshot_Text("MEMORY","resource_stats_status","QUERY_FAILED");RenderSnapshot_Value("MEMORY","resource_stats_hresult",memory_result);}
            RenderSnapshot_U64("FRAME_PASSPORT","native_successful_presents",native.successful_presents);RenderSnapshot_Text("FRAME_PASSPORT","native_present_result",hr==0?"PRESENTED":"FAILED_OR_OCCLUDED");
            RenderSnapshot_U64("MEMORY","runtime_texture_uploads_total",native.runtime_uploads);RenderSnapshot_U64("MEMORY","runtime_texture_reuses_total",native.runtime_reuses);RenderSnapshot_U64("MEMORY","runtime_texture_releases_total",native.runtime_releases);RenderSnapshot_U64("MEMORY","runtime_texture_uploaded_bytes_total",native.runtime_upload_bytes);
            RenderSnapshot_U64("MEMORY","runtime_uploaded_texture_handles",c.uploads.size());
            RenderSnapshot_Value("TIMING","cpu_prepare_ms",input.prepare_ms);RenderSnapshot_Value("TIMING","cpu_native_submit_ms",double(submitted.QuadPart-start.QuadPart)*1000.0/double(frequency.QuadPart));
            RenderSnapshot_Value("TIMING","cpu_present_ms",double(end.QuadPart-submitted.QuadPart)*1000.0/double(frequency.QuadPart));RenderSnapshot_Value("TIMING","cpu_backend_telemetry_record_ms",double(native.telemetry_cpu_ticks)*1000.0/double(frequency.QuadPart));
            RenderSnapshot_Text("TIMING","cpu_native_submit_scope","INCLUDES_SCENE_INIT_UPLOADS_AND_TELEMETRY; EXCLUDES_PRESENT");
            if(hr<0){RenderSnapshot_GPUTiming(RenderSnapshot_CaptureID(),input.engine_frame,RenderSnapshot_RenderFrameID(),"CANCELLED",NULL,0);ForgetTiming(RenderSnapshot_CaptureID());RenderSnapshot_End("GPU_SUBMISSION_FAILED");}
        }
        if(hr<0){Write("[R4 native B5] frame failed: step=%s HRESULT=0x%08X shader=%s",c.frame.step?c.frame.step:"present",static_cast<uint32_t>(hr),c.level.bridge.shader_error);return false;}
        if(c.frame.rendered && c.frame.rendered%300==0){KichR4GPUProfile profile={};native.api.tools.get_profile(c.frame.device,&profile,sizeof(profile));
            Write("[R4 native B5] frames=%u legacy_draws=0 models=%u hands=%u UI=%u CPU_queue_ms=%.3f CPU_submit_ms=%.3f present_ms=%.3f shadow_passes_frame=%u GPU_ready=%u GPU_ms=[%.3f %.3f %.3f %.3f %.3f %.3f %.3f]",
                c.frame.rendered,batch->models,batch->hands,batch->ui,input.prepare_ms,double(submitted.QuadPart-start.QuadPart)*1000.0/double(frequency.QuadPart),double(end.QuadPart-submitted.QuadPart)*1000.0/double(frequency.QuadPart),c.deferred.shadow_passes-shadows_before,profile.ready,
                profile.milliseconds[0],profile.milliseconds[1],profile.milliseconds[2],profile.milliseconds[3],profile.milliseconds[4],profile.milliseconds[5],profile.milliseconds[6]);}
        else if(c.loaded){KichR4GPUProfile unused={};native.api.tools.get_profile(c.frame.device,&unused,sizeof(unused));}
        {std::lock_guard<std::mutex> lock(runtime_mutex);std::set<std::string> alive;for(const auto& item:runtime)alive.insert(item.second.key);retired.clear();
            for(auto it=c.uploads.begin();it!=c.uploads.end();){if(!alive.count(it->first)){if(it->second.handle){c.level.bridge.resources.release_resource(c.frame.device,it->second.handle);++native.runtime_releases;}it=c.uploads.erase(it);}else ++it;}}
        snapshot_guard.complete=true;return true;
    }catch(const std::exception& e){Write("[R4 native B5] frame exception: %s",e.what());return false;}
}
void KichR4_NativeDestroy(){StopContext();if(native.module){FreeLibrary(native.module);native.module=NULL;}native.desired.clear();memset(&native.telemetry,0,sizeof(native.telemetry));}
