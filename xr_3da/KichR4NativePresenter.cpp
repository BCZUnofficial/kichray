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
#include "KichR4MaterialServices.h"
#include "KichR4TexturePreflight.h"
#include "KichR4FramePhases.h"
#include "KichR4UpdateProbe.h"
#include "KichR4RecordSession.h"
#include "KichR4RecordTiming.h"
#include "KichR4Watch.h"
#include "KichR4NormalMapProbe.h"
#include "KichR4QueueProfile.h"
bool KichR4_NativeTerrain(const char* shader,char (*layers)[128]);
bool KichR4_NativeMaterialState(const char*,const char*,kich_r4_capture::Style&);
bool KichR4_NativeNeedScreenshot();
void KichR4_NativeWriteScreenshot(const unsigned char*,uint32_t,uint32_t);
__declspec(dllimport) unsigned int rtc_decompress(void* dst,unsigned int dst_len,const void* src,unsigned int src_len); // xrCore rt_compressor.h
namespace {
// P0-S11 PERF01 background DDS reader, shared by every level context. Owned
// here, stopped in KichR4_NativeDestroy; never left to static destruction.
kich_r4_texture_io::Loader* texture_io=NULL;bool texture_io_failed=false;
kich_r4_texture_io::Loader* TextureIO(bool wanted){
    if(!wanted || texture_io_failed)return NULL;
    if(!texture_io)try{texture_io=new kich_r4_texture_io::Loader(rtc_decompress);}catch(...){texture_io_failed=true;texture_io=NULL;}
    return texture_io;}
struct Runtime {void* texture;std::string key;std::vector<unsigned char> pixels;uint32_t width,height;uint64_t revision;Runtime():texture(NULL),width(0),height(0),revision(0){};};
std::mutex runtime_mutex;
std::map<void*,Runtime> runtime;
std::map<std::string,Runtime> retired;
uint64_t texture_serial=0;
struct Uploaded {KichR4Handle handle;uint64_t revision,last_engine_frame,last_present_attempt,retry;Uploaded():handle(0),revision(0),last_engine_frame(0),last_present_attempt(0),retry(0){}};
struct WorkPeak {
    uint32_t frame=0;float camera[3]={};double queue=0,submit=0,plan=0,preflight=0,model=0,bounds=0,bvh_build=0,bvh_refit=0,sun_select=0,local_select=0,io=0,parse=0,upload=0;
    uint64_t detail_bounds=0,opens=0,uploads=0;uint32_t created=0,lights=0,shadow_faces=0;
};
struct Context {
    KichR4FrameSession frame;KichR4LevelSession level;KichR4StreamSession stream;KichR4DeferredSession deferred;
    std::map<std::string,Uploaded> uploads;
    uint64_t runtime_creates=0,runtime_updates=0,runtime_failures=0;
    bool loaded;std::string name;
    kich_r4_metrics::Window metrics;uint32_t last_created=0,last_released=0;
    uint64_t created_window=0,meshes_window=0;double submit_peak_ms=0;WorkPeak peak;
    uint32_t warmup_frames=0;
    uint64_t record_epoch_seen=UINT64_MAX,released_window=0;
    Context():loaded(false){memset(&frame,0,sizeof(frame));}
};
struct NativeState {
    HMODULE module;KichR4RendererAPI api;KichR4LevelFiles files;KichR4TelemetryAPI telemetry;
    kich_r4_record::TimingQueue timings;
    kich_r4_record::Session record;
    uint64_t timing_error_epoch=UINT64_MAX,timing_error_device_generation=UINT64_MAX,timing_error_level_generation=UINT64_MAX;
    char pending_marks[8][64]={};unsigned pending_mark_count=0;
    uint64_t current_engine_frame,device_generation,level_generation,present_frame,successful_presents,runtime_uploads,runtime_reuses,runtime_releases,runtime_upload_bytes,telemetry_cpu_ticks;
    std::unique_ptr<Context> context;
    void (*log)(const char*);void* window;uint32_t width,height,color,debug,interval;bool debug_key;std::string desired;
    NativeState():module(NULL),log(NULL),window(NULL),width(0),height(0),color(0xff000000),debug(0),interval(1),debug_key(false){memset(&api,0,sizeof(api));files=KichR4LevelFiles();memset(&telemetry,0,sizeof(telemetry));current_engine_frame=0;device_generation=level_generation=present_frame=successful_presents=runtime_uploads=runtime_reuses=runtime_releases=runtime_upload_bytes=telemetry_cpu_ticks=0;}
} native;
#if defined(__GNUC__)
__attribute__((format(gnu_printf,1,2)))
#endif
void Write(const char* format,...){if(!native.log)return;char text[1024];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);text[sizeof(text)-1]=0;native.log(text);}
void RecordOutput(const char* text){if(native.record.active)native.record.Emit("%s",text);}
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
void ForgetTiming(uint64_t id,uint64_t engine_frame,uint64_t render_frame){
    KichR4TelemetryTiming identity={};identity.capture_id=id;identity.engine_frame=engine_frame;identity.render_frame=render_frame;
    const unsigned index=native.timings.Find(identity);if(index<native.timings.count)native.timings.Erase(index);
}
void ExcludeRecordTiming(uint64_t engine_frame,const char* status){
    for(unsigned i=0;i<native.timings.count;){const auto& pending=native.timings.pending[i];const auto& p=pending.context;
        if(!pending.record_epoch || p.engine_frame!=engine_frame || p.present_frame!=native.present_frame){++i;continue;}
        if(native.record.active && pending.record_epoch==native.record.epoch)native.record.Emit("[R4 E1.7 gpu] session=%llu frame=%llu render_frame=%llu token=%llu status=%s; excluded_from_gameplay_GPU_measurements",
            static_cast<unsigned long long>(pending.record_epoch),static_cast<unsigned long long>(p.engine_frame),static_cast<unsigned long long>(p.render_frame),static_cast<unsigned long long>(p.capture_id),status);
        native.timings.Erase(i);
    }
    // Pending provider queries remain asynchronous and bounded. Their later
    // results have no owner and are discarded, never waited for or reused.
    if(native.context)native.context->deferred.ordinary_profile=false;
}
void CancelTimings(const char* status){
    for(unsigned i=0;i<native.timings.count;++i){const auto& pending=native.timings.pending[i];const auto& p=pending.context;
        if(!pending.record_epoch)RenderSnapshot_GPUTiming(p.capture_id,p.engine_frame,p.render_frame,status,NULL,0);
        else if(native.record.active && pending.record_epoch==native.record.epoch)native.record.Emit("[R4 E1.7 gpu] session=%llu frame=%llu token=%llu status=%s",static_cast<unsigned long long>(pending.record_epoch),static_cast<unsigned long long>(p.engine_frame),static_cast<unsigned long long>(p.capture_id),status);}
    native.timings.Clear();
}
void PollTimings(){
    if(!native.context || !native.telemetry.poll_timing || (!kich_r4_record::Enabled() && !native.timings.count))return;
    KichR4TelemetryTiming result={};const int32_t hr=native.telemetry.poll_timing(native.context->frame.device,&result,sizeof(result));
    if(hr!=0){if(native.timing_error_epoch!=native.record.epoch || native.timing_error_device_generation!=native.device_generation || native.timing_error_level_generation!=native.level_generation){
            native.timing_error_epoch=native.record.epoch;native.timing_error_device_generation=native.device_generation;native.timing_error_level_generation=native.level_generation;
            Write("[R4 E1.7 gpu] timing_poll_failed HRESULT=0x%08X; rendering_continues; not_device_reset_evidence; once_per_session_context",static_cast<uint32_t>(hr));}
        CancelTimings("UNSUPPORTED");return;}
    if(!result.status)return;
    const unsigned pending=native.timings.Find(result);
    if(pending==native.timings.count)return;
    const char* status=result.status==KICH_R4_TIMING_READY?"READY":result.status==KICH_R4_TIMING_DISJOINT?"DISJOINT":result.status==KICH_R4_TIMING_TIMEOUT?"TIMEOUT":result.status==KICH_R4_TIMING_ABORTED?"CANCELLED":"UNSUPPORTED";
    const uint64_t epoch=native.timings.pending[pending].record_epoch;
    if(!epoch)RenderSnapshot_GPUTiming(result.capture_id,result.engine_frame,result.render_frame,status,result.milliseconds,result.valid_mask);
    else if(native.record.active && epoch==native.record.epoch)native.record.Emit("[R4 E1.7 gpu] session=%llu frame=%llu render_frame=%llu token=%llu status=%s mask=0x%X age_frames=%llu resource_shadow_ms=%.3f gbuffer_ms=%.3f lighting_ms=%.3f combine_ms=%.3f forward_ms=%.3f post_ui_ms=%.3f end_gap_ms=%.3f; ORIGINAL_FRAME_NOT_CURRENT_CPU_FRAME",
        static_cast<unsigned long long>(epoch),static_cast<unsigned long long>(result.engine_frame),static_cast<unsigned long long>(result.render_frame),static_cast<unsigned long long>(result.capture_id),status,result.valid_mask,
        static_cast<unsigned long long>(native.current_engine_frame>=result.engine_frame?native.current_engine_frame-result.engine_frame:0),result.milliseconds[0],result.milliseconds[1],result.milliseconds[2],result.milliseconds[3],result.milliseconds[4],result.milliseconds[5],result.milliseconds[6]);
    native.timings.Erase(pending);
}
void BeginTelemetry(const KichR4GameFrame& input,bool menu,int64_t now){
    KichR4TelemetryContext capture={};capture.struct_size=sizeof(capture);
    capture.flags=(RenderSnapshot_Detailed()?KICH_R4_TELEMETRY_DETAILED:0)|(menu?KICH_R4_TELEMETRY_MENU:0);
    native.current_engine_frame=input.engine_frame;capture.capture_id=g_render_snapshot_active?RenderSnapshot_CaptureID():0;capture.engine_frame=input.engine_frame;
    capture.render_frame=g_render_snapshot_active?RenderSnapshot_RenderFrameID():0;capture.present_frame=++native.present_frame;
    capture.device_generation=native.device_generation;capture.level_generation=native.level_generation;native.telemetry_cpu_ticks=0;
    native.context->deferred.ordinary_profile=false;
    native.context->deferred.profile_context_valid=false;
    bool timing_owned=false;
    if(capture.capture_id){
        RenderSnapshot_GPUTiming(capture.capture_id,capture.engine_frame,capture.render_frame,native.telemetry.set_capture?"NOT_READY":"UNSUPPORTED",NULL,menu?1:127);
        if(native.telemetry.set_capture){timing_owned=native.timings.Add(capture,0);if(!timing_owned)RenderSnapshot_GPUTiming(capture.capture_id,capture.engine_frame,capture.render_frame,"TIMEOUT",NULL,0);}
        RenderSnapshot_Text("GPU_TIMING","stage_mapping",menu?"0=MENU_RENDER_TOTAL;1..6=NOT_APPLICABLE":"0=RESOURCE_INIT_UPLOAD_SHADOWS;1=GBUFFER;2=LIGHTING;3=COMBINE;4=FORWARD_DISTORT_HUD;5=POSTPROCESS_UI;6=END_MARKER_GAP");
        RenderSnapshot_Text("GPU_TIMING","poll_policy","DONOTFLUSH; NO_WAIT; COMPLETION_TO_ORIGINAL_CAPTURE; MAX_120_RENDER_POLLS");
        RenderSnapshot_Text("GPU_TIMING","paused_render","PENDING_UNTIL_RENDER_RESUMES_OR_CONTEXT_CLOSES; NO_SPIN_WAIT");
        RenderSnapshot_Text("FRAME_PASSPORT","backend_telemetry_api",native.telemetry.set_capture?"AVAILABLE_ABI_1":"UNSUPPORTED");
        RenderSnapshot_Text("FRAME_PASSPORT","r4_dll_revision",native.api.build.revision);RenderSnapshot_Text("FRAME_PASSPORT","r4_dll_source_sha256",native.api.build.source_id);
        RenderSnapshot_Text("FRAME_PASSPORT","r4_dll_shader_sha256",native.api.build.shader_id);RenderSnapshot_Text("FRAME_PASSPORT","r4_dll_compiled",native.api.build.compiled);
        RenderSnapshot_U64("FRAME_PASSPORT","native_present_attempt_ordinal",capture.present_frame);RenderSnapshot_U64("FRAME_PASSPORT","native_device_generation",capture.device_generation);RenderSnapshot_U64("FRAME_PASSPORT","native_level_generation",capture.level_generation);
        RenderSnapshot_U64("FRAME_PASSPORT","native_precache_warmup",input.warmup?1:0);
        if(input.warmup)RenderSnapshot_Text("GPU_TIMING","warmup_scope","SCENE_PLUS_LOADING_UI; EXCLUDED_FROM_GAMEPLAY_PERFORMANCE");
    }
    else if(native.record.active && !menu && input.allowed && input.active && !input.warmup && !native.debug && !input.debug_down &&
        !input.observation.paused && !input.observation.console_visible && native.record.GPUDue(now)){
        capture.capture_id=native.timings.NextToken();capture.render_frame=capture.present_frame;
        const bool queued=native.telemetry.set_capture && native.timings.Add(capture,native.record.epoch);
        timing_owned=queued;
        native.context->deferred.ordinary_profile=queued;
        native.record.Emit("[R4 E1.7 gpu_request] session=%llu frame=%u record_ms=%.3f token=%llu status=%s camera=[%.3f %.3f %.3f] direction=[%.4f %.4f %.4f]; sample_period_ms=500",
            static_cast<unsigned long long>(native.record.epoch),input.engine_frame,native.record.Elapsed(now),static_cast<unsigned long long>(capture.capture_id),queued?"ISSUED":"UNSUPPORTED_OR_QUEUE_FULL",
            input.position[0],input.position[1],input.position[2],input.observation.camera_direction[0],input.observation.camera_direction[1],input.observation.camera_direction[2]);
        if(!queued)capture.capture_id=capture.render_frame=0;
    }
    if(native.telemetry.set_capture){const int32_t hr=native.telemetry.set_capture(native.context->frame.device,&capture,RenderSnapshot_Detailed()?TelemetryRecord:NULL);
        native.context->deferred.profile_context_valid=hr==KICH_R4_OK;
        if(hr!=0 && capture.capture_id){if(g_render_snapshot_active)RenderSnapshot_GPUTiming(capture.capture_id,capture.engine_frame,capture.render_frame,"UNSUPPORTED",NULL,0);
            else native.record.Emit("[R4 E1.7 gpu] session=%llu frame=%u status=UNSUPPORTED",static_cast<unsigned long long>(native.record.epoch),input.engine_frame);
            native.context->deferred.ordinary_profile=false;if(timing_owned)ForgetTiming(capture.capture_id,capture.engine_frame,capture.render_frame);}}
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

void StopContext(){kich_r4_phases::Global().Reset();native.record.Gap();kich_r4_watch::Global().Reset();if(!native.context)return;CancelTimings("CANCELLED");Context& c=*native.context;if(c.frame.device){KichR4GeometryReport report={};const int32_t hr=c.level.bridge.Stop(c.frame,&report);
    Write("[R4 native B5] cleanup: HRESULT=0x%08X resources=%u/%u live=%u payload=%llu views=%u/%u",static_cast<uint32_t>(hr),report.resources.created,report.resources.released,report.resources.live_resources,static_cast<unsigned long long>(report.resources.payload_bytes),report.frame.views_created,report.frame.views_released);}
    native.context.reset();
}
KichR4Handle OverrideTexture(void* context,const std::string& key,bool& handled){Context& c=*static_cast<Context*>(context);handled=false;
    if(key.compare(0,12,"r4_runtime__"))return 0;
    handled=true;
    std::lock_guard<std::mutex> lock(runtime_mutex);Runtime* source=NULL;
    for(std::map<void*,Runtime>::iterator i=runtime.begin();i!=runtime.end();++i)if(i->second.key==key){source=&i->second;break;}
    if(!source){const auto found=retired.find(key);if(found!=retired.end())source=&found->second;}
    Uploaded& uploaded=c.uploads[key];
    if(!source)return uploaded.handle?uploaded.handle:c.level.checker;
    if(uploaded.retry>native.present_frame)return uploaded.handle?uploaded.handle:c.level.checker;
    uint32_t w=source->width,h=source->height;uint64_t revision=source->revision;
    const unsigned char* pixels=source->texture?KichR4_CPUTexturePixels(source->texture,w,h,revision):source->pixels.data();if(!pixels || !w || !h)return uploaded.handle?uploaded.handle:c.level.checker;
    if(uploaded.handle && (uploaded.revision==revision || uploaded.last_present_attempt==native.present_frame)){++native.runtime_reuses;
        if(RenderSnapshot_Detailed()){RenderSnapshot_Record("DYNAMIC_TEXTURE",key.c_str(),"action","REUSED_GPU_TEXTURE");RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"handle",uploaded.handle);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"revision",revision);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"last_upload_engine_frame",uploaded.last_engine_frame);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"last_upload_present_attempt",uploaded.last_present_attempt);}return uploaded.handle;}
    if(!w || !h || w>16384 || h>16384 || uint64_t(w)*h*4>UINT32_MAX)return uploaded.handle?uploaded.handle:c.level.checker;
    KichR4Handle next=0;const int32_t hr=native.api.tools.upload_bgra8(c.frame.device,uploaded.handle,w,h,pixels,w*4,w*h*4,&next);
    if(hr<0){++c.runtime_failures;uploaded.retry=native.present_frame+30;
        const uint32_t code=static_cast<uint32_t>(hr);
        if(kich_r4_texture::FatalDevice(hr))c.frame.Record("E1 runtime device failure",hr);
        Write("[R4 E1] runtime upload failed; retaining last-good: %s HRESULT=0x%08X",key.c_str(),code);return uploaded.handle?uploaded.handle:c.level.checker;}
    uploaded.retry=0;
    if(next==uploaded.handle)++c.runtime_updates;else ++c.runtime_creates;
    ++native.runtime_uploads;native.runtime_upload_bytes+=uint64_t(w)*h*4;uploaded.last_engine_frame=native.current_engine_frame;uploaded.last_present_attempt=native.present_frame;
    if(RenderSnapshot_Detailed()){RenderSnapshot_Record("DYNAMIC_TEXTURE",key.c_str(),"action",next==uploaded.handle?"DYNAMIC_WRITE_DISCARD_UPDATE":"DYNAMIC_TEXTURE_CREATE_AND_UPLOAD");RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"handle",next);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"revision",revision);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"width",w);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"height",h);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"upload_bytes",uint64_t(w)*h*4);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"last_upload_engine_frame",uploaded.last_engine_frame);RenderSnapshot_RecordU64("DYNAMIC_TEXTURE",key.c_str(),"last_upload_present_attempt",uploaded.last_present_attempt);}
    if(uploaded.handle && uploaded.handle!=next){c.level.bridge.resources.release_resource(c.frame.device,uploaded.handle);++native.runtime_releases;}uploaded.handle=next;uploaded.revision=revision;return next;
}
void ConfigureContext(Context& c){c.frame.api=native.api.frame;c.level.bridge.resources=native.api.resources;c.level.bridge.api=native.api.geometry;
    c.level.api=native.api.level;c.level.files=native.files;c.level.log=native.log;c.stream.api=native.api.stream;c.deferred.api=native.api.deferred;c.deferred.native_mode=true;c.deferred.tools=native.api.tools;
    c.deferred.native_models.api=native.api.models;c.deferred.details.api=native.api.details;c.deferred.details.source=&KichR4_DetailFrame();
    c.level.keep_going=[](void*)->bool{KichR4_NativePumpLoading();return true;};
    c.level.texture_override=OverrideTexture;c.level.texture_context=&c;
    c.level.texture_registry.budget=uint64_t(KichR4_NativeTextureBudgetMB())*1024*1024;
}
bool CreatePlaceholders(Context& c){if(c.level.Placeholder(c.frame,false,&c.level.white)!=0 || c.level.Placeholder(c.frame,true,&c.level.checker)!=0)return false;
    c.level.TransparentPlaceholder(c.frame);return true;} // P0-S11, optional
