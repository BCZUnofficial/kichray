int main(){
    Context context;
    Reset(context);PollTimings();assert(!poll_calls);
    KichR4GameFrame frame={};frame.engine_frame=41;frame.active=frame.allowed=true;
    BeginTelemetry(frame,false,1000);assert(!context.deferred.ordinary_profile && !native.timings.count && !submitted_capture.capture_id);
    {
        Reset(context);const auto p=Pending(7);assert(native.timings.Add(p,0));delivered=Result(p);PollTimings();
        assert(poll_calls==1 && native.timings.count==0 && snapshot_results.size()==1 && snapshot_results[0].status=="READY");
    }
    {
        Reset(context);Enable(3);BeginTelemetry(frame,false,1000);
        assert(context.deferred.ordinary_profile && native.timings.count==1 && native.timings.pending[0].record_epoch==3);
        context.deferred.Mark(context.frame,0);assert(context.deferred.profile_context_valid && profile_calls==1);
        const auto p=native.timings.pending[0].context;assert(submitted_capture.capture_id==p.capture_id && p.engine_frame==41);
        delivered=Result(p);++delivered.render_frame;PollTimings();assert(native.timings.count==1);
        delivered=Result(p);++delivered.engine_frame;PollTimings();assert(native.timings.count==1);
        delivered=Result(p);native.current_engine_frame=47;PollTimings();
        assert(!native.timings.count && snapshot_results.empty());
        assert(record_lines.back().find("frame=41 ")!=std::string::npos && record_lines.back().find("age_frames=6 ")!=std::string::npos);
        assert(record_lines.back().find("resource_shadow_ms=1.000")!=std::string::npos);
    }
    {
        Reset(context);Enable(5);const auto p=Pending(UINT64_C(0x8000000000000011));assert(native.timings.Add(p,3));
        delivered=Result(p);PollTimings();assert(!native.timings.count && record_lines.empty() && snapshot_results.empty());
    }
    {
        Reset(context);Enable(7);const auto p=Pending(UINT64_C(0x8000000000000001));assert(native.timings.Add(p,0));
        delivered=Result(p);PollTimings();assert(snapshot_results.size()==1 && record_lines.empty());
    }
    {
        Reset(context);g_render_snapshot_active=true;snapshot_detailed=true;
        BeginTelemetry(frame,false,1000);assert(native.timings.count==1 && native.timings.pending[0].record_epoch==0);
        assert(snapshot_results.size()==1 && snapshot_results[0].status=="NOT_READY");
        assert(submitted_capture.flags==KICH_R4_TELEMETRY_DETAILED && submitted_capture.capture_id==snapshot_id);
    }
    {
        Reset(context);Enable(9);set_error=-1;BeginTelemetry(frame,false,1000);
        assert(!context.deferred.ordinary_profile && !native.timings.count && snapshot_results.empty());
        context.deferred.Mark(context.frame,0);assert(!context.deferred.profile_context_valid && !profile_calls);
        assert(record_lines.back().find("UNSUPPORTED")!=std::string::npos);
    }
    {
        // A new snapshot cannot erase an older request merely by sharing its
        // numeric ID when registering the new context fails.
        Reset(context);Enable(11);const auto older=Pending(7,29,31);assert(native.timings.Add(older,11));
        g_render_snapshot_active=true;set_error=-1;BeginTelemetry(frame,false,1000);
        assert(native.timings.count==1 && native.timings.Find(Result(older))==0);
        context.deferred.Mark(context.frame,0);assert(!context.deferred.profile_context_valid && !profile_calls);
    }
    {
        // A successful prior frame must not leave stale snapshot profiling enabled
        // when the optional API is unavailable on the next attempt.
        Reset(context);g_render_snapshot_active=true;BeginTelemetry(frame,false,1000);
        context.deferred.Mark(context.frame,0);assert(profile_calls==1);
        native.telemetry.set_capture=NULL;BeginTelemetry(frame,false,1001);
        context.deferred.Mark(context.frame,0);assert(profile_calls==1 && !context.deferred.profile_context_valid);
        assert(snapshot_results.back().status=="UNSUPPORTED");
    }
    {
        // Positive non-OK results also cannot submit timestamps against stale context.
        Reset(context);g_render_snapshot_active=true;set_error=1;BeginTelemetry(frame,true,1000);
        context.deferred.Mark(context.frame,0);assert(!profile_calls && !context.deferred.profile_context_valid);
    }
    {
        Reset(context);Enable(13);assert(native.timings.Add(Pending(7),0));assert(native.timings.Add(Pending(13),13));assert(native.timings.Add(Pending(17),11));
        CancelTimings("CANCELLED");assert(!native.timings.count && snapshot_results.size()==1 && record_lines.size()==1);
        assert(record_lines[0].find("session=13 ")!=std::string::npos && record_lines[0].find("token=13 ")!=std::string::npos);
    }
    {
        Reset(context);Enable(15);native.present_frame=3;
        auto screenshot=Pending(23,41,3);screenshot.present_frame=3;
        auto previous=Pending(29,41,2);previous.present_frame=2;
        auto snapshot=Pending(31,41,11);snapshot.present_frame=3;
        assert(native.timings.Add(screenshot,15) && native.timings.Add(previous,15) && native.timings.Add(snapshot,0));
        context.deferred.ordinary_profile=true;ExcludeRecordTiming(41,"EXCLUDED_SCREENSHOT");
        assert(native.timings.count==2 && !context.deferred.ordinary_profile);
        assert(native.timings.Find(Result(screenshot))==native.timings.count);
        assert(native.timings.Find(Result(previous))==0 && native.timings.Find(Result(snapshot))==1);
        assert(record_lines.size()==1 && record_lines[0].find("EXCLUDED_SCREENSHOT")!=std::string::npos);
        delivered=Result(screenshot);PollTimings();assert(native.timings.count==2 && record_lines.size()==1 && snapshot_results.empty());
        delivered=Result(snapshot);PollTimings();assert(snapshot_results.size()==1 && native.timings.count==1);
    }
    {
        Reset(context);Enable(16);assert(native.timings.Add(Pending(7),0));assert(native.timings.Add(Pending(13),16));
        poll_error=-1;PollTimings();assert(!native.timings.count && snapshot_results.size()==1 && snapshot_results[0].status=="UNSUPPORTED");
        assert(record_lines[0].find("timing_poll_failed")!=std::string::npos);
        assert(record_lines[0].find("not_device_reset_evidence")!=std::string::npos);
        const size_t lines=record_lines.size();PollTimings();PollTimings();assert(record_lines.size()==lines);
        ++native.record.epoch;PollTimings();assert(record_lines.size()==lines+1);
        ++native.level_generation;PollTimings();assert(record_lines.size()==lines+2);
        ++native.device_generation;PollTimings();assert(record_lines.size()==lines+3);
    }
    for(unsigned mode=0;mode<7;++mode){
        Reset(context);Enable(17);KichR4GameFrame input=frame;bool menu=false;
        switch(mode){case 0:menu=true;break;case 1:input.allowed=false;break;case 2:input.active=false;break;case 3:input.warmup=true;break;case 4:native.debug=8;break;case 5:input.observation.paused=true;break;case 6:input.observation.console_visible=true;break;}
        BeginTelemetry(input,menu,1000);assert(!context.deferred.ordinary_profile && !native.timings.count && !submitted_capture.capture_id);
    }
    puts("PASS: actual presenter GPU routing: OFF/snapshot exceptions, exact original frame, epoch ownership, sparse eligibility, API failures, snapshot collision isolation, late screenshot exclusion");
}
