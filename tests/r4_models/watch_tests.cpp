#include "../../xr_3da/KichR4Watch.h"
#include <cstdlib>
#include <new>
#include <stdexcept>
namespace {
unsigned checks=0;size_t allocations=0;bool count_allocations=false;unsigned lines=0;
char output[32768];size_t used=0;
void Check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
void Log(const char* line){Check(std::strlen(line)<900,"line fits nontruncating bounded sink");++lines;const size_t length=std::strlen(line);if(used+length+2<sizeof(output)){std::memcpy(output+used,line,length);used+=length;output[used++]='\n';output[used]=0;}}
void ClearLog(){lines=0;used=0;output[0]=0;}
void Enable(bool enabled=true){kich_r4_record::Request(enabled);kich_r4_record::BeginFrame(0);}
kich_r4_capture::NativeModelCommand Model(uint64_t serial,float z=1.f){kich_r4_capture::NativeModelCommand c;c.mesh=std::make_shared<kich_r4_capture::Mesh>();c.visual_id=123;c.lifecycle_serial=serial;c.identity_measured=true;c.source_frame=10;c.resource="actor/test";c.shader="models/test";c.count=3;c.bounds_known=c.bounds.valid=true;
    c.bounds.min[0]=c.bounds.min[1]=-.2f;c.bounds.max[0]=c.bounds.max[1]=.2f;c.bounds.min[2]=z-.1f;c.bounds.max[2]=z+.1f;c.object_light_valid=true;c.object_hemi=.25f;c.object_sun=.5f;return c;}
kich_r4_lighting::Constants Constants(){kich_r4_lighting::Constants c={};c.shadow[0]=c.shadow[1]=kich_r4_scene::Identity();c.sun_color[3]=1;c.material[0]=6;return c;}
const float camera[]={0,0,0},direction[]={0,0,1},controlled[]={0,0,-10};
void AcquisitionAndOwnership(){Enable();kich_r4_watch::Tracker t;kich_r4_capture::Batch b;b.Begin(640,480,10);b.native_models.push_back(Model(1,3));b.native_models.push_back(Model(2,.5f));b.native_models.push_back(Model(3,.2f));b.native_models.back().layer=1;
    t.Request(true);const long owners=b.native_models[1].mesh.use_count();t.Begin(10,1000,4,b,camera,direction,controlled);Check(t.Active() && t.TargetLifetime()==2,"nearest camera-ray bounds candidate selected and HUD excluded");Check(b.native_models[1].mesh.use_count()==owners,"watch weak ownership does not retain geometry");Check(t.Latest().actor_known && t.Latest().actor_distance>10,"controlled and render camera distance separate");
    KichR4DeferredDrawDesc draw={};draw.textures[3]=11;draw.textures[4]=12;draw.state.draw.index_count=3;auto& target=b.native_models[1];target.has_material=true;target.material.lightmap="lmap";target.material.hemi="hemi";target.available_attributes=kich_r4_material::ATTR_UV1;
    t.ObserveModel(target,0,Constants(),draw);t.Finish(Constants());Check(t.Latest().passes[0].handles[3]==11 && t.Latest().passes[0].flags==6,"records actual successful material binding values");
    ClearLog();t.Report(1000,Log);Check(std::strstr(output,"lightmap=BOUND") && std::strstr(output,"hemi_map=BOUND"),"bound features and source flags reported");Check(std::strstr(output,"PCF_VISIBILITY_NOT_READ") && std::strstr(output,"occlusion=UNKNOWN"),"CPU proxy truthfully excludes pixel visibility");const unsigned before=lines;t.Report(1199,Log);Check(lines==before,"five Hz upper bound");
    b.native_models[1].lifecycle_serial=20;t.Begin(10,1200,4,b,camera,direction);Check(t.State()==kich_r4_watch::Lost,"same process pointer/sharedmesh new lifetime cannot inherit lock");t.Begin(10,1400,4,b,camera,direction);Check(t.State()==kich_r4_watch::Lost,"lost target never silently replaced");
    t.Request(true);t.Begin(10,1600,4,b,camera,direction);Check(t.TargetLifetime()==20,"explicit reacquisition permitted");t.Begin(10,1800,5,b,camera,direction);Check(t.State()==kich_r4_watch::Lost,"level revision invalidates target");
}
void ShortEventsNoAllocations(){Enable();kich_r4_watch::Tracker t;kich_r4_capture::Batch b;b.Begin(640,480,10);b.native_models.push_back(Model(7,.5f));t.Request(true);t.Begin(10,1000,1,b,camera,direction);t.Finish(Constants());ClearLog();t.Report(1000,Log);
    allocations=0;count_allocations=true;
    for(unsigned i=0;i<24;++i){b.engine_frame=11+i;auto& m=b.native_models[0];m.source_frame=b.engine_frame;m.first=i%2?0:3;m.count=i%2?3:6;m.object_hemi=i%2?.2f:.8f;
        t.Begin(b.engine_frame,1001+i,1,b,camera,direction);auto constants=Constants();constants.shadow[0].m[12]=i%2?.95f:0;
        KichR4DeferredDrawDesc draw={};draw.state.draw.index_count=m.count;draw.textures[0]=i%2?100:101;t.ObserveModel(m,0,constants,draw);t.Finish(constants);}
    count_allocations=false;Check(allocations==0,"per-frame watch sample/binding/event collection allocates nothing");Check(t.PendingEvents()==8 && t.OmittedEvents()==16,"short changes bounded and omitted events explicit");
    t.Report(1400,Log);Check(std::strstr(output,"time_ms=1001 frame=11")!=NULL,"events carry observed timestamp not drain timestamp");Check(std::strstr(output,"omitted_events=16")!=NULL,"event loss visible");
    Enable(false);const auto state=t.Latest().frame;t.Begin(100,1600,1,b,camera,direction);Check(t.Latest().frame==state && !t.Active(),"disabled recorder stops collection");Enable();t.Begin(34,1700,1,b,camera,direction);Check(t.State()==kich_r4_watch::Lost,"recording epoch does not silently replay old target");
}
void MissingAndInvalid(){Enable();kich_r4_watch::Tracker t;kich_r4_capture::Batch b;b.Begin(1,1,10);b.native_models.push_back(Model(0));t.Request(true);t.Begin(10,1000,1,b,camera,direction);Check(t.State()==kich_r4_watch::NoCandidate,"unknown lifetime never claimed stable");
    b.native_models[0].lifecycle_serial=1;t.Begin(10,1200,1,b,camera,direction);Check(t.State()==kich_r4_watch::NoCandidate,"no-candidate result requires user retry");t.Request(true);t.Begin(10,1400,1,b,camera,direction);Check(t.Active(),"valid explicit retry");b.native_models.clear();t.Begin(11,1600,1,b,camera,direction);Check(t.State()==kich_r4_watch::Lost,"missing frame target becomes lost");
    Check(!std::strcmp(kich_r4_watch::MapState(true,false,0,2),"NO_UV1"),"missing source UV distinct");Check(!std::strcmp(kich_r4_watch::MapState(false,true,0,2),"NOT_AUTHORED"),"absent source map distinct");
    kich_r4_capture::State registry;registry.models[(void*)1]=kich_r4_capture::Entry();registry.models[(void*)1].lifecycle_serial=kich_r4_capture::NextModelLifetime();registry.Copy((void*)2,(void*)1);const uint64_t first=registry.models[(void*)2].lifecycle_serial;Check(first && first!=registry.models[(void*)1].lifecycle_serial,"model copies get independent lifetimes");registry.models.erase((void*)2);registry.Copy((void*)2,(void*)1);Check(registry.models[(void*)2].lifecycle_serial!=first,"registry pointer reuse gets new lifetime");
}
void SuspendedFrames(){Enable();kich_r4_watch::Tracker t;kich_r4_capture::Batch b;b.Begin(1,1,10);b.native_models.push_back(Model(77,.5f));t.Request(true);t.Begin(10,1000,1,b,camera,direction);t.Finish(Constants());ClearLog();t.Report(1000,Log);
    t.Suspend(11,1100,"MENU_OR_WARMUP");ClearLog();t.Report(1300,Log);Check(std::strstr(output,"status=SUSPENDED") && !std::strstr(output,"watch_sample"),"menu reports a gap without replaying stale locked sample");Check(t.State()==kich_r4_watch::Locked && !t.Active(),"menu preserves lifetime lock without draw hook activity");
    t.Suspend(12,1400,"MENU_OR_WARMUP");ClearLog();t.Report(1500,Log);Check(lines==0,"repeated gap emits no unbounded notices");
    b.engine_frame=b.native_models[0].source_frame=13;t.Begin(13,1600,1,b,camera,direction);t.Finish(Constants());ClearLog();t.Report(1700,Log);Check(std::strstr(output,"status=LOCKED") && std::strstr(output,"RESUMED_AFTER_FRAME_GAP") && std::strstr(output,"frame=13"),"completed resumed frame keeps exact target and announces gap");
    b.engine_frame=b.native_models[0].source_frame=14;t.Begin(14,1800,1,b,camera,direction);t.Suspend(14,1800,"BEGIN_CLEAR_OCCLUDED");KichR4DeferredDrawDesc draw={};t.ObserveModel(b.native_models[0],0,Constants(),draw);t.Finish(Constants());ClearLog();t.Report(2000,Log);Check(!t.Active() && std::strstr(output,"status=SUSPENDED") && !std::strstr(output,"watch_sample"),"aborted frame cannot acquire later menu draw or publish unfinished state");
}
}
void* operator new(size_t n){if(count_allocations)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
// Keep allocator observation outside compiler allocation-pair folding.
__attribute__((noinline)) void FreeTestMemory(void* p){std::free(p);}
void operator delete(void* p)noexcept{FreeTestMemory(p);}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete[](void* p)noexcept{::operator delete(p);}
int main(){try{AcquisitionAndOwnership();ShortEventsNoAllocations();MissingAndInvalid();SuspendedFrames();Enable(false);printf("E1.7 watch: %u checks passed\n",checks);return 0;}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
