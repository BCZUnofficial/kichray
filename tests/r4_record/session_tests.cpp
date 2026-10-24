#include "../../xr_3da/KichR4RecordSession.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <new>
#include <stdexcept>
#include <limits>
#if defined(KICH_TEST_X87)
#include <fpu_control.h>
#endif
static uint64_t allocations=0;
void* operator new(size_t size){++allocations;void* p=std::malloc(size?size:1);if(!p)throw std::bad_alloc();return p;}
void* operator new[](size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
namespace {
using namespace kich_r4_record;
using kich_r4_shadow_record::Frame;
using kich_r4_shadow_record::Pass;
using kich_r4_shadow_record::Work;
unsigned checks=0,line_count=0;
char lines[32][1024];
void Require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void Log(const char* text){if(line_count<32){std::snprintf(lines[line_count],sizeof(lines[line_count]),"%s",text);++line_count;}}
void ThrowLog(const char*){throw 7;}
void ClearLog(){line_count=0;}
bool Contains(unsigned line,const char* value){return line<line_count && std::strstr(lines[line],value)!=NULL;}
const int64_t Hz=10000000;
void Setup(KichR4GameFrame& input,Frame& frame,uint32_t number=1){
    input.engine_frame=number;input.position[0]=1;input.position[1]=2;input.position[2]=3;
    input.observation.valid=true;input.observation.camera_direction[0]=0;input.observation.camera_direction[1]=0;input.observation.camera_direction[2]=1;
    frame.valid=true;frame.frame=number;frame.local_completed=6;frame.local_resolution=512;
    for(unsigned c=0;c<2;++c){Pass& p=frame.sun[c];p.begun=p.completed=p.signatures=true;p.frame=number;p.face=c;
        p.resolution=2048;p.map=10+c;p.generation=1;p.span=64.f*(c+1);p.texel_size=p.span/p.resolution;
        for(unsigned i=0;i<16;++i)p.matrix[i]=i%5==0?1.f:0.f;
        p.work[0].Caster(100+c,0,12);p.work[0].Draw(12);}
}
void Next(KichR4GameFrame& input,Frame& frame){++input.engine_frame;frame.frame=input.engine_frame;for(unsigned c=0;c<2;++c)frame.sun[c].frame=input.engine_frame;}
const Trace& Latest(const Session& session){return session.trace[(session.trace_next+Session::TraceCapacity-1)%Session::TraceCapacity];}
void DisabledAndEpochs(){
    Session s;KichR4GameFrame input={};Frame frame;Setup(input,frame);ClearLog();const uint64_t before=allocations;
    Require(!s.Sync(false,0,0,Hz,Log),"default OFF does not start a session");
    s.Observe(input,frame,1,1,1);s.Emit("must_not_emit %u",1u);s.TraceReport();
    Require(!s.GPUDue(1) && !s.ReportDue(Hz*2),"OFF has no profiling or report schedule");
    Require(!s.trace_count && !s.last_frame && !s.level && !s.lines && !line_count,"OFF performs no trace mutation or formatting/output");
    Require(allocations==before,"OFF collection/scheduling/report path allocates nothing");
    Require(s.Sync(true,1,100,Hz,Log) && s.active && s.epoch==1,"enabled new epoch starts a session");
    Require(line_count==1 && Contains(0,"state=START") && s.lines==1,"start emits one bounded declaration");
    Require(!s.Sync(true,1,200,Hz,Log) && line_count==1 && s.started==100,"same epoch does not reset counters or time origin");
    s.Observe(input,frame,300,1,1);Require(s.trace_count==1,"session records its first frame");
    Require(s.Sync(false,2,400,Hz,Log) && !s.active && !s.trace_count && !s.has_previous,"OFF closes session and clears trace history");
    Require(line_count==4 && Contains(1,"[R4 E1.7 trace]") && Contains(2,"shadow_change") && Contains(3,"session=1 state=STOP"),"stop drains cached last samples before closing prior session");
    s.Emit("off");Require(line_count==4,"disabled Emit cannot re-enable writing");
    Require(s.Sync(true,3,500,Hz,Log) && s.started==500 && s.lines==1 && !s.trace_count && !s.level && !s.device,"restart has independent counters/trace/generation");
    s.Observe(input,frame,600,9,7);Require(Latest(s).reason==31,"restart begins a complete baseline");
    s.sink=ThrowLog;s.Emit("isolated callback");Require(s.active && s.sink_failures==1,"sink exception cannot escape into rendering");
    s.sink=NULL;const uint64_t emitted=s.lines;s.Emit("no sink");Require(s.lines==emitted,"missing sink does not count an emitted record");
}
void Clocks(){
    const int64_t starts[]={0,180000000000LL,1000000000000000000LL,(std::numeric_limits<int64_t>::max)()-Hz*10};
    for(unsigned i=0;i<sizeof(starts)/sizeof(starts[0]);++i){Session s;s.Sync(true,i+1,starts[i],Hz,NULL);
        Require(std::fabs(s.Elapsed(starts[i]+123456)-12.3456)<.0001,"elapsed subtracts integer ticks before conversion at long uptime");
        Require(s.GPUDue(starts[i]),"first GPU sample may start at session origin including zero");
        Require(!s.GPUDue(starts[i]),"same QPC timestamp cannot request two GPU samples");
        Require(!s.GPUDue(starts[i]+Hz/2-1) && s.GPUDue(starts[i]+Hz/2),"GPU cadence has exact half-second boundary");
        Require(!s.GPUDue(starts[i]+Hz/2-1),"backward clock cannot issue a new GPU sample");
        Require(!s.ReportDue(starts[i]+2*Hz-1) && s.ReportDue(starts[i]+2*Hz),"report cadence has exact two-second boundary");
        Require(!s.ReportDue(starts[i]+2*Hz) && !s.ReportDue(starts[i]+Hz),"duplicate/backward report clocks cannot duplicate report");
        if(starts[i])Require(s.Elapsed(starts[i]-1)==0,"time before origin is not a large wrapped duration");
    }
    Session odd;odd.Sync(true,1,0,3,NULL);Require(odd.GPUDue(0) && !odd.GPUDue(1) && odd.GPUDue(2),"odd frequency uses ceil half-period");
    Session invalid;invalid.Sync(true,1,1,0,NULL);Require(!invalid.GPUDue(100) && !invalid.ReportDue(100) && invalid.Elapsed(100)==0,"zero clock frequency disables timing");
}
void TraceChanges(){
    Session s;KichR4GameFrame input={};Frame frame;Setup(input,frame);s.Sync(true,1,100,Hz,NULL);
    s.Observe(input,frame,100,1,1);Require(s.trace_count==1 && Latest(s).reason==31,"first valid frame establishes baseline");
    Next(input,frame);s.Observe(input,frame,200,1,1);Require(s.trace_count==1 && s.previous.frame==2,"identical consecutive shadow state does not flood trace");
    Next(input,frame);frame.sun[0].matrix[12]=.25f;s.Observe(input,frame,300,1,1);Require(Latest(s).reason==MatrixChanged,"projection change is isolated from caster/range changes");
    Next(input,frame);frame.sun[0].map^=1;frame.sun[0].generation^=1;s.Observe(input,frame,400,1,1);Require(Latest(s).reason==MatrixChanged,"equal map/generation bit changes cannot XOR-cancel");
    Next(input,frame);frame.sun[0].completed=false;s.Observe(input,frame,500,1,1);Require(Latest(s).reason==MatrixChanged,"pass completion change is observed even with same matrix");
    Next(input,frame);frame.sun[0].work[0].id_sum^=1;s.Observe(input,frame,600,1,1);Require(Latest(s).reason==CasterSetChanged,"caster identity signature change observed");
    Next(input,frame);frame.sun[0].work[0].range_sum^=1;s.Observe(input,frame,700,1,1);Require(Latest(s).reason==DrawRangeChanged,"draw range change observed without fake caster change");
    Next(input,frame);frame.sun[0].work[0].unknown_ids=1;s.Observe(input,frame,800,1,1);Require(Latest(s).reason==CasterSetChanged,"loss of known identity is visible");
    Next(input,frame);frame.local_resolution=256;s.Observe(input,frame,900,1,1);Require(Latest(s).reason==LocalPoolChanged,"local resolution change has separate reason");
    Next(input,frame);frame.local_completed=12;s.Observe(input,frame,1000,1,1);Require(Latest(s).reason==LocalPoolChanged,"local face count change has separate reason");
    Next(input,frame);++frame.local_generation;s.Observe(input,frame,1050,1,1);Require(Latest(s).reason==LocalShadowPlanChanged,"actual local pool generation change is recorded");
    Next(input,frame);input.position[0]=99;input.observation.camera_direction[0]=.5f;frame.sun[1].matrix[0]=2;s.Observe(input,frame,1100,1,1);
    Require(Latest(s).position[0]==99 && Latest(s).direction[0]==.5f && Latest(s).frame==input.engine_frame,"trace snapshots camera and matching engine frame by value");
    const unsigned before=s.trace_count;frame.valid=false;Next(input,frame);s.Observe(input,frame,1200,1,1);Require(s.trace_count==before && !s.has_previous,"invalid shadow capture breaks continuity without fabricated sample");
    frame.valid=true;Next(input,frame);--frame.frame;s.Observe(input,frame,1300,1,1);Require(s.trace_count==before && !s.has_previous,"stale shadow frame cannot be combined with current camera");
    frame.frame=input.engine_frame;s.Observe(input,frame,1400,1,1);Require(Latest(s).reason&FrameGap,"resumed valid data has explicit gap");
    const Work old=frame.sun[0].work[0];Next(input,frame);frame.sun[0].work[0]=frame.sun[0].work[1];frame.sun[0].work[1]=old;s.Observe(input,frame,1500,1,1);
    Require((Latest(s).reason&(CasterSetChanged|DrawRangeChanged))==(CasterSetChanged|DrawRangeChanged),"kind reclassification cannot hide behind equal totals");
    Next(input,frame);s.Observe(input,frame,1600,1,1,4);Require(Latest(s).reason==ModeChanged && Latest(s).mode==4,"diagnostic mode transition is retained without inventing shadow changes");
    ClearLog();s.sink=Log;s.TraceReport();Require(Contains(line_count-1,"mode=0x4") && Contains(0,"modes=1_warmup"),"trace examples and legend distinguish non-gameplay modes");
}
void BoundedRingAndGenerations(){
    Session s;KichR4GameFrame input={};Frame frame;Setup(input,frame,0);s.Sync(true,7,1,Hz,NULL);
    const uint64_t before=allocations;
    for(unsigned i=0;i<400;++i){input.engine_frame=frame.frame=i;frame.sun[0].matrix[12]=float(i);s.Observe(input,frame,1+i*100,11,12);}
    Require(allocations==before,"400 changing frames perform no trace allocation");
    Require(s.trace_count==128 && s.trace_overwritten==272 && s.trace_changes==400,"ring capacity and overwritten count remain exact");
    const unsigned first=(s.trace_next+Session::TraceCapacity-s.trace_count)%Session::TraceCapacity;
    Require(s.trace[first].frame==272 && Latest(s).frame==399,"ring retains chronologically newest bounded frames");
    ClearLog();s.sink=Log;const uint64_t report_allocations=allocations;s.TraceReport();
    Require(allocations==report_allocations && line_count==9,"bounded report emits at most eight examples plus header without allocating");
    Require(Contains(0,"retained=128") && Contains(0,"examples=8") && Contains(0,"omitted=392"),"report admits omissions instead of claiming a complete trace");
    Require(Contains(1,"frame=272 ") && Contains(8,"frame=399 "),"even sampling preserves first and last retained frame");
    Require(!s.trace_count && !s.trace_changes && s.has_previous,"report drains samples while preserving comparison baseline");
    Next(input,frame);s.Observe(input,frame,50000,11,12);Require(!s.trace_count,"unchanged next frame after report does not invent a gap");
    frame.sun[0].matrix[12]=999;Next(input,frame);s.Observe(input,frame,50100,11,12);Require(s.trace_count==1,"pending change exists before level switch");
    Next(input,frame);s.Observe(input,frame,50200,13,12);
    Require(s.trace_count==1 && Latest(s).frame==input.engine_frame && (Latest(s).reason&FrameGap) && s.trace_epoch_discarded==1,"level change discards old pending trace and starts new baseline");
    Next(input,frame);s.Observe(input,frame,50300,13,14);
    Require(s.trace_count==1 && s.trace_epoch_discarded==2 && s.level==13 && s.device==14,"device change likewise prevents cross-generation trace mixing");
    ClearLog();s.TraceReport();Require(Contains(0,"level=13 device=14") && Contains(0,"previous_epoch_discarded=2"),"report identifies generation and discarded previous data");
    Require(!s.trace_epoch_discarded,"reported generation discard count is drained");
}
void ModuloAndGaps(){
    Session s;KichR4GameFrame input={};Frame frame;Setup(input,frame,UINT32_MAX);s.Sync(true,1,1,Hz,NULL);
    s.Observe(input,frame,2,1,1);Next(input,frame);Require(!input.engine_frame,"test crossed frame counter rollover");
    s.Observe(input,frame,3,1,1);Require(s.trace_count==1 && !s.previous.reason,"UINT32_MAX to zero is a consecutive engine frame");
    Next(input,frame);Next(input,frame);s.Observe(input,frame,4,1,1);Require(Latest(s).reason==FrameGap,"skipped engine frame has gap reason with unchanged data");
    s.Gap();Next(input,frame);s.Observe(input,frame,5,1,1);Require(Latest(s).reason==31,"explicit gap forces a new bounded baseline");
    const unsigned before=s.trace_count;s.Observe(input,frame,0,1,1);Require(s.trace_count==before && !s.has_previous,"clock before session start cannot append a misleading timestamp");
}
}
int main(){try{
#if defined(KICH_TEST_X87)
    fpu_control_t saved;_FPU_GETCW(saved);fpu_control_t single=(saved&~_FPU_EXTENDED)|_FPU_SINGLE;_FPU_SETCW(single);
#endif
    DisabledAndEpochs();Clocks();TraceChanges();BoundedRingAndGenerations();ModuloAndGaps();
#if defined(KICH_TEST_X87)
    _FPU_SETCW(saved);
#endif
    std::printf("E1.7 bounded recording session: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