bool CreateContext(){native.context.reset(new Context());Context& c=*native.context;ConfigureContext(c);
    if(c.frame.Start(native.window,native.width,native.height)<0)return false;
    ++native.device_generation;++native.level_generation;
    return CreatePlaceholders(c);
}
bool ResetContext(){kich_r4_phases::Global().Reset();native.record.Gap();CancelTimings("DEVICE_RESET");Context& previous=*native.context;
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
        if(pixels && saved.width<=16384 && saved.height<=16384 && uint64_t(saved.width)*saved.height*4<=UINT32_MAX){saved.pixels.assign(pixels,pixels+size_t(saved.width)*saved.height*4);saved.texture=NULL;retired[saved.key]=std::move(saved);}}
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
void KichR4_RecordSet(bool enabled){kich_r4_record::Request(enabled);if(!enabled)native.pending_mark_count=0;Write("[R4 E1.7 record] requested=%u; applies_at_frame_boundary; normal_error_log_remains_enabled",unsigned(enabled));}
void KichR4_RecordMark(const char* label){
    if(!kich_r4_record::Requested()){Write("[R4 E1.7 mark] recording_is_OFF; use log_record 1 first");return;}
    if(!native.record.active){
        if(native.pending_mark_count==8){Write("[R4 E1.7 mark] pending_queue_full; label_not_recorded");return;}
        snprintf(native.pending_marks[native.pending_mark_count++],64,"%.63s",label?label:"");
        Write("[R4 E1.7 mark] queued_until_record_start label=%.63s",label?label:"");return;
    }
    const int64_t now=kich_r4_phases::Counter();
    native.record.Emit("[R4 E1.7 mark] session=%llu record_ms=%.3f last_present_frame=%u label=%s; no_snapshot_no_readback",
        static_cast<unsigned long long>(native.record.epoch),native.record.Elapsed(now),native.record.last_frame,label?label:"");
}
void KichR4_RecordWatch(bool enabled){kich_r4_watch::Global().Request(enabled);Write("[R4 E1.7 watch] requested=%u; camera_ray_bounds_candidate_not_pixel_pick; needs log_record 1",unsigned(enabled));}
void KichR4_RecordView(unsigned view){if(view>=kich_r4_optics::DebugViewCount)return;native.debug=view;
    Write("[R4 E1.7 view] view=%u %s; diagnostic_views_excluded_from_gameplay_FPS",view,kich_r4_optics::DebugView(view));}
