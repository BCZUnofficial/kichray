static void Capture(Device& d,uint64_t id,bool menu=false){KichR4TelemetryContext capture={};capture.struct_size=sizeof(capture);capture.capture_id=id;capture.engine_frame=UINT64_C(9007199254740999);capture.render_frame=UINT64_C(9007199254741999);capture.flags=menu?KICH_R4_TELEMETRY_MENU:0;assert(SetTelemetryCapture(1,&capture,NULL)==0);assert(!d.number_calls && !d.hardware.adapter_queries && !d.hardware.feature_queries);assert(NativeProfileMark(1,0)==0);for(unsigned i=1;i<8;++i)assert(NativeProfileMark(1,i)==0);}
static KichR4TelemetryTiming Poll(){KichR4TelemetryTiming r={};assert(PollTelemetryTiming(1,&r,sizeof(r))==0);return r;}
static void Sink(const KichR4TelemetryRecord*){}
int main(){
    assert(TelemetryHash("hello",5)==UINT64_C(0xa430d84680aabd0b));
    assert(!TelemetrySubresourcesOverlap(0,1,1,1));assert(TelemetrySubresourcesOverlap(UINT32_MAX,1,UINT32_MAX,1));
    KichR4TelemetryContext c={};c.struct_size=sizeof(c);assert(TelemetryContextValid(c));c.flags=4;assert(!TelemetryContextValid(c));
    {Device d;current=&d;Capture(d,UINT64_C(9007199254743999));assert(Poll().status==0);d.commands.ready=true;
        KichR4GPUProfile legacy={};assert(GetNativeProfile(1,&legacy,sizeof(legacy))==0 && !legacy.ready);
        KichR4TelemetryTiming r=Poll();assert(r.status==KICH_R4_TIMING_READY && r.capture_id==UINT64_C(9007199254743999) && r.engine_frame==d.telemetry.engine_frame && r.render_frame==d.telemetry.render_frame && r.valid_mask==127);for(unsigned i=0;i<7;++i)assert(r.milliseconds[i]==10);assert(!Poll().status);}
    {Device d;current=&d;Capture(d,2,true);d.commands.ready=true;KichR4TelemetryTiming r=Poll();assert(r.status==KICH_R4_TIMING_READY && r.valid_mask==1 && r.milliseconds[0]==10 && !r.milliseconds[1]);}
    {Device d;current=&d;Capture(d,3);d.commands.ready=true;d.commands.disjoint=true;assert(Poll().status==KICH_R4_TIMING_DISJOINT);}
    {Device d;current=&d;Capture(d,4);d.commands.ready=true;d.commands.frequency=0;assert(Poll().status==KICH_R4_TIMING_DISJOINT);}
    {Device d;current=&d;Capture(d,5);for(unsigned i=0;i<119;++i)assert(!Poll().status);assert(Poll().status==KICH_R4_TIMING_TIMEOUT && !d.profile[0].pending && !d.profile[0].disjoint);assert(d.commands.reads==120);}
    {Device d;current=&d;for(unsigned i=1;i<=4;++i)Capture(d,i);d.telemetry.capture_id=5;assert(NativeProfileMark(1,0)==S_FALSE);KichR4TelemetryTiming r=Poll();assert(r.status==KICH_R4_TIMING_TIMEOUT && r.capture_id==5 && d.commands.begins==4);}
    {Device d;current=&d;d.hardware.fail_call=4;d.telemetry.capture_id=7;assert(NativeProfileMark(1,0)<0);assert(Poll().status==KICH_R4_TIMING_UNSUPPORTED && !d.profile_open && !d.profile[0].disjoint);}
    {Device d;current=&d;Capture(d,8);d.commands.failed=true;assert(Poll().status==KICH_R4_TIMING_UNSUPPORTED);}
    {Device d;current=&d;Capture(d,0);d.commands.ready=true;KichR4GPUProfile legacy={};assert(GetNativeProfile(1,&legacy,sizeof(legacy))==0 && legacy.ready);assert(!Poll().status);}
    {Device d;current=&d;const uint64_t token=UINT64_C(0x8000000000000017);Capture(d,token);
        // OFF is not a resource reset and may not reassign an in-flight query.
        assert(SetTelemetryCapture(1,NULL,NULL)==0 && d.profile[0].pending);
        Capture(d,19);d.commands.ready=true;
        KichR4TelemetryTiming record=Poll(),snapshot=Poll();
        assert(record.capture_id==token && record.status==KICH_R4_TIMING_READY);
        assert(snapshot.capture_id==19 && snapshot.status==KICH_R4_TIMING_READY);
        assert(!Poll().status && d.hardware.calls==18 && d.commands.begins==2);
        Capture(d,UINT64_C(0x8000000000000020));assert(!d.profile[0].pending && !d.profile[1].pending);}
    {Device d;current=&d;KichR4TelemetryContext capture={};capture.struct_size=sizeof(capture);capture.capture_id=UINT64_C(0x8000000000000031);capture.engine_frame=37;capture.render_frame=41;
        assert(TelemetryContextValid(capture));assert(SetTelemetryCapture(1,&capture,NULL)==0);
        assert(NativeProfileMark(1,0)==0 && NativeProfileMark(1,1)==0);
        assert(SetTelemetryCapture(1,NULL,NULL)==0 && !d.profile_open && !d.profile[0].pending);
        KichR4TelemetryTiming r=Poll();assert(r.status==KICH_R4_TIMING_ABORTED && r.capture_id==capture.capture_id && r.engine_frame==37 && r.render_frame==41);
        assert(!Poll().status);assert(SetTelemetryCapture(1,NULL,NULL)==0 && !Poll().status);}
    {Device d;current=&d;KichR4TelemetryContext capture={};capture.struct_size=sizeof(capture);capture.capture_id=53;capture.engine_frame=59;
        assert(SetTelemetryCapture(1,&capture,Sink)==0 && !d.number_calls && !d.hardware.adapter_queries);
        capture.flags=4;assert(SetTelemetryCapture(1,&capture,NULL)==KICH_R4_E_ARGUMENT);
        assert(d.telemetry.flags==0 && d.telemetry.capture_id==53 && d.telemetry.engine_frame==59);
        assert(SetTelemetryCapture(1,NULL,Sink)==KICH_R4_E_ARGUMENT && d.telemetry_sink==Sink);
        capture.flags=KICH_R4_TELEMETRY_DETAILED;assert(SetTelemetryCapture(1,&capture,Sink)==0);
        assert(d.number_calls && d.hardware.adapter_queries==1 && d.hardware.feature_queries==1);}
    {Device d;current=&d;KichR4TelemetryContext capture={};capture.struct_size=sizeof(capture);capture.capture_id=61;capture.engine_frame=67;
        assert(SetTelemetryCapture(1,&capture,NULL)==0);assert(SetTelemetryCapture(1,NULL,NULL)==0);
        KichR4TelemetryTiming r=Poll();assert(r.status==KICH_R4_TIMING_ABORTED && r.capture_id==61 && r.engine_frame==67);
        assert(!d.hardware.calls && !d.commands.reads && !d.commands.begins);}
    {uint64_t stamps[8]={10,5,20,30,40,50,60,70};double ms[7]={};assert(TelemetryTimestampResult(false,1000,stamps,127,ms)==KICH_R4_TIMING_DISJOINT);}
    puts("PASS: actual provider timestamp bodies: exact capture identity/high-bit tokens, timing-only no formatting, OFF/resume ownership, async readiness, disjoint/unsupported, bounded timeout/ring, no forced wait, legacy ownership");
}
