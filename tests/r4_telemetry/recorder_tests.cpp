#include "../../xr_3da/RenderSnapshot.h"
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <limits>
#include <thread>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <math.h>
namespace {
unsigned checks=0;
void Require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::string Read(const std::string& path){std::ifstream file(path.c_str());std::ostringstream out;out<<file.rdbuf();return out.str();}
std::string Section(const std::string& report,const std::string& name){const std::string heading="["+name+"]\n";const size_t begin=report.find(heading);
    if(begin==std::string::npos)throw std::runtime_error("Missing "+name);
    const size_t end=report.find("\n[",begin+heading.size());return report.substr(begin,end==std::string::npos?end:end-begin);}
bool Has(const std::string& section,const std::string& key,const std::string& value){return section.find("\n"+key+"="+value+"\n")!=std::string::npos;}
uint64_t Number(const std::string& section,const std::string& key){const std::string prefix="\n"+key+"=";const size_t at=section.find(prefix);if(at==std::string::npos)throw std::runtime_error("Missing number "+key);return std::stoull(section.substr(at+prefix.size()));}
RenderSnapshotInfo info={};uint64_t render=100;
void State(const char* screen="GAME",const char* root="GAME",bool paused=false,bool loaded=true,bool console=false){++render;RenderSnapshot_FrameIDs(render,render-1,1,1);RenderSnapshot_UIState(screen,root,root,loaded,console,paused);info.paused=paused?1:0;info.console_visible=console?1:0;}
std::string Capture(const char* status="COMPLETE"){RenderSnapshot_Begin(info);Require(g_render_snapshot_active,"frame starts when requested");RenderSnapshot_End(status);return Read(RenderSnapshot_LastPath());}
RenderSnapshotDraw Draw(){RenderSnapshotDraw d={};d.struct_size=sizeof(d);d.submitted=1;d.topology=4;d.index_count=6;d.instance_count=3;return d;}
}
int main(int argc,char** argv){try{
    Require(argc==2,"output directory required");strncpy(info.directory,argv[1],sizeof(info.directory)-1);strcpy(info.renderer,"R4");strcpy(info.api,"D3D11");strcpy(info.scene,"GAME");info.frame=17;info.width=1280;info.height=720;
    State();Require(!RenderSnapshot_DiagnosticActive() && !RenderSnapshot_Wanted(),"ordinary frames do not arm diagnostics");
    Require(RenderSnapshot_RequestReason("HOTKEY"),"manual hotkey request accepted");RenderSnapshot_Begin(info);
    const uint64_t capture_id=RenderSnapshot_CaptureID(),capture_render=RenderSnapshot_RenderFrameID();
    RenderSnapshot_U64("MEMORY","exact_unsigned",std::numeric_limits<uint64_t>::max());RenderSnapshot_I64("MEMORY","exact_signed",std::numeric_limits<int64_t>::min());
    RenderSnapshot_RecordU64("RESOURCE","extreme","resource_id",std::numeric_limits<uint64_t>::max());RenderSnapshot_RecordI64("RESOURCE","extreme","base_vertex",std::numeric_limits<int64_t>::min());
    RenderSnapshot_FrameIDs(~uint64_t(0),~uint64_t(0),3,4);Require(RenderSnapshot_RenderFrameID()==capture_render,"generation update cannot overwrite active render identity");
    RenderSnapshot_PresentID(42,true);RenderSnapshot_End("COMPLETE");std::string path=RenderSnapshot_LastPath(),report=Read(path);
    Require(Number(Section(report,"TELEMETRY_COST"),"capture_id")==capture_id && Section(report,"TELEMETRY_COST").find("report_payload_write_flush_ms=")!=std::string::npos,"serialization and payload flush costs are measured separately");
    Require(Has(Section(report,"SNAPSHOT"),"schema","3") && Has(Section(report,"SNAPSHOT"),"comparable_schema","2"),"versioned schema retains comparable predecessor counters");
    Require(Has(Section(report,"MEMORY"),"exact_unsigned","18446744073709551615") && Has(Section(report,"MEMORY"),"exact_signed","-9223372036854775808"),"integer writers never round through double");
    Require(Has(Section(report,"RECORD RESOURCE"),"resource_id","18446744073709551615") && Has(Section(report,"RECORD RESOURCE"),"base_vertex","-9223372036854775808"),"dictionary integer writers preserve extreme values");
    Require(Has(Section(report,"FRAME_IDENTITY"),"present_frame","42") && Has(Section(report,"FRAME_IDENTITY"),"present_status","SUBMITTED"),"present ordinal comes from actual successful presentation");
    Require(Has(Section(report,"FRAME_IDENTITY"),"request_reason","HOTKEY") && Number(Section(report,"FRAME_IDENTITY"),"capture_id")==capture_id,"request reason and exact identity recorded");
    double values[7]={1.25,0,0,0,0,0,0};Require(!RenderSnapshot_GPUTiming(capture_id,info.frame,capture_render+1,"READY",values,1),"later render frame may not attach timing to earlier capture");
    Require(!RenderSnapshot_GPUTiming(capture_id,info.frame,capture_render,"NOT_READY",NULL,1),"closed capture not-ready never fabricates a final result");
    Require(RenderSnapshot_GPUTiming(capture_id,info.frame,capture_render,"READY",values,1),"exact asynchronous completion can append timing only");
    std::string completed=Read(path);Require(completed.compare(0,report.size(),report)==0 && completed.size()>report.size(),"asynchronous result preserves closed graphics bytes");
    Require(Has(Section(completed,"GPU_TIMING_COMPLETION"),"stage0_milliseconds","1.25") && Has(Section(completed,"GPU_TIMING_COMPLETION"),"stage1_milliseconds","NOT_APPLICABLE"),"unused GPU stages are not reported as zero timings");
    Require(!RenderSnapshot_GPUTiming(capture_id,info.frame,capture_render,"READY",values,1),"completed timing cannot be overwritten twice");
    Require(!RenderSnapshot_DiagnosticActive() && !RenderSnapshot_Wanted(),"completed request does not keep capture armed");

    State();RenderSnapshot_Request();RenderSnapshot_Begin(info);RenderSnapshotDraw draw=Draw();RenderSnapshot_Pass("SUN_SHADOW");
    for(unsigned i=0;i<9000;++i)RenderSnapshot_Draw(&draw);
    for(unsigned i=0;i<400;++i){std::string id="material"+std::to_string(i);RenderSnapshot_Record("MATERIAL",id.c_str(),"source","world");}
    const char* categories[]={"UI","DECALS","NORMAL_MAPPING","DISTORTION","SHADER","R2_UI_BINDING","R2_DECAL_BINDING"};
    for(unsigned i=0;i<7;++i)RenderSnapshot_Record(categories[i],"late","evidence","KEPT_AFTER_WORLD_FLOOD");
    RenderSnapshot_Pass("HUD_UI");RenderSnapshot_Context("UI","button","ui_material");RenderSnapshot_UIWidget("root/button","ACTUAL_WIDGET");RenderSnapshot_MaterialRoute("mat_ui");RenderSnapshot_GeometrySource("geo_ui");RenderSnapshot_Draw(&draw);
    RenderSnapshot_End("COMPLETE");report=Read(RenderSnapshot_LastPath());const std::string completeness=Section(report,"COMPLETENESS");
    Require(Has(completeness,"frame_capture","COMPLETE") && Has(completeness,"chronological_trace","INCOMPLETE") && Has(completeness,"summary_integrity","COMPLETE"),"trace loss does not erase full frame boundary or counters");
    Require(Has(completeness,"representative_dictionaries","INCOMPLETE"),"bounded material dictionary loss is explicit");
    const std::string material=Section(report,"DICTIONARY MATERIAL");Require(Number(material,"records")==128 && Number(material,"updates_dropped")==272,"material dictionary stays bounded with exact loss");
    for(unsigned i=0;i<7;++i)Require(Has(Section(report,std::string("RECORD ")+categories[i]),"evidence","KEPT_AFTER_WORLD_FLOOD"),"late evidence has its own independent reservation");
    Require(Has(Section(report,"RECORD UI_ROUTE"),"representative_ui_widget","root/button") && Has(Section(report,"RECORD UI_ROUTE"),"material_route_id","mat_ui"),"protected actual draw representation links widget and material beyond early floods");

    State();RenderSnapshot_Request();std::vector<std::thread> producers;
    for(unsigned t=0;t<4;++t)producers.push_back(std::thread([](){for(unsigned i=0;i<200;++i)RenderSnapshot_Event("DECAL","mark","CREATED","OBSERVED");}));
    for(auto& producer:producers){producer.join();}report=Capture();const std::string tape=Section(report,"EVENT_TAPE");
    Require(Number(tape,"observed")==800 && Number(tape,"retained")==256 && Number(tape,"dropped")==544,"concurrent event producers retain exact bounded counts");
    Require(Has(Section(report,"COMPLETENESS"),"event_tape","INCOMPLETE"),"event loss status is independent and visible");
    RenderSnapshot_Event("AFTER","ordinary","IGNORED","NO_REQUEST");RenderSnapshot_Request();report=Capture();Require(Number(Section(report,"EVENT_TAPE"),"observed")==0,"inactive event recording is disabled and next request starts fresh");

    State();Require(RenderSnapshot_UICycleRequest("PDA",30),"opt-in PDA cycle arms");const uint64_t group=RenderSnapshot_GroupID();Require(group && !RenderSnapshot_Wanted(),"armed cycle waits for actual next graphics state");
    State();std::string baseline=Capture();Require(Has(Section(baseline,"UI_TRANSITION"),"role","BASELINE") && Has(Section(baseline,"UI_TRANSITION"),"status","WAITING_OPEN"),"actual GAME state captures baseline");
    RenderSnapshot_Event("INPUT","PDA_KEY","PRESSED","CONSUMED_BY_OTHER_WIDGET");State();Require(!RenderSnapshot_Wanted(),"key alone never advances UI cycle");
    RenderSnapshot_UIWindow("PDA","pda_root",true);Require(!strcmp(RenderSnapshot_UIScreen(),"PDA"),"actual window show updates canonical screen");State("PDA","pda_root",true);
    std::string opened=Capture();Require(Has(Section(opened,"UI_TRANSITION"),"role","OPENED") && Has(Section(opened,"UI_STATE"),"paused","1"),"paused PDA frame captured without simulation advance");
    Require(Number(Section(opened,"FRAME_IDENTITY"),"engine_frame")==Number(Section(baseline,"FRAME_IDENTITY"),"engine_frame") && Number(Section(opened,"FRAME_IDENTITY"),"render_frame")>Number(Section(baseline,"FRAME_IDENTITY"),"render_frame"),"paused engine frame retains distinct graphics identity");
    RenderSnapshot_Event("INPUT","ESC","PRESSED","OBSERVED");RenderSnapshot_UIWindow("PDA","pda_root",false);State();std::string recovery=Capture();
    Require(Has(Section(recovery,"UI_TRANSITION"),"role","RECOVERY") && Has(Section(recovery,"UI_TRANSITION"),"status","COMPLETE"),"actual close after ESC captures recovery");
    Require(Has(Section(recovery,"UI_RESTORATION"),"input_owner_comparison","MATCH") && Has(Section(recovery,"UI_RESTORATION"),"pause_comparison","MATCH"),"restoration compares observed input and pause state");
    Require(Number(Section(recovery,"FRAME_IDENTITY"),"group_id")==group && !RenderSnapshot_DiagnosticActive() && RenderSnapshot_GroupID()==0,"three frames share group then cycle stops");
    State();Require(!RenderSnapshot_Wanted(),"ordinary graphics frame after cycle creates no extra capture");
    RenderSnapshot_Request();RenderSnapshot_Abort("MANUAL_CANCEL");report=Read(RenderSnapshot_LastPath());Require(Number(Section(report,"FRAME_IDENTITY"),"group_id")==0 && report.find("[UI_TRANSITION]")==std::string::npos,"unrelated manual abort never inherits a completed UI group");

    State();RenderSnapshot_UICycleRequest("PDA",20);State();Capture();RenderSnapshot_RequestReason("MANUAL_DURING_CYCLE");State("PDA","pda_root",true);report=Capture();
    Require(Has(Section(report,"UI_TRANSITION"),"role","OPENED") && Has(Section(report,"UI_STATE"),"screen","PDA"),"cycle phase has priority over manual queue and captures the actual target frame");
    State();report=Capture();Require(Has(Section(report,"UI_TRANSITION"),"role","RECOVERY") && Has(Section(report,"UI_STATE"),"screen","GAME"),"queued manual request cannot displace recovery onto a wrong screen");
    State();report=Capture();Require(Has(Section(report,"FRAME_IDENTITY"),"scenario_role","MANUAL") && Number(Section(report,"FRAME_IDENTITY"),"group_id")==0,"manual request remains independent after cycle phases");
    State();RenderSnapshot_UICycleRequest("PDA",20);State();Capture();State("PDA","pda_root",true);State();RenderSnapshot_Begin(info);
    Require(!g_render_snapshot_active,"phase request is discarded if observed screen changed before Begin");RenderSnapshot_UICycleCancel("TEST_CANCEL");State();Capture();

    State("INVENTORY","inventory",true);Require(RenderSnapshot_UICycleRequest("INVENTORY",20),"already-open inventory cycle accepted without closing UI");State("INVENTORY","inventory",true);report=Capture();
    Require(Has(Section(report,"UI_TRANSITION"),"baseline_missing","1") && Has(Section(report,"UI_TRANSITION"),"role","OPENED"),"already-open target explicitly lacks baseline");
    State();report=Capture();Require(Has(Section(report,"UI_TRANSITION"),"status","COMPLETE_BASELINE_MISSING"),"missing baseline stays explicit on recovery");

    State();Require(RenderSnapshot_UICycleRequest("PDA",3),"timeout cycle accepted");State();Capture();State();Require(!RenderSnapshot_Wanted(),"timeout has not elapsed early");State();report=Capture();
    Require(Has(Section(report,"UI_TRANSITION"),"status","TIMEOUT") && Has(Section(report,"UI_TRANSITION"),"role","TRANSITION_TERMINATED") && !RenderSnapshot_DiagnosticActive(),"timeout records explicit termination and stops");
    State();RenderSnapshot_UICycleRequest("PDA",20);RenderSnapshot_UICycleCancel("CANCELLED_BY_USER");State();report=Capture();Require(Has(Section(report,"UI_TRANSITION"),"status","CANCELLED_BY_USER"),"explicit user cancellation is reviewable");
    State();RenderSnapshot_UICycleRequest("PDA",20);State();Capture();RenderSnapshot_FrameIDs(~uint64_t(0),~uint64_t(0),2,1);RenderSnapshot_UIState("GAME","GAME","GAME",true,false,false);report=Capture();
    Require(Has(Section(report,"UI_TRANSITION"),"status","LEVEL_CHANGED"),"level generation changes terminate cycle before mixing levels");

    State("START_MENU","start_menu",false,false);RenderSnapshot_UICycleRequest("MENU",20);State("START_MENU","start_menu",false,false);report=Capture();
    Require(Has(Section(report,"UI_STATE"),"screen","START_MENU") && Has(Section(report,"UI_TRANSITION"),"role","OPENED") && Has(Section(report,"UI_TRANSITION"),"baseline_missing","1"),"frontend START_MENU is a real MENU target without fabricating a gameplay baseline");
    RenderSnapshot_UICycleCancel("MENU_CYCLE_CANCELLED");State("START_MENU","start_menu",false,false);Capture();
    State();RenderSnapshot_UICycleRequest("PDA",20);State();Capture();RenderSnapshot_FrameIDs(~uint64_t(0),~uint64_t(0),1,2);RenderSnapshot_UIState("GAME","GAME","GAME",true,false,false);report=Capture();Require(Has(Section(report,"UI_TRANSITION"),"status","DEVICE_RESET"),"device generation changes terminate cycle explicitly");
    State();RenderSnapshot_Request();RenderSnapshot_Abort("DEVICE_RESET");report=Read(RenderSnapshot_LastPath());Require(Has(Section(report,"COMPLETENESS"),"frame_capture","NOT_CAPTURED") && Has(Section(report,"FRAME_IDENTITY"),"frame_scope","EVENT_ONLY_TERMINATION; no graphics frame captured"),"abort before frame produces termination context without a fabricated graphics capture");
    Require(!RenderSnapshot_DiagnosticActive() && !RenderSnapshot_Wanted(),"abort drains all pending diagnostic work");
    State();RenderSnapshot_Request();RenderSnapshot_Begin(info);RenderSnapshot_Draw(&draw);RenderSnapshot_Abort("LEVEL_UNLOAD");report=Read(RenderSnapshot_LastPath());Require(Has(Section(report,"COMPLETENESS"),"frame_capture","INCOMPLETE"),"mid-frame abort closes explicit incomplete graphics frame");

    Require(!RenderSnapshot_ProfileRequest(0) && !RenderSnapshot_ProfileRequest(121),"counter profile rejects unbounded requests");Require(RenderSnapshot_ProfileRequest(3),"bounded counter-only profile accepted");
    for(unsigned n=0;n<3;++n){State();RenderSnapshot_Begin(info);Require(g_render_snapshot_active && !RenderSnapshot_Detailed(),"counter profile disables metadata-heavy caller path");
        for(unsigned i=0;i<11;++i){RenderSnapshot_Draw(&draw);}RenderSnapshot_Record("UI","discarded","data","UNEXPECTED");RenderSnapshot_End("COMPLETE");report=Read(RenderSnapshot_LastPath());
        Require(report.find("[DRAW ")==std::string::npos && Has(Section(report,"CAPTURE_MODE"),"mode","COUNTERS_ONLY"),"counter profile emits no detailed draw trace");
        Require(Number(Section(report,"DRAW_AGGREGATE_TOTALS"),"submitted_draw_calls")==11 && Number(Section(report,"TRACE_LOSS"),"draw_records_dropped")==11,"counter mode preserves global exact submission totals");
        const std::string loss=Section(report,"TRACE_LOSS");Require(Number(loss,"summary_updates_observed")==Number(loss,"summary_updates_retained")+Number(loss,"summary_updates_dropped")+Number(loss,"summary_updates_omitted"),"counter profile summary updates account explicitly for intentional omissions");
        Require(Has(Section(report,"COMPLETENESS"),"chronological_trace","NOT_REQUESTED_COUNTER_MODE") && Number(Section(report,"DICTIONARY UI"),"records")==0,"intentional omissions are not mislabeled trace completeness");}
    Require(!RenderSnapshot_DiagnosticActive() && !RenderSnapshot_Wanted(),"profile stops exactly at requested count");
    RenderSnapshot_SetHotkey(63);Require(RenderSnapshot_GetHotkey()==63,"DIK key is configurable");RenderSnapshot_SetHotkey(256);Require(RenderSnapshot_GetHotkey()==63,"invalid DIK is rejected");RenderSnapshot_SetHotkey(0);Require(RenderSnapshot_GetHotkey()==0,"zero disables hotkey");
    for(unsigned i=0;i<16;++i){Require(RenderSnapshot_Request(),"bounded queue admits each request");}Require(!RenderSnapshot_Request(),"bounded request queue explicitly rejects overflow");
    RenderSnapshot_Abort("SHUTDOWN");Require(!RenderSnapshot_Wanted(),"shutdown cancels bounded request queue");

    State();RenderSnapshot_Request();RenderSnapshot_Begin(info);const uint64_t deleted_id=RenderSnapshot_CaptureID(),deleted_render=RenderSnapshot_RenderFrameID();RenderSnapshot_End("COMPLETE");path=RenderSnapshot_LastPath();remove(path.c_str());
    Require(!RenderSnapshot_GPUTiming(deleted_id,info.frame,deleted_render,"TIMEOUT",NULL),"missing original log cannot be recreated as timing-only file");Require(Read(path).empty(),"late timing append does not create deleted path");
    State();RenderSnapshot_Request();RenderSnapshot_Begin(info);const uint64_t invalid_id=RenderSnapshot_CaptureID(),invalid_render=RenderSnapshot_RenderFrameID();values[0]=NAN;
    Require(!RenderSnapshot_GPUTiming(invalid_id,info.frame,invalid_render,"READY",values,1),"NaN GPU timing is rejected");Require(RenderSnapshot_GPUTiming(invalid_id,info.frame,invalid_render,"DISJOINT",NULL,127),"disjoint timing has explicit non-value status");RenderSnapshot_End("COMPLETE");report=Read(RenderSnapshot_LastPath());Require(Has(Section(report,"COMPLETENESS"),"gpu_timing","DISJOINT"),"disjoint validity is separate from frame completeness");
    printf("G3b.1 telemetry recorder: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
