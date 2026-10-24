#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include <stdexcept>
#include <chrono>
#include "KichR4RecordControl.h"
#include "KichR4OpticalRules.h"
typedef const char* LPCSTR;
#define TRUE true
#define FALSE false
struct IWriter {unsigned writes=0;};
struct IConsole_Command {
    typedef char TStatus[256];
    bool bEmptyArgsHandled=false,bLowerCaseArgs=true;
    explicit IConsole_Command(LPCSTR){}
    virtual ~IConsole_Command(){}
    virtual void Execute(LPCSTR)=0;
    virtual void Status(TStatus&){}
    virtual void Save(IWriter* output){++output->writes;}
};
template<size_t Size,class... Args>int sprintf_s(char (&value)[Size],const char* format,Args... args){return std::snprintf(value,Size,format,args...);}
int xr_strcmp(const char* a,const char* b){return std::strcmp(a,b);}
std::vector<std::string> messages,marks;
bool native_active=true,watch=false;
unsigned record_calls=0,watch_calls=0,view_calls=0,view=0;
bool KichR4_NativeActive(){return native_active;}
void Msg(const char* format,...){char text[1024];va_list args;va_start(args,format);std::vsnprintf(text,sizeof(text),format,args);va_end(args);messages.push_back(text);}
void KichR4_RecordSet(bool value){++record_calls;kich_r4_record::Request(value);}
void KichR4_RecordMark(const char* text){marks.push_back(text);}
void KichR4_RecordWatch(bool value){++watch_calls;watch=value;}
void KichR4_RecordView(unsigned value){++view_calls;view=value;}
unsigned KichR4_RecordView(){return view;}
#include "record_commands_actual.h"
unsigned queue_clock_calls=0;
bool g_render_snapshot_active=false,queue_measured=false;
std::chrono::steady_clock::time_point queue_begin;
std::chrono::steady_clock::time_point QueueNow(){++queue_clock_calls;return std::chrono::steady_clock::time_point(std::chrono::milliseconds(queue_clock_calls*5));}
#include "queue_clock_actual.h"
kich_r4_record::Control* RecordPeer();
namespace {
unsigned checks=0;
void Require(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
void Commands(){
    using namespace kich_r4_record;
    CCC_LogRecord record("log_record");CCC_LogMark mark("log_mark");CCC_LogWatch track("log_watch");CCC_R4DebugView debug("r4_debug_view");
    Require(!Enabled() && !Requested() && Epoch()==0,"recording defaults OFF and has no persisted epoch");
    Require(RecordPeer()==&Switch(),"console/device/presenter share one EXE record control across translation units");
    record.Execute("");Require(!record_calls && messages.back().find("requested=0 active=0")!=std::string::npos,"empty argument reports status without enabling recording");
    const char* invalid[]={"2","-1","true","on","1 0","1x"," 1","1 ","00","01","0x1"};
    for(const char* arg:invalid){record.Execute(arg);Require(!record_calls,"invalid boolean cannot mutate recording");track.Execute(arg);Require(!watch_calls,"invalid watch boolean cannot mutate selection");}
    mark.Execute("before");track.Execute("1");Require(marks.empty() && !watch_calls,"watch/mark reject disabled recording");
    record.Execute("1");Require(record_calls==1 && Requested() && !Enabled(),"command changes requested state without mid-frame mutation");
    mark.Execute("SUN.Edge-01:test");Require(marks.size()==1 && marks.back()=="SUN.Edge-01:test" && !mark.bLowerCaseArgs,"mark preserves bounded safe label");
    const char* bad_labels[]={"",NULL,"with space","unsafe\nline","percent%","path/slash","quote\""};
    for(const char* label:bad_labels){mark.Execute(label);Require(marks.size()==1,"invalid mark is rejected before producer");}
    const std::string maximum(63,'x'),overflow(64,'x');mark.Execute(maximum.c_str());mark.Execute(overflow.c_str());
    Require(marks.size()==2 && marks.back().size()==63,"mark length bound is exact");
    track.Execute("1");Require(watch && watch_calls==1,"watch accepts recording requested before next frame");
    Require(BeginFrame(1) && Enabled() && Epoch()==1,"frame boundary latches recording and starts an epoch");
    Require(!BeginFrame(2) && Epoch()==1,"ordinary frame does not restart recording epoch");
    const char* names[]={"normal","hemi","baked_rgb","sun_mask","sun_shadow"};const unsigned values[]={0,5,6,7,8};
    for(unsigned i=0;i<5;++i){debug.Execute(names[i]);Require(view==values[i],"named diagnostic view maps to existing shader mode");}
    for(unsigned i=0;i<kich_r4_optics::DebugViewCount;++i){char value[16];std::snprintf(value,sizeof(value),"%u",i);debug.Execute(value);Require(view==i,"numeric existing view accepted");}
    const unsigned previous=view_calls;const char* bad_views[]={"13","-1","99999999999999999999999999999999","5x","5 6","none"," 5","5 "};
    for(const char* value:bad_views){debug.Execute(value);Require(view_calls==previous,"invalid view does not mutate active mode");}
    debug.Execute("");Require(view_calls==previous && messages.back().find("debug_view=12")!=std::string::npos,"view status does not mutate rendering");
    IWriter writer;record.Save(&writer);mark.Save(&writer);track.Save(&writer);debug.Save(&writer);Require(!writer.writes,"diagnostic commands never persist in user config");
    record.Execute("0");Require(!Requested() && Enabled(),"disable is latched at next frame too");
    Require(BeginFrame(3) && !Enabled() && Epoch()==2,"disable closes epoch");
    record.Execute("1");BeginFrame(4);Require(Enabled() && Epoch()==3,"ON-OFF-ON creates fresh epoch");
    native_active=false;const unsigned records=record_calls,watches=watch_calls,views=view_calls;const size_t marked=marks.size();
    record.Execute("0");mark.Execute("wrong_renderer");track.Execute("0");debug.Execute("normal");
    Require(record_calls==records && watch_calls==watches && view_calls==views && marks.size()==marked,"R2 calls cannot mutate native R4 recording or views");
    native_active=true;Request(false);BeginFrame(5);
}
void QueueGates(){
    using namespace kich_r4_record;
    BeginQueueClock();Require(!queue_measured && queue_clock_calls==0 && KichR4_NativeQueueTime()==0,"OFF performs no queue clocks");
    Request(true);BeginFrame(6);BeginQueueClock();Require(queue_measured && queue_clock_calls==1,"ON begins queue measurement");
    Require(KichR4_NativeQueueTime()==5 && queue_clock_calls==2,"ON reads elapsed queue time");
    Request(false);BeginFrame(7);Require(KichR4_NativeQueueTime()==0 && queue_clock_calls==2,"OFF cannot return stale measurement or read a timer");
    g_render_snapshot_active=true;BeginQueueClock();Require(queue_measured && queue_clock_calls==3,"explicit snapshot can start timing while recording OFF");
    Require(KichR4_NativeQueueTime()==5 && queue_clock_calls==4,"explicit snapshot retains queue measurement");
    g_render_snapshot_active=false;BeginQueueClock();Require(!queue_measured && queue_clock_calls==4,"snapshot end restores no-clock ordinary mode");
}
}
int main(){try{Commands();QueueGates();std::printf("E1.7 recording commands/queue gates: %u checks passed\n",checks);return 0;}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
