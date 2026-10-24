#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "xr_3da/KichR4RecordControl.h"
#include "xr_3da/KichR4RecordTiming.h"
#include "xr_3da/KichR4Game.h"
static bool g_render_snapshot_active=false,snapshot_detailed=false;
static uint64_t snapshot_id=7,snapshot_render=11;
static unsigned poll_calls=0,set_calls=0;
static unsigned profile_calls=0;
static int32_t poll_error=0,set_error=0;
static KichR4TelemetryTiming delivered={};
static KichR4TelemetryContext submitted_capture={};
struct SnapshotResult {uint64_t id,engine,render;std::string status;};
static std::vector<SnapshotResult> snapshot_results;
static std::vector<std::string> record_lines;
static bool RenderSnapshot_Detailed(){return snapshot_detailed;}
static uint64_t RenderSnapshot_CaptureID(){return snapshot_id;}
static uint64_t RenderSnapshot_RenderFrameID(){return snapshot_render;}
static void RenderSnapshot_GPUTiming(uint64_t id,uint64_t engine,uint64_t render,const char* status,const double*,uint32_t){
    const SnapshotResult result={id,engine,render,status};snapshot_results.push_back(result);
}
static void RenderSnapshot_Text(const char*,const char*,const char*){}
static void RenderSnapshot_U64(const char*,const char*,uint64_t){}
static void KICH_R4_CALL TelemetryRecord(const KichR4TelemetryRecord*){}
static int32_t KICH_R4_CALL SetCapture(KichR4Handle,const KichR4TelemetryContext* c,KichR4TelemetrySink){++set_calls;if(c)submitted_capture=*c;return set_error;}
static int32_t KICH_R4_CALL PollCapture(KichR4Handle,KichR4TelemetryTiming* result,uint32_t){++poll_calls;*result=delivered;delivered.status=0;return poll_error;}
struct Record {
    bool active,due;uint64_t epoch;
    Record():active(false),due(true),epoch(0){}
    bool GPUDue(int64_t){const bool value=due;due=false;return value;}
    double Elapsed(int64_t time)const{return double(time);}
    void Emit(const char* format,...){char text[2048];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);record_lines.push_back(text);}
};
struct KichR4FrameSession {KichR4Handle device;KichR4FrameSession():device(1){}};
static void ProfileMark(KichR4Handle,unsigned){++profile_calls;}
struct Context {
    KichR4FrameSession frame;
    struct Deferred {
        bool native_mode=true,ordinary_profile=false,profile_context_valid=true;
        struct Tools {void (*profile_mark)(KichR4Handle,unsigned)=ProfileMark;} tools;
        // ACTUAL_DEFERRED_PROFILE_MARK
    } deferred;
};
struct Native {
    Context* context;
    KichR4TelemetryAPI telemetry;
    kich_r4_record::TimingQueue timings;
    Record record;
    uint64_t timing_error_epoch=UINT64_MAX,timing_error_device_generation=UINT64_MAX,timing_error_level_generation=UINT64_MAX;
    uint64_t current_engine_frame,device_generation,level_generation,present_frame,telemetry_cpu_ticks;
    uint32_t debug;
    struct API {struct Build {const char* revision;const char* source_id;const char* shader_id;const char* compiled;Build():revision("test"),source_id("test"),shader_id("test"),compiled("test"){}} build;} api;
    Native():context(NULL),telemetry(),current_engine_frame(0),device_generation(1),level_generation(1),present_frame(0),telemetry_cpu_ticks(0),debug(0){}
} native;
static void Write(const char* format,...){char text[2048];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);record_lines.push_back(text);}
static void Reset(Context& context){
    native=Native();native.context=&context;context.deferred=Context::Deferred();
    native.telemetry.set_capture=SetCapture;native.telemetry.poll_timing=PollCapture;
    kich_r4_record::Switch()=kich_r4_record::Control();
    snapshot_results.clear();record_lines.clear();poll_calls=set_calls=profile_calls=0;poll_error=set_error=0;
    g_render_snapshot_active=snapshot_detailed=false;snapshot_id=7;snapshot_render=11;
    delivered=KichR4TelemetryTiming();submitted_capture=KichR4TelemetryContext();
}
static KichR4TelemetryContext Pending(uint64_t id,uint64_t frame=41,uint64_t render=43){KichR4TelemetryContext c={};c.struct_size=sizeof(c);c.capture_id=id;c.engine_frame=frame;c.render_frame=render;return c;}
static KichR4TelemetryTiming Result(const KichR4TelemetryContext& c){KichR4TelemetryTiming r={};r.struct_size=sizeof(r);r.status=KICH_R4_TIMING_READY;r.capture_id=c.capture_id;r.engine_frame=c.engine_frame;r.render_frame=c.render_frame;r.valid_mask=127;for(unsigned i=0;i<7;++i)r.milliseconds[i]=double(i+1);return r;}
static void Enable(uint64_t epoch){kich_r4_record::Request(true);kich_r4_record::BeginFrame(1);native.record.active=true;native.record.epoch=epoch;}
