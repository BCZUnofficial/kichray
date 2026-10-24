#ifndef KICH_R4_RECORD_SESSION_H
#define KICH_R4_RECORD_SESSION_H
#include "KichR4RecordControl.h"
#include "KichR4Game.h"
#include "KichR4ShadowRecord.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace kich_r4_record {
// Bounded numerical history. Recording never starts a second scene traversal,
// GPU readback, filesystem write, or allocation on behalf of this buffer.
struct Trace {
    uint32_t frame,reason,mode,local_faces,local_size;
    double ms;
    float position[3],direction[3];
    uint64_t matrix[2],ids[2],ranges[2];
    uint64_t casters[2],draws[2],local_generation;
    Trace():frame(0),reason(0),mode(0),local_faces(0),local_size(0),ms(0),position(),direction(),matrix(),ids(),ranges(),casters(),draws(),local_generation(0){}
};
enum TraceReason { MatrixChanged=1,CasterSetChanged=2,DrawRangeChanged=4,LocalShadowPlanChanged=8,LocalPoolChanged=LocalShadowPlanChanged,FrameGap=16,ModeChanged=32 };
struct Session {
    enum { TraceCapacity=128,TraceExamples=8 };
    bool active,has_previous,has_gpu_sample;
    uint64_t epoch,level,device,lines,bytes,trace_overwritten,trace_changes,trace_epoch_discarded,sink_failures;
    uint32_t last_frame;
    int64_t started,frequency,last_gpu,last_report;
    double collect_ms,emit_ms;
    unsigned trace_count,trace_next;
    Trace trace[TraceCapacity],previous;
    void (*sink)(const char*);
    Session():active(false),has_previous(false),has_gpu_sample(false),epoch(0),level(0),device(0),lines(0),bytes(0),trace_overwritten(0),trace_changes(0),trace_epoch_discarded(0),sink_failures(0),last_frame(0),started(0),frequency(0),last_gpu(0),last_report(0),collect_ms(0),emit_ms(0),trace_count(0),trace_next(0),sink(NULL){}
#if defined(__GNUC__)
    __attribute__((format(gnu_printf,2,3)))
#endif
    void Emit(const char* format,...){
        if(!active || !sink)return;
        char text[1024];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
        text[sizeof(text)-1]=0;++lines;bytes+=strlen(text)+2;try{sink(text);}catch(...){++sink_failures;}
    }
    double Elapsed(int64_t now)const{return frequency>0 && now>=started?double(now-started)*1000.0/double(frequency):0;}
    bool Sync(bool enabled,uint64_t requested_epoch,int64_t now,int64_t ticks_per_second,void (*log)(const char*)){
        sink=log;
        if(epoch==requested_epoch && active==enabled)return false;
        if(active){
            // Drain only already-collected numeric data; stopping never issues
            // a final GPU query, scene traversal or synchronous file flush.
            TraceReport();
            Emit("[R4 E1.7 record] session=%llu state=STOP last_frame=%u record_ms=%.3f lines=%llu bytes=%llu pending_trace=%u output_failures=%llu; errors_remain_enabled",
                static_cast<unsigned long long>(epoch),last_frame,Elapsed(now),static_cast<unsigned long long>(lines),static_cast<unsigned long long>(bytes),trace_count,static_cast<unsigned long long>(sink_failures));}
        epoch=requested_epoch;active=enabled;started=last_report=now;frequency=ticks_per_second;last_gpu=0;last_frame=0;
        has_previous=has_gpu_sample=false;trace_count=trace_next=0;trace_overwritten=trace_changes=trace_epoch_discarded=sink_failures=lines=bytes=0;collect_ms=emit_ms=0;
        level=device=0;
        if(active)Emit("[R4 E1.7 record] session=%llu state=START default=OFF report_ms=2000 gpu_sample_ms=500 trace_capacity=%u trace_examples=%u; CPU_inputs_not_pixel_visibility; no_GPU_wait",
            static_cast<unsigned long long>(epoch),TraceCapacity,TraceExamples);
        return true;
    }
    bool GPUDue(int64_t now){
        if(!active || frequency<=0 || now<started)return false;
        const int64_t half=frequency/2+(frequency%2!=0);
        if(has_gpu_sample && (now<last_gpu || now-last_gpu<half))return false;
        last_gpu=now;has_gpu_sample=true;return true;
    }
    bool ReportDue(int64_t now){
        if(!active || frequency<=0 || now<last_report || uint64_t(now-last_report)<uint64_t(frequency)*2u)return false;
        last_report=now;return true;
    }
    void Gap(){has_previous=false;}
    void Generation(uint64_t level_generation,uint64_t device_generation){
        if(level==level_generation && device==device_generation)return;
        // Resource/caster identities cannot be compared across scene epochs.
        // Do not emit old samples with the next level's report header.
        trace_epoch_discarded+=trace_changes;
        trace_count=trace_next=0;trace_overwritten=trace_changes=0;Gap();
        level=level_generation;device=device_generation;
    }
    static uint64_t Hash(const void* memory,size_t bytes){
        const unsigned char* p=static_cast<const unsigned char*>(memory);uint64_t value=UINT64_C(14695981039346656037);
        for(size_t i=0;i<bytes;++i){value^=p[i];value*=UINT64_C(1099511628211);}return value;
    }
    void Observe(const KichR4GameFrame& input,const kich_r4_shadow_record::Frame& shadows,int64_t now,uint64_t level_generation,uint64_t device_generation,unsigned mode=0){
        if(!active)return;
        Generation(level_generation,device_generation);
        last_frame=input.engine_frame;if(!shadows.valid || shadows.frame!=input.engine_frame || now<started){Gap();return;}
        Trace sample;sample.frame=input.engine_frame;sample.ms=Elapsed(now);sample.mode=mode;
        memcpy(sample.position,input.position,sizeof(sample.position));memcpy(sample.direction,input.observation.camera_direction,sizeof(sample.direction));
        sample.local_faces=shadows.local_completed;sample.local_size=shadows.local_resolution;sample.local_generation=shadows.local_generation;
        for(unsigned c=0;c<2;++c){const auto& pass=shadows.sun[c];
            const uint64_t projection[]={pass.map,pass.generation,pass.resolution,
                kich_r4_shadow_record::FloatBits(pass.span),kich_r4_shadow_record::FloatBits(pass.texel_size),
                uint64_t(pass.begun),uint64_t(pass.completed),uint64_t(pass.signatures)};
            sample.matrix[c]=kich_r4_shadow_record::Mix(Hash(pass.matrix,sizeof(pass.matrix)))^Hash(projection,sizeof(projection));
            for(unsigned k=0;k<kich_r4_shadow_record::KindCount;++k){const auto& work=pass.work[k];
                const uint64_t identities[]={k,work.id_sum,work.id_xor,work.casters,work.unknown_ids};
                const uint64_t ranges[]={k,work.range_sum,work.draws,work.indices};
                sample.ids[c]=kich_r4_shadow_record::Mix(sample.ids[c]^Hash(identities,sizeof(identities)));
                sample.ranges[c]=kich_r4_shadow_record::Mix(sample.ranges[c]^Hash(ranges,sizeof(ranges)));
                sample.casters[c]+=work.casters;sample.draws[c]+=work.draws;}}
        if(!has_previous || sample.frame!=previous.frame+1)sample.reason|=FrameGap;
        for(unsigned c=0;c<2;++c){
            if(!has_previous || sample.matrix[c]!=previous.matrix[c])sample.reason|=MatrixChanged;
            if(!has_previous || sample.ids[c]!=previous.ids[c] || sample.casters[c]!=previous.casters[c])sample.reason|=CasterSetChanged;
            if(!has_previous || sample.ranges[c]!=previous.ranges[c] || sample.draws[c]!=previous.draws[c])sample.reason|=DrawRangeChanged;}
        if(!has_previous || sample.local_faces!=previous.local_faces || sample.local_size!=previous.local_size || sample.local_generation!=previous.local_generation)sample.reason|=LocalShadowPlanChanged;
        if(has_previous && sample.mode!=previous.mode)sample.reason|=ModeChanged;
        previous=sample;has_previous=true;
        if(!sample.reason)return;
        ++trace_changes;if(trace_count==TraceCapacity)++trace_overwritten;else ++trace_count;
        trace[trace_next]=sample;trace_next=(trace_next+1)%TraceCapacity;
    }
    void TraceReport(){
        if(!active || !trace_count)return;
        const unsigned examples=trace_count<TraceExamples?trace_count:unsigned(TraceExamples);
        Emit("[R4 E1.7 trace] session=%llu level=%llu device=%llu change_frames=%llu retained=%u examples=%u omitted=%llu previous_epoch_discarded=%llu; reasons=1_matrix,2_casters,4_ranges,8_local_shadow_plan,16_gap,32_mode; modes=1_warmup,2_snapshot,4_debug,8_paused,16_console,32_inactive,64_screenshot; examples_not_complete_trace; signatures_not_visibility_proof",
            static_cast<unsigned long long>(epoch),static_cast<unsigned long long>(level),static_cast<unsigned long long>(device),static_cast<unsigned long long>(trace_changes),trace_count,examples,
            static_cast<unsigned long long>(trace_overwritten+trace_count-examples),static_cast<unsigned long long>(trace_epoch_discarded));
        const unsigned first=(trace_next+TraceCapacity-trace_count)%TraceCapacity;
        for(unsigned i=0;i<examples;++i){const unsigned offset=examples>1?i*(trace_count-1)/(examples-1):0;const Trace& t=trace[(first+offset)%TraceCapacity];
            Emit("[R4 E1.7 shadow_change] session=%llu frame=%u record_ms=%.3f reason=0x%X mode=0x%X camera=[%.3f %.3f %.3f] direction=[%.4f %.4f %.4f] sun_casters=[%llu %llu] sun_draws=[%llu %llu] matrix_hash=[%016llX %016llX] caster_signature=[%016llX %016llX] range_signature=[%016llX %016llX] local_faces=%u local_size=%u local_pool_revision=%llu",
                static_cast<unsigned long long>(epoch),t.frame,t.ms,t.reason,t.mode,t.position[0],t.position[1],t.position[2],t.direction[0],t.direction[1],t.direction[2],
                static_cast<unsigned long long>(t.casters[0]),static_cast<unsigned long long>(t.casters[1]),static_cast<unsigned long long>(t.draws[0]),static_cast<unsigned long long>(t.draws[1]),
                static_cast<unsigned long long>(t.matrix[0]),static_cast<unsigned long long>(t.matrix[1]),static_cast<unsigned long long>(t.ids[0]),static_cast<unsigned long long>(t.ids[1]),
                static_cast<unsigned long long>(t.ranges[0]),static_cast<unsigned long long>(t.ranges[1]),t.local_faces,t.local_size,static_cast<unsigned long long>(t.local_generation));}
        trace_count=trace_next=0;trace_overwritten=trace_changes=trace_epoch_discarded=0;
    }
};
}
#endif