unsigned KichR4_RecordView(){return native.debug;}
bool KichR4_NativeReadPixels(std::vector<unsigned char>& out,uint32_t& w,uint32_t& h){if(!native.context)return false;Context& c=*native.context;
    if(native.api.tools.read_pixels(c.frame.device,NULL,0,&w,&h)<0 || !w || !h || uint64_t(w)*h*4>256u*1024u*1024u)return false;
    out.resize(size_t(w)*h*4);
    return native.api.tools.read_pixels(c.frame.device,out.data(),static_cast<uint32_t>(out.size()),&w,&h)==0;
}
bool KichR4_NativePresent(const KichR4GameFrame& input){if(!native.context)return false;
    const bool measure=kich_r4_record::Measure(g_render_snapshot_active);
    const int64_t prelude_started=measure?kich_r4_phases::Counter():0;
    const int64_t record_clock=measure?prelude_started:native.record.active?kich_r4_phases::Counter():0;
    if(native.record.Sync(kich_r4_record::Enabled(),kich_r4_record::Epoch(),record_clock,measure?kich_r4_phases::Frequency():native.record.frequency,native.log)){
        native.timings.DropRecord();if(!native.record.active)kich_r4_watch::Global().Reset();
    }
    if(native.record.active && native.pending_mark_count){
        for(unsigned i=0;i<native.pending_mark_count;++i)native.record.Emit("[R4 E1.7 mark] session=%llu record_ms=%.3f frame=%u label=%s; queued_before_session; no_snapshot_no_readback",
            static_cast<unsigned long long>(native.record.epoch),native.record.Elapsed(record_clock),input.engine_frame,native.pending_marks[i]);
        native.pending_mark_count=0;
    }
    native.current_engine_frame=input.engine_frame;
    kich_r4_normal_probe::Global().BeginFrame(input.engine_frame);
    struct PhaseTail {uint32_t frame;int64_t started;explicit PhaseTail(uint32_t f):frame(f),started(0){}
        ~PhaseTail(){if(started)kich_r4_phases::Global().Span(frame,kich_r4_phases::PostPresent,kich_r4_phases::Counter()-started);}} phase_tail(input.engine_frame);
    PollTimings();Context* context=native.context.get();
    struct SnapshotFailureGuard {uint32_t frame;bool complete;explicit SnapshotFailureGuard(uint32_t value):frame(value),complete(false){}
        ~SnapshotFailureGuard(){if(!complete){ExcludeRecordTiming(frame,"FRAME_ABORTED");if(g_render_snapshot_active){
            RenderSnapshot_GPUTiming(RenderSnapshot_CaptureID(),frame,RenderSnapshot_RenderFrameID(),"CANCELLED",NULL,0);
            ForgetTiming(RenderSnapshot_CaptureID(),frame,RenderSnapshot_RenderFrameID());RenderSnapshot_PresentID(native.successful_presents,false);RenderSnapshot_End("NATIVE_FRAME_FAILED");}}}} snapshot_guard(input.engine_frame);
    if(g_render_snapshot_active){RenderSnapshot_Text("MEMORY","registered_gpu_payload_bytes","NOT_AVAILABLE");RenderSnapshot_Text("MEMORY","resource_stats_status","NOT_QUERIED");
        RenderSnapshot_Text("MEMORY","registered_gpu_payload_scope","DEVICE_RESOURCE_REGISTRY_PAYLOAD; NOT_VRAM_RESIDENCY");RenderSnapshot_Text("MEMORY","cpu_capture_bytes","NOT_AVAILABLE");
        RenderSnapshot_Text("MODEL_RENDERER","frame_status","NOT_SUBMITTED");
        context->deferred.details.SnapshotSummary(input.engine_frame,false);}
    try{if(context->name!=native.desired){if(!ResetContext())return false;context=native.context.get();}
        Context& c=*context;
        if(c.record_epoch_seen!=kich_r4_record::Epoch()){
            c.record_epoch_seen=kich_r4_record::Epoch();c.metrics=kich_r4_metrics::Window();c.created_window=c.released_window=c.meshes_window=0;c.submit_peak_ms=0;c.peak=WorkPeak();
            if(native.record.active){KichR4ResourceStats baseline={};if(native.api.resources.get_stats(c.frame.device,&baseline,sizeof(baseline))==0){c.last_created=baseline.created;c.last_released=baseline.released;}}
        }
        c.level.texture_measure_snapshot=g_render_snapshot_active;
        c.level.texture_io=TextureIO(input.lighting.dds_async!=0); // P0-S11 r4_dds_async
        c.level.texture_registry.Begin();BeginTelemetry(input,c.name.empty() || !(input.allowed || input.warmup),record_clock);TelemetryGuard telemetry_guard(c.frame.device);
        LARGE_INTEGER start={},end={},submitted={},frequency={};if(measure){QueryPerformanceCounter(&start);frequency.QuadPart=kich_r4_phases::Frequency();}
        const uint32_t shadows_before=c.deferred.shadow_passes;
        if(measure)kich_r4_phases::Global().Span(input.engine_frame,kich_r4_phases::NativePrelude,start.QuadPart-prelude_started);
        const bool scene_requested=input.allowed || input.warmup;
        if(!c.loaded && !c.name.empty() && scene_requested){if(c.level.Start(c.frame,c.name.c_str())!=0)return false;c.loaded=true;
            for(size_t i=0;i<c.level.scene.materials.size();++i){kich_r4_level::Material& m=c.level.scene.materials[i];char layers[4][128]={};if(KichR4_NativeTerrain(m.shader.c_str(),layers)){m.kind=1;for(unsigned j=0;j<4;++j)m.layers[j]=layers[j];}
                if(!m.shader.empty() && KichR4_NativeResolveMaterial(m.shader.c_str(),m.texture_list.c_str(),m.descriptor)){
                    kich_r4_capture::Style style;
                    if(m.descriptor.semantic==kich_r4_material::UNKNOWN)KichR4_NativeMaterialState(m.shader.c_str(),m.texture_list.c_str(),style);
                    else kich_r4_material::Apply(m.descriptor,style);
                    m.cutout=m.descriptor.semantic==kich_r4_material::CUTOUT || (m.descriptor.semantic==kich_r4_material::UNKNOWN && style.alpha_func!=8);m.blend=style.blend;m.source=style.src;m.destination=style.dst;m.alpha_func=style.alpha_func;m.alpha_ref=style.alpha_ref;
                    if(m.descriptor.special==kich_r4_material::SPECIAL_WATER)m.kind=2;
                    else if(m.descriptor.special==kich_r4_material::SPECIAL_TERRAIN)m.kind=1;
                    else m.kind=m.descriptor.semantic==kich_r4_material::UNKNOWN?(style.blend?3u:m.kind):kich_r4_material::Geometry(m.descriptor)?0u:3u;
                }}
            LARGE_INTEGER loaded={};if(measure){QueryPerformanceCounter(&loaded);
            Write("[R4 G3e load] native_level_and_materials_ms=%.3f DDS_io_ms=%.3f DDS_parse_ms=%.3f DDS_create_upload_api_ms=%.3f DDS_loaded=%u; excludes engine prefetch; create includes provider parse and upload submission",
                double(loaded.QuadPart-start.QuadPart)*1000.0/double(frequency.QuadPart),c.level.texture_io_ms,c.level.texture_parse_ms,c.level.texture_create_ms,c.level.textures_loaded);
            }
        }
        const kich_r4_capture::Batch* batch=KichR4_CapturedFrame();if(!batch || !batch->Ready()){Write("[R4 native B5] rejected CPU frame: %s",batch?batch->error.c_str():"queue unavailable");return false;}
        const auto plan_started=kich_r4_metrics::Start(g_render_snapshot_active);
        if(c.loaded && scene_requested)c.deferred.BeginFramePlan(c.level,input);
        const double plan_ms=kich_r4_metrics::Elapsed(plan_started);
        const auto preflight_started=kich_r4_metrics::Start(g_render_snapshot_active);
        const int32_t preflight=kich_r4_texture::Preflight(c.frame,c.level,*batch,input,c.deferred.details,c.loaded && scene_requested,c.loaded && scene_requested?&c.deferred.visibility_plan:NULL,input.warmup);
        if(preflight==KICH_R4_FRAME_SUSPENDED){
            c.metrics.Gap();native.record.Gap();
            kich_r4_phases::Global().MarkEligible(input.engine_frame,false);
            ExcludeRecordTiming(input.engine_frame,"FRAME_SUSPENDED");
            if(g_render_snapshot_active){RenderSnapshot_GPUTiming(RenderSnapshot_CaptureID(),input.engine_frame,RenderSnapshot_RenderFrameID(),"CANCELLED",NULL,0);ForgetTiming(RenderSnapshot_CaptureID(),input.engine_frame,RenderSnapshot_RenderFrameID());}
            RenderSnapshot_PresentID(native.successful_presents,false);RenderSnapshot_End("PRECACHE_CANCELLED");snapshot_guard.complete=true;return true;}
        if(preflight!=KICH_R4_OK)return false;
        const double preflight_ms=kich_r4_metrics::Elapsed(preflight_started);
        if(g_render_snapshot_active){const KichR4LevelSession::AsyncCounters& a=c.level.preflight_probe.async;
            RenderSnapshot_U64("DDS_ASYNC","enabled",c.level.texture_io?1:0);RenderSnapshot_U64("DDS_ASYNC","frame_requests",a.requests);
            RenderSnapshot_U64("DDS_ASYNC","frame_staged",a.staged);RenderSnapshot_U64("DDS_ASYNC","frame_staged_bytes",a.staged_bytes);
            RenderSnapshot_U64("DDS_ASYNC","frame_waiting",a.waiting);RenderSnapshot_U64("DDS_ASYNC","frame_sync_fallbacks",a.failures);
            if(texture_io){const kich_r4_texture_io::Stats io=texture_io->Snapshot();RenderSnapshot_U64("DDS_ASYNC","queued",io.queued);
                RenderSnapshot_U64("DDS_ASYNC","ready",io.ready);RenderSnapshot_U64("DDS_ASYNC","ready_bytes",io.ready_bytes);}
            RenderSnapshot_Text("DDS_ASYNC","policy","WORLD_ROLES_BACKGROUND_READ; UI_HUD_AND_PRECACHE_SYNC; MAIN_THREAD_LOOKUP_ONLY; WORKER_WIN32_READ_AND_LZO; PENDING_BASE_WHITE_EFFECT_TRANSPARENT_OPTIONAL_CHECKER");}
        if(c.stream.PrepareUI(c.frame,c.level,*batch)<0)return false;
        if(input.debug_down && !native.debug_key){native.debug=(native.debug+1)%kich_r4_optics::DebugViewCount;
            Write("[R4 G3e] F9 view=%u %s; diagnostic views bypass postprocess",native.debug,kich_r4_optics::DebugView(native.debug));}native.debug_key=input.debug_down;
        if(!c.frame.rendered){const float* p=input.postprocess;Write("[R4 native B5] output: gamma=%.3f brightness=%.3f contrast=%.3f balance=[%.3f %.3f %.3f] PP_base=[%.3f %.3f %.3f] PP_add=[%.3f %.3f %.3f]",
            p[0],p[1],p[2],p[19],p[20],p[21],p[10],p[11],p[12],p[13],p[14],p[15]);}
        const KichR4FrameColor color={sizeof(color),{((native.color>>16)&255)/255.f,((native.color>>8)&255)/255.f,(native.color&255)/255.f,1}};
        const bool tracing=g_render_snapshot_active;
        const bool watch_allowed=native.record.active && c.loaded && input.allowed && input.active && !input.warmup && input.observation.valid;
        struct WatchFrameGuard {
            bool complete;uint32_t frame,time_ms;
            ~WatchFrameGuard(){if(!complete)kich_r4_watch::Global().Suspend(frame,time_ms,"FRAME_ABORTED_OR_SUSPENDED");}
        } watch_guard={!watch_allowed,input.engine_frame,native.record.active?static_cast<uint32_t>(native.record.Elapsed(record_clock)):0};
        if(native.record.active){
            const int64_t watch_started=kich_r4_phases::Counter();
            if(watch_allowed)kich_r4_watch::Global().Begin(input.engine_frame,watch_guard.time_ms,native.level_generation,*batch,input.position,input.observation.camera_direction,
                input.observation.current_entity_valid?input.observation.current_entity_position:NULL);
            else kich_r4_watch::Global().Suspend(input.engine_frame,watch_guard.time_ms,"NO_ACTIVE_WORLD_FRAME");
            native.record.collect_ms+=double(kich_r4_phases::Counter()-watch_started)*1000.0/double(frequency.QuadPart);
        }
        struct SinkGuard {KichR4NativeToolsAPI& api;KichR4Handle device;bool enabled;
            void Reset(){if(enabled){api.set_snapshot_sink(device,NULL);enabled=false;}}~SinkGuard(){Reset();}} sink_guard={native.api.tools,c.frame.device,tracing};
        if(tracing)native.api.tools.set_snapshot_sink(c.frame.device,RenderSnapshot_Draw);
        int32_t hr=c.frame.BeginClear(color);if(hr==KICH_R4_FRAME_OCCLUDED || hr==KICH_R4_FRAME_SUSPENDED){sink_guard.Reset();telemetry_guard.Reset();
            c.metrics.Gap();native.record.Gap();
            ExcludeRecordTiming(input.engine_frame,"FRAME_SUSPENDED");
            if(g_render_snapshot_active){RenderSnapshot_GPUTiming(RenderSnapshot_CaptureID(),input.engine_frame,RenderSnapshot_RenderFrameID(),"CANCELLED",NULL,0);ForgetTiming(RenderSnapshot_CaptureID(),input.engine_frame,RenderSnapshot_RenderFrameID());}
            RenderSnapshot_PresentID(native.successful_presents,false);RenderSnapshot_End("PRESENT_SUSPENDED");snapshot_guard.complete=true;return true;}
        if(hr==0 && c.loaded && scene_requested){hr=c.deferred.Draw(c.frame,c.level,c.stream,input,batch,input.warmup?0:native.debug);
            // The existing rotating precache camera exercises the real scene
            // dependency union and lazy pipelines under normal budgets. Keep
            // loading/UI as the final output; no readback, wait or asset sweep.
            if(hr==0 && input.warmup){hr=c.deferred.DrawMenu(c.frame,c.level,c.stream,input,*batch,true);if(hr==0)++c.warmup_frames;}}
        else if(hr==0){if(c.deferred.profile_context_valid && native.telemetry.set_capture && tracing)native.api.tools.profile_mark(c.frame.device,0);hr=c.deferred.DrawMenu(c.frame,c.level,c.stream,input,*batch);
            if(c.deferred.profile_context_valid && native.telemetry.set_capture && tracing)for(uint32_t marker=1;marker<8;++marker)native.api.tools.profile_mark(c.frame.device,marker);}
        if(hr==0 && c.frame.failure<0)hr=c.frame.failure;
        const bool screenshot=hr==0 && KichR4_NativeNeedScreenshot();
        if(screenshot){ExcludeRecordTiming(input.engine_frame,"EXCLUDED_SCREENSHOT");std::vector<unsigned char> pixels;uint32_t w=0,h=0;if(KichR4_NativeReadPixels(pixels,w,h))KichR4_NativeWriteScreenshot(pixels.data(),w,h);}
        if(measure)QueryPerformanceCounter(&submitted);
        if(hr==0){hr=c.frame.api.present(c.frame.device,native.interval);if(!hr){++c.frame.rendered;++native.successful_presents;}}
        if(measure){QueryPerformanceCounter(&end);
            kich_r4_phases::Global().Span(input.engine_frame,kich_r4_phases::NativeSubmit,submitted.QuadPart-start.QuadPart);
            kich_r4_phases::Global().Span(input.engine_frame,kich_r4_phases::Present,end.QuadPart-submitted.QuadPart);
            phase_tail.started=end.QuadPart;}
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
            RenderSnapshot_U64("MEMORY","runtime_creates_scene",c.runtime_creates);RenderSnapshot_U64("MEMORY","runtime_updates_scene",c.runtime_updates);RenderSnapshot_U64("MEMORY","runtime_failures_scene",c.runtime_failures);
            RenderSnapshot_U64("MEMORY","dds_resident_payload_bytes",c.level.texture_registry.bytes);
            RenderSnapshot_U64("MEMORY","dds_budget_bytes",c.level.texture_registry.budget);
            RenderSnapshot_U64("MEMORY","dds_evictions_total",c.level.texture_registry.evictions);
            RenderSnapshot_Text("MEMORY","dds_residency_policy","FRAME_DEPENDENCY_UNION; PLAN_MIPS_BEFORE_EVICTION; RETAINED_MIP_HYSTERESIS; UI_HUD_PRIORITY");
            RenderSnapshot_U64("MEMORY","dds_planned_payload_bytes",c.level.texture_registry.planned_bytes);
            RenderSnapshot_U64("MEMORY","dds_planned_sources",c.level.texture_registry.planned_sources);
            RenderSnapshot_U64("MEMORY","dds_planned_mip_steps",c.level.texture_registry.planned_downgrades);
            RenderSnapshot_U64("MEMORY","dds_irreducible_budget_denials",c.level.texture_registry.planned_denied);
            RenderSnapshot_U64("MEMORY","ui_texture_sources",c.stream.ui_texture_sources);
            RenderSnapshot_U64("MEMORY","ui_texture_ready",c.stream.ui_texture_ready);
            RenderSnapshot_U64("MEMORY","ui_texture_fallbacks",c.stream.ui_texture_fallbacks);
            std::set<std::string> ui_textures;
            for(const auto& command:batch->commands)if(command.layer==2 && !command.texture.empty())ui_textures.insert(kich_r4_texture::Key(command.texture));
            for(const auto& name:ui_textures){const auto found=c.level.texture_registry.entries.find(name);
                if(found==c.level.texture_registry.entries.end()){RenderSnapshot_Record("UI_TEXTURE",name.c_str(),"source","OVERRIDE_OR_NON_DDS");continue;}
                const auto& entry=found->second;
                RenderSnapshot_Record("UI_TEXTURE",name.c_str(),"status",entry.handle?"RESIDENT":"FALLBACK");
                RenderSnapshot_RecordU64("UI_TEXTURE",name.c_str(),"handle",entry.handle);
                RenderSnapshot_RecordU64("UI_TEXTURE",name.c_str(),"payload_bytes",entry.bytes);
                RenderSnapshot_RecordU64("UI_TEXTURE",name.c_str(),"failure_kind",entry.reason);
                RenderSnapshot_RecordU64("UI_TEXTURE",name.c_str(),"hresult",static_cast<uint32_t>(entry.failure));
                RenderSnapshot_RecordU64("UI_TEXTURE",name.c_str(),"pinned_this_frame",entry.touched==c.level.texture_registry.epoch);}
            for(const auto& item:c.level.texture_registry.entries){const auto& e=item.second;
                RenderSnapshot_RecordU64("DDS_RESIDENCY",item.first.c_str(),"handle",e.handle);RenderSnapshot_RecordU64("DDS_RESIDENCY",item.first.c_str(),"payload_bytes",e.bytes);
                RenderSnapshot_RecordU64("DDS_RESIDENCY",item.first.c_str(),"width",e.width);RenderSnapshot_RecordU64("DDS_RESIDENCY",item.first.c_str(),"height",e.height);RenderSnapshot_RecordU64("DDS_RESIDENCY",item.first.c_str(),"mips",e.mips);
                RenderSnapshot_RecordU64("DDS_RESIDENCY",item.first.c_str(),"requested_this_frame",e.wanted==c.level.texture_registry.epoch);
                RenderSnapshot_RecordU64("DDS_RESIDENCY",item.first.c_str(),"role_priority",e.role);
                RenderSnapshot_RecordU64("DDS_RESIDENCY",item.first.c_str(),"resident_first_mip",e.resident);
                RenderSnapshot_RecordU64("DDS_RESIDENCY",item.first.c_str(),"faces",e.faces);
                if(!e.levels.empty()){RenderSnapshot_RecordU64("DDS_RESIDENCY",item.first.c_str(),"source_width",e.levels[0].width);RenderSnapshot_RecordU64("DDS_RESIDENCY",item.first.c_str(),"source_height",e.levels[0].height);}
                RenderSnapshot_RecordU64("DDS_RESIDENCY",item.first.c_str(),"retry_epoch",e.retry);RenderSnapshot_RecordU64("DDS_RESIDENCY",item.first.c_str(),"failure_kind",e.reason);
                RenderSnapshot_RecordU64("DDS_RESIDENCY",item.first.c_str(),"hresult",static_cast<uint32_t>(e.failure));}
            RenderSnapshot_Value("TIMING","cpu_prepare_ms",input.prepare_ms);RenderSnapshot_Value("TIMING","cpu_native_submit_ms",double(submitted.QuadPart-start.QuadPart)*1000.0/double(frequency.QuadPart));
            RenderSnapshot_Value("TIMING","cpu_frame_plan_ms",plan_ms);RenderSnapshot_Value("TIMING","cpu_texture_preflight_ms",preflight_ms);
            RenderSnapshot_Value("TIMING","cpu_present_ms",double(end.QuadPart-submitted.QuadPart)*1000.0/double(frequency.QuadPart));RenderSnapshot_Value("TIMING","cpu_backend_telemetry_record_ms",double(native.telemetry_cpu_ticks)*1000.0/double(frequency.QuadPart));
            RenderSnapshot_Text("TIMING","cpu_native_submit_scope","INCLUDES_SCENE_INIT_UPLOADS_AND_TELEMETRY; EXCLUDES_PRESENT");
            if(hr<0){RenderSnapshot_GPUTiming(RenderSnapshot_CaptureID(),input.engine_frame,RenderSnapshot_RenderFrameID(),"CANCELLED",NULL,0);ForgetTiming(RenderSnapshot_CaptureID(),input.engine_frame,RenderSnapshot_RenderFrameID());RenderSnapshot_End("GPU_SUBMISSION_FAILED");}
        }
        if(hr<0){Write("[R4 native B5] frame failed: step=%s HRESULT=0x%08X shader=%s",c.frame.step?c.frame.step:"present",static_cast<uint32_t>(hr),c.level.bridge.shader_error);return false;}
        const bool eligible=hr==0 && c.loaded && input.allowed && input.active && !input.warmup &&
            !input.observation.paused && !input.observation.console_visible && !tracing && !native.debug && !screenshot;
        if(measure)kich_r4_phases::Global().MarkEligible(input.engine_frame,eligible);
        if(eligible && c.warmup_frames){if(native.record.active)native.record.Emit("[R4 E1.6 warmup] precache_scene_frames=%u DDS_loaded=%u resident_bytes=%llu; actual_view_union; loading_UI_preserved; no_GPU_wait",
            c.warmup_frames,c.level.textures_loaded,static_cast<unsigned long long>(c.level.texture_registry.bytes));c.warmup_frames=0;}
        if(native.record.active){
            const int64_t collect_started=kich_r4_phases::Counter();
            c.metrics.Add(end.QuadPart,frequency.QuadPart,eligible);
            KichR4ResourceStats resources={};const bool stats_ok=native.api.resources.get_stats(c.frame.device,&resources,sizeof(resources))==0;
            const uint32_t created=stats_ok?resources.created-c.last_created:0,released=stats_ok?resources.released-c.last_released:0;
            const double submit_ms=double(submitted.QuadPart-start.QuadPart)*1000.0/double(frequency.QuadPart);
            const double present_ms=double(end.QuadPart-submitted.QuadPart)*1000.0/double(frequency.QuadPart);
            if(eligible){c.created_window+=created;c.released_window+=released;c.meshes_window+=c.deferred.native_models.mesh_uploads;
                if(!c.peak.frame || submit_ms+input.prepare_ms>c.peak.submit+c.peak.queue){
                    const auto& detail=c.deferred.details;const auto& probe=c.level.preflight_probe;
                    c.peak.frame=input.engine_frame;memcpy(c.peak.camera,input.position,sizeof(c.peak.camera));c.peak.queue=input.prepare_ms;c.peak.submit=submit_ms;c.peak.plan=plan_ms;c.peak.preflight=preflight_ms;c.peak.model=c.deferred.model_prepare_ms;
                    c.peak.bounds=detail.shadow_bounds_ms;c.peak.bvh_build=detail.shadow_bvh_build_ms;c.peak.bvh_refit=detail.shadow_bvh_refit_ms;c.peak.sun_select=detail.shadow_sun_select_ms;c.peak.local_select=detail.shadow_local_select_ms;
                    c.peak.detail_bounds=detail.shadow_bounds_built;c.peak.io=probe.io_ms;c.peak.parse=probe.parse_ms;c.peak.upload=probe.upload_ms;c.peak.opens=probe.opens;c.peak.uploads=probe.uploads;c.peak.created=created;c.peak.lights=input.lighting.count;c.peak.shadow_faces=c.deferred.frame_local_shadow_faces;
                }
                c.submit_peak_ms=(std::max)(c.submit_peak_ms,submit_ms);}
            const bool world_current=c.loaded && scene_requested && hr==0 && c.deferred.shadow_record.valid && c.deferred.shadow_record.frame==input.engine_frame;
            if(c.loaded && scene_requested && hr==0){
                if(watch_allowed){kich_r4_watch::Global().Finish(c.deferred.base);watch_guard.complete=true;}
                native.record.Observe(input,c.deferred.shadow_record,end.QuadPart,native.level_generation,native.device_generation,
                    (input.warmup?1u:0u)|(tracing?2u:0u)|(native.debug?4u:0u)|(input.observation.paused?8u:0u)|(input.observation.console_visible?16u:0u)|(!input.active?32u:0u)|(screenshot?64u:0u));
            }else native.record.Gap();
            native.record.last_frame=input.engine_frame;
            native.record.collect_ms+=double(kich_r4_phases::Counter()-collect_started)*1000.0/double(frequency.QuadPart);
            if(native.record.ReportDue(end.QuadPart)){
                const int64_t emit_started=kich_r4_phases::Counter();
                const auto& observation=input.observation;
                native.record.Emit("[R4 E1.7 camera] session=%llu frame=%u record_ms=%.3f level=%.96s device_generation=%llu level_generation=%llu camera=[%.3f %.3f %.3f] direction=[%.4f %.4f %.4f] fov=%.3f detail_radius=%.1f demo_effector=%u paused=%u console=%u view=%u normal_performance_frame=%u warmup=%u snapshot=%u screenshot=%u",
                    static_cast<unsigned long long>(native.record.epoch),input.engine_frame,native.record.Elapsed(end.QuadPart),c.name.c_str(),
                    static_cast<unsigned long long>(native.device_generation),static_cast<unsigned long long>(native.level_generation),input.position[0],input.position[1],input.position[2],
                    observation.camera_direction[0],observation.camera_direction[1],observation.camera_direction[2],input.geometry_lod[3],observation.detail_radius,
                    unsigned(observation.demo_active),unsigned(observation.paused),unsigned(observation.console_visible),native.debug,unsigned(eligible),unsigned(input.warmup),unsigned(tracing),unsigned(screenshot));
                native.record.Emit("[R4 E1.7 entities] frame=%u metadata_valid=%u current_valid=%u current_id=%u current_position=[%.3f %.3f %.3f] view_valid=%u view_id=%u view_position=[%.3f %.3f %.3f]; current_entity_is_not_assumed_actor; demo_camera_may_differ",
                    input.engine_frame,unsigned(observation.valid),unsigned(observation.current_entity_valid),observation.current_entity_id,
                    observation.current_entity_position[0],observation.current_entity_position[1],observation.current_entity_position[2],unsigned(observation.view_entity_valid),observation.view_entity_id,
                    observation.view_entity_position[0],observation.view_entity_position[1],observation.view_entity_position[2]);
                kich_r4_phases::Report phase_report;kich_r4_phases::Global().TakeReport(phase_report);
                kich_r4_update::EmitReport(native.record);
                native.record.Emit("[R4 E1.7 cpu] completed_frames=%u cycle_mean_ms=%.3f cycle_min_ms=%.3f cycle_max_ms=%.3f device_mean_ms=%.3f message_gap_mean_ms=%.3f frames_ge_16_667ms=%u frames_ge_33_333ms=%u frames_ge_50ms=%u slow_retained=%u; completed_previous_normal_frames; CPU_wall_clock; nested_spans_not_additive",
                    phase_report.samples,phase_report.samples?phase_report.Milliseconds(phase_report.total_cycle_ticks)/phase_report.samples:0,
                    phase_report.Milliseconds(phase_report.cycle_min_ticks),phase_report.Milliseconds(phase_report.cycle_max_ticks),
                    phase_report.samples?phase_report.Milliseconds(phase_report.total_device_ticks)/phase_report.samples:0,
                    phase_report.samples?phase_report.Milliseconds(phase_report.total_gap_ticks)/phase_report.samples:0,
                    phase_report.at_least_60hz,phase_report.at_least_30hz,phase_report.at_least_20hz,phase_report.stored);
                const char* phase_names[]={"frame_move","render","worker_wait","worker_fallback","native_prelude","native_submit","present","post_present"};
                for(unsigned i=0;i<kich_r4_phases::PhaseCount;++i)native.record.Emit("[R4 E1.7 cpu_phase] name=%s samples=%u mean_ms=%.3f max_ms=%.3f; render_contains_native_spans",
                    phase_names[i],phase_report.phase_samples[i],phase_report.phase_samples[i]?phase_report.Milliseconds(phase_report.phase_ticks[i])/phase_report.phase_samples[i]:0,phase_report.Milliseconds(phase_report.phase_max_ticks[i]));
                for(unsigned i=0;i<phase_report.stored;++i){const kich_r4_phases::Sample& p=phase_report.slow[i];
                    native.record.Emit("[R4 E1.6 hitch] frame=%u cycle_ms=%.3f device_ms=%.3f message_gap_ms=%.3f frame_move_ms=%.3f render_ms=%.3f worker_wait_ms=%.3f worker_fallback_ms=%.3f native_prelude_ms=%.3f native_submit_ms=%.3f present_ms=%.3f post_present_ms=%.3f measured_mask=0x%X",
                        p.frame,p.Milliseconds(p.CycleTicks()),p.Milliseconds(p.device_ticks),p.Milliseconds(p.gap_ticks),p.PhaseMS(kich_r4_phases::FrameMove),p.PhaseMS(kich_r4_phases::Render),p.PhaseMS(kich_r4_phases::WorkerWait),p.PhaseMS(kich_r4_phases::WorkerFallback),p.PhaseMS(kich_r4_phases::NativePrelude),p.PhaseMS(kich_r4_phases::NativeSubmit),p.PhaseMS(kich_r4_phases::Present),p.PhaseMS(kich_r4_phases::PostPresent),p.measured);
                }
                KichR4_ReportLightingTransitions(input.engine_frame,RecordOutput);
                const double projection_a=input.camera_projection[10],projection_b=input.camera_projection[14];
                const bool projection_valid=kich_r4_lighting::Finite(float(projection_a)) && kich_r4_lighting::Finite(float(projection_b)) && projection_a>1.0 && projection_b<0;
                native.record.Emit("[R4 E1.9 projection] frame=%u valid=%u near_m=%.7g far_m=%.7g depth_a=%.9g depth_b=%.9g; source=Device_mProject; receiver=%s; camera_relative; D24_kept_for_visibility_and_stencil",
                    input.engine_frame,unsigned(projection_valid),projection_valid?-projection_b/projection_a:0,projection_valid?-projection_b/(projection_a-1):0,projection_a,projection_b,
                    !world_current?"NO_CURRENT_WORLD_FRAME":c.deferred.base.receiver_inverse.m[15]!=0?"LINEAR_CLIP_W_R32F":"D24_ORTHOGRAPHIC_COMPATIBILITY");
                kich_r4_normal_probe::EmitReport(native.record,input.engine_frame,world_current);
                native.record.Emit("[R4 E1.8 projectors] frame=%u current_world=%u spots=%u named=%u ready=%u fallback=%u; selected_opaque_lights_once; named_unavailable_uses_analytic_light; not_visible_pixels",
                    input.engine_frame,unsigned(world_current),world_current?c.deferred.frame_projector_spots:0,world_current?c.deferred.frame_projector_named:0,
                    world_current?c.deferred.frame_projector_ready:0,world_current?c.deferred.frame_projector_fallback:0);
                const auto& hud=c.deferred.native_models;
                native.record.Emit("[R4 E1.8 HUD] frame=%u current_world=%u recorded_draws=%llu valid_exposure_draws=%llu mapped_draws=%llu unlit_draws=%llu hemi_min=%.6g hemi_max=%.6g local_add_draws=%u; visual_part_submissions_not_pixels; no_per_bone_light_queries",
                    input.engine_frame,unsigned(world_current),static_cast<unsigned long long>(world_current?hud.hud_recorded_draws:0),
                    static_cast<unsigned long long>(world_current?hud.hud_exposure_valid_draws:0),static_cast<unsigned long long>(world_current?hud.hud_mapped_draws:0),
                    static_cast<unsigned long long>(world_current?hud.hud_unlit_draws:0),world_current && hud.hud_exposure_valid_draws?hud.hud_hemi_min:0,
                    world_current && hud.hud_exposure_valid_draws?hud.hud_hemi_max:0,world_current?hud.draw_calls[KichR4ModelSession::HUDAdd]:0);
                native.record.Emit("[R4 E1.7 status] frame=%u fps_current=%.2f fps_recent=%.2f present_interval_ms=%.3f recent_samples=%u recent_ms=%.0f shadow_submission=%s sun_cascades=%u spot_shadow_lights=%u point_shadow_lights=%u local_shadow_faces=%u; FPS_normal_frames_only; CPU_submission_not_visible_pixels; report_period_ms=2000",
                    input.engine_frame,eligible?c.metrics.Current():0,c.metrics.Recent(),eligible?c.metrics.interval:0,c.metrics.report_count,c.metrics.report_ms,
                    !world_current?"NO_CURRENT_WORLD_FRAME":c.deferred.frame_sun_cascades || c.deferred.frame_local_shadow_faces?"ON":"NONE",world_current?c.deferred.frame_sun_cascades:0,world_current?c.deferred.frame_spot_shadow_lights:0,world_current?c.deferred.frame_point_shadow_lights:0,world_current?c.deferred.frame_local_shadow_faces:0);
                native.record.Emit("[R4 E1.7 frame] frame=%u normal_performance_frame=%u present_interval_1pct_low_fps=%.2f CPU_queue_ms=%.3f CPU_submit_ms=%.3f present_ms=%.3f created_frame=%u released_frame=%u stats_ok=%u shadow_passes=%u; GPU_times_reported_separately_by_original_frame",
                    input.engine_frame,unsigned(eligible),c.metrics.Low(),input.prepare_ms,submit_ms,present_ms,created,released,unsigned(stats_ok),c.deferred.shadow_passes-shadows_before);
                native.record.Emit("[R4 E1.7 stability] normal_created=%llu normal_released=%llu normal_mesh_uploads=%llu normal_submit_peak_ms=%.3f model_prepare_current_ms=%.3f GPU_payload_bytes=%llu live_resources=%u DDS_bytes=%llu DDS_budget=%llu; payload_not_VRAM; interval_window=120; low=reciprocal_mean_slowest_ceil_1pct",
                    static_cast<unsigned long long>(c.created_window),static_cast<unsigned long long>(c.released_window),static_cast<unsigned long long>(c.meshes_window),c.submit_peak_ms,world_current?c.deferred.model_prepare_ms:0,
                    static_cast<unsigned long long>(resources.payload_bytes),resources.live_resources,static_cast<unsigned long long>(c.level.texture_registry.bytes),static_cast<unsigned long long>(c.level.texture_registry.budget));
                native.record.Emit("[R4 E1.8 resource_classes] frame=%u model_meshes=%llu model_vertex_bytes=%llu model_source_index_bytes=%llu model_partition_buffers=%llu model_partition_bytes=%llu partition_created_current=%llu pose_capacity_bytes=%llu DDS_bytes=%llu; registered_payload_not_VRAM; immutable_LOD_ranges_share_partitions",
                    input.engine_frame,static_cast<unsigned long long>(c.deferred.native_models.meshes.size()),
                    static_cast<unsigned long long>(c.deferred.native_models.vertex_bytes),
                    static_cast<unsigned long long>(c.deferred.native_models.index_bytes-c.deferred.native_models.basis_bytes),
                    static_cast<unsigned long long>(c.deferred.native_models.basis_buffers),static_cast<unsigned long long>(c.deferred.native_models.basis_bytes),
                    static_cast<unsigned long long>(world_current?c.deferred.native_models.basis_creations:0),
                    static_cast<unsigned long long>(c.deferred.native_models.AllocatedPoseBytes()),static_cast<unsigned long long>(c.level.texture_registry.bytes));
                if(c.peak.frame){const auto& peak=c.peak;
                    native.record.Emit("[R4 E1.7 heavy_frame] frame=%u camera=[%.3f %.3f %.3f] CPU_queue_ms=%.3f submit_ms=%.3f plan_ms=%.3f preflight_ms=%.3f model_prepare_ms=%.3f detail_bounds=%llu bounds_ms=%.3f BVH_build_ms=%.3f BVH_refit_ms=%.3f sun_select_ms=%.3f local_select_ms=%.3f io_ms=%.3f parse_ms=%.3f upload_api_ms=%.3f opens=%llu uploads=%llu created=%u lights=%u local_shadow_faces=%u; maximum_queue_plus_submit_in_normal_window; nested_not_additive",
                        peak.frame,peak.camera[0],peak.camera[1],peak.camera[2],peak.queue,peak.submit,peak.plan,peak.preflight,peak.model,static_cast<unsigned long long>(peak.detail_bounds),peak.bounds,peak.bvh_build,peak.bvh_refit,peak.sun_select,peak.local_select,peak.io,peak.parse,peak.upload,static_cast<unsigned long long>(peak.opens),static_cast<unsigned long long>(peak.uploads),peak.created,peak.lights,peak.shadow_faces);
                }
                if(world_current){
                native.record.Emit("[R4 E1.3 work] lights=%u light_volume_queries=%u occluded_lights=%u static_view_queries=%u static_view_reuses=%u static_leaf_tests=%llu static_node_tests=%llu forward_sorts=%u frame_plan_ms=%.3f texture_preflight_ms=%.3f",
                    input.lighting.count,input.lighting.occlusion_tested,static_cast<unsigned>(input.lighting.occlusion_rejected.size()),c.deferred.visibility_plan.queries,c.deferred.visibility_plan.cache_hits,
                    static_cast<unsigned long long>(c.deferred.visibility_plan.bounds_tests),static_cast<unsigned long long>(c.deferred.visibility_plan.node_tests),c.deferred.forward_sorts,plan_ms,preflight_ms);
                const auto& details=c.deferred.details;
                native.record.Emit("[R4 E1.7 details] shadow_bounds=%llu shadow_queries=%llu shadow_reuses=%llu leaf_tests=%llu node_tests=%llu index_builds=%u index_refits=%u rebuild_reasons=0x%X bounds_ms=%.3f BVH_build_ms=%.3f BVH_refit_ms=%.3f sun_select_ms=%.3f local_select_ms=%.3f; CPU_current_frame; reasons=1_source,2_count,4_order_origin,8_bounds,16_empty_or_invalid,32_no_topology,64_cell_layout",
                    static_cast<unsigned long long>(details.shadow_bounds_built),static_cast<unsigned long long>(details.shadow_queries),static_cast<unsigned long long>(details.shadow_cache_hits),
                    static_cast<unsigned long long>(details.shadow_visibility_tests),static_cast<unsigned long long>(details.shadow_node_tests),details.shadow_index_builds,details.shadow_index_refits,details.shadow_rebuild_reasons,
                    details.shadow_bounds_ms,details.shadow_bvh_build_ms,details.shadow_bvh_refit_ms,details.shadow_sun_select_ms,details.shadow_local_select_ms);
                native.record.Emit("[R4 E1.8 detail_index] frame=%u spatial_unit=%s cells=%llu; packed_order_is_not_cell_identity",
                    input.engine_frame,details.shadow_cell_mode?"CACHE_CELL":"LEGACY_INSTANCE",static_cast<unsigned long long>(details.shadow_cells.size()));
                }else native.record.Emit("[R4 E1.7 work] frame=%u state=NO_CURRENT_WORLD_FRAME; scene_work_not_reused_from_previous_frame",input.engine_frame);
                const auto& preflight=c.level.preflight_probe;
                native.record.Emit("[R4 E1.7 preflight] measured=%u traversal_ms=%.3f planning_ms=%.3f commit_ms=%.3f io_ms=%.3f parse_ms=%.3f upload_api_ms=%.3f opens=%llu open_failures=%llu source_bytes=%llu prepared_hits=%llu prepared_bytes=%llu metadata_hits=%llu retries_skipped=%llu resident_hits=%llu uploads=%llu uploaded_bytes=%llu upload_failures=%llu evictions=%llu; traversal_commit_include_nested_IO_parse_upload; opens_are_logical_sources",
                    unsigned(preflight.measured),preflight.traversal_ms,preflight.planning_ms,preflight.commit_ms,preflight.io_ms,preflight.parse_ms,preflight.upload_ms,
                    static_cast<unsigned long long>(preflight.opens),static_cast<unsigned long long>(preflight.open_failures),static_cast<unsigned long long>(preflight.source_bytes),
                    static_cast<unsigned long long>(preflight.prepared_hits),static_cast<unsigned long long>(preflight.prepared_bytes),static_cast<unsigned long long>(preflight.metadata_hits),static_cast<unsigned long long>(preflight.retry_skips),static_cast<unsigned long long>(preflight.resident_hits),
                    static_cast<unsigned long long>(preflight.uploads),static_cast<unsigned long long>(preflight.upload_bytes),static_cast<unsigned long long>(preflight.upload_failures),static_cast<unsigned long long>(preflight.evictions));
                {const KichR4LevelSession::AsyncCounters& a=c.level.async_window;const kich_r4_texture_io::Stats io=texture_io?texture_io->Snapshot():kich_r4_texture_io::Stats();
                    native.record.Emit("[R4 P0-S11 dds_async] enabled=%u requests=%llu staged=%llu staged_bytes=%llu waiting=%llu transitional_draws=%llu sync_fallbacks=%llu worker_reads=%llu worker_failures=%llu worker_bytes=%llu worker_read_ms_total=%.3f queued=%u ready=%u ready_bytes=%llu expired=%llu; window_since_last_report; worker_time_off_frame",
                        unsigned(c.level.texture_io!=NULL),static_cast<unsigned long long>(a.requests),static_cast<unsigned long long>(a.staged),static_cast<unsigned long long>(a.staged_bytes),
                        static_cast<unsigned long long>(a.waiting),static_cast<unsigned long long>(a.transitional),static_cast<unsigned long long>(a.failures),
                        static_cast<unsigned long long>(io.completed),static_cast<unsigned long long>(io.failed),static_cast<unsigned long long>(io.bytes),io.read_ms,
                        unsigned(io.queued),unsigned(io.ready),static_cast<unsigned long long>(io.ready_bytes),static_cast<unsigned long long>(io.expired));
                    c.level.async_window=KichR4LevelSession::AsyncCounters();}
                {kich_r4_queue::Profile& q=kich_r4_queue::Global(); // P0-S11 PERF02 queue stages
                    if(q.frames){char text[1200];int used=snprintf(text,sizeof(text),"[R4 P0-S11 queue] frames=%u",q.frames);
                        for(unsigned s=0;s<kich_r4_queue::StageCount && used>0 && size_t(used)<sizeof(text);++s){const int n=snprintf(text+used,sizeof(text)-used," %s_ms=%.3f/%.3f",kich_r4_queue::StageName(s),q.sum[s]/q.frames,q.peak[s]);if(n<0)break;used+=n;}
                        for(unsigned k=0;k<kich_r4_queue::CounterCount && used>0 && size_t(used)<sizeof(text);++k){const int n=snprintf(text+used,sizeof(text)-used," %s=%.1f",kich_r4_queue::CounterName(k),double(q.count_sum[k])/q.frames);if(n<0)break;used+=n;}
                        native.record.Emit("%s; stage_ms=mean/max_per_frame; counters=mean_per_frame; model_capture_and_stream_capture_are_nested_in_the_other_stages_not_additive",text);}
                    q.Reset();}
                if(world_current){const auto& shadows=c.deferred.shadow_record;
                for(unsigned cascade=0;cascade<3;++cascade){const auto& pass=shadows.sun[cascade]; // 2 = P0-S2 contact
                    native.record.Emit("[R4 E1.7 sun] frame=%u cascade=%u valid=%u begun=%u completed=%u map=%llu map_generation=%llu resolution=%u span_m=%.3f texel_m=%.5f; world_to_shadow; blend_near_clip_edge=0.9..1.0; point_comparison_valid_plane_9tap_invalid_plane_1tap_PCF",
                        input.engine_frame,cascade,unsigned(shadows.valid && shadows.frame==input.engine_frame && scene_requested),unsigned(pass.begun),unsigned(pass.completed),static_cast<unsigned long long>(pass.map),static_cast<unsigned long long>(pass.generation),pass.resolution,pass.span,pass.texel_size);
                    native.record.Emit("[R4 E1.7 sun_matrix] frame=%u cascade=%u m=[%.9g %.9g %.9g %.9g; %.9g %.9g %.9g %.9g; %.9g %.9g %.9g %.9g; %.9g %.9g %.9g %.9g]",
                        input.engine_frame,cascade,pass.matrix[0],pass.matrix[1],pass.matrix[2],pass.matrix[3],pass.matrix[4],pass.matrix[5],pass.matrix[6],pass.matrix[7],pass.matrix[8],pass.matrix[9],pass.matrix[10],pass.matrix[11],pass.matrix[12],pass.matrix[13],pass.matrix[14],pass.matrix[15]);
                }
                const char* kinds[]={"static_draw_entries","model_visual_parts","detail_instances","compatibility_commands"};
                for(unsigned k=0;k<kich_r4_shadow_record::KindCount;++k){const auto& near_work=shadows.sun[0].work[k];const auto& far_work=shadows.sun[1].work[k];const auto& local=shadows.local[k];
                    native.record.Emit("[R4 E1.7 casters] frame=%u unit=%s near_draws=%llu near_casters=%llu near_indices=%llu far_draws=%llu far_casters=%llu far_indices=%llu local_draws=%llu local_casters=%llu local_indices=%llu unknown_sun_ids=%llu; not_unique_objects_or_visible_pixels",
                        input.engine_frame,kinds[k],static_cast<unsigned long long>(near_work.draws),static_cast<unsigned long long>(near_work.casters),static_cast<unsigned long long>(near_work.indices),static_cast<unsigned long long>(far_work.draws),static_cast<unsigned long long>(far_work.casters),static_cast<unsigned long long>(far_work.indices),
                        static_cast<unsigned long long>(local.draws),static_cast<unsigned long long>(local.casters),static_cast<unsigned long long>(local.indices),static_cast<unsigned long long>(near_work.unknown_ids+far_work.unknown_ids));
                }
                native.record.Emit("[R4 E1.7 local_shadow] frame=%u attempted_faces=%u completed_faces=%u resolution=%u pool_revision=%llu; local_identity_signatures_not_measured",
                    input.engine_frame,shadows.local_attempted,shadows.local_completed,shadows.local_resolution,static_cast<unsigned long long>(shadows.local_generation));
                }
                native.record.TraceReport();kich_r4_watch::Global().Report(static_cast<uint32_t>(native.record.Elapsed(end.QuadPart)),RecordOutput);
                native.record.emit_ms+=double(kich_r4_phases::Counter()-emit_started)*1000.0/double(frequency.QuadPart);
                native.record.Emit("[R4 E1.7 recorder_cost] session=%llu lines=%llu bytes=%llu collection_window_ms=%.3f periodic_emit_window_ms=%.3f; partial_CPU_overhead:watch_selection_finish_trace_stats_and_periodic_format_enqueue; excludes_per_draw_signature_hooks_GPU_queries_and_background_disk; no_flush_or_readback; not_zero_cost",
                    static_cast<unsigned long long>(native.record.epoch),static_cast<unsigned long long>(native.record.lines),static_cast<unsigned long long>(native.record.bytes),native.record.collect_ms,native.record.emit_ms);
                native.record.collect_ms=native.record.emit_ms=0;c.created_window=c.released_window=c.meshes_window=0;c.submit_peak_ms=0;c.peak=WorkPeak();c.metrics.Reported();
            }
            if(stats_ok){c.last_created=resources.created;c.last_released=resources.released;}
        }
        {std::lock_guard<std::mutex> lock(runtime_mutex);std::set<std::string> alive;for(const auto& item:runtime)alive.insert(item.second.key);retired.clear();
            for(auto it=c.uploads.begin();it!=c.uploads.end();){if(!alive.count(it->first)){if(it->second.handle){c.level.bridge.resources.release_resource(c.frame.device,it->second.handle);++native.runtime_releases;}it=c.uploads.erase(it);}else ++it;}}
        snapshot_guard.complete=true;return true;
    }catch(const std::exception& e){Write("[R4 native B5] frame exception: %s",e.what());return false;}
}
void KichR4_NativeDestroy(){StopContext();delete texture_io;texture_io=NULL;if(native.module){FreeLibrary(native.module);native.module=NULL;}native.desired.clear();memset(&native.telemetry,0,sizeof(native.telemetry));}
