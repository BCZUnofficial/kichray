#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <new>
#include "KichR4NormalMapProbe.h"

static unsigned checks=0,allocations=0;
#define CHECK(value) do{++checks;if(!(value)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#value);std::abort();}}while(0)
void* operator new(std::size_t size){++allocations;void* p=std::malloc(size?size:1);if(!p)throw std::bad_alloc();return p;}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
struct Sink {
    unsigned lines;char text[16][1600];
    Sink():lines(0){}
#if defined(__GNUC__)
    __attribute__((format(printf,2,3)))
#endif
    bool Emit(const char* format,...){CHECK(lines<16);va_list args;va_start(args,format);
        const int length=std::vsnprintf(text[lines++],sizeof(text[0]),format,args);va_end(args);CHECK(length>=0 && size_t(length)<sizeof(text[0]));return true;}
};
using namespace kich_r4_normal_probe;
static void Begin(uint32_t number){kich_r4_record::BeginFrame(number);Global().BeginFrame(number);}
int main(){
    const unsigned baseline=allocations;Frame& frame=Global();Input bad;bad.resource=bad.base=bad.bump=bad.auxiliary=reinterpret_cast<const char*>(1);
    frame.BeginFrame(1);frame.Observe(Model,bad);Sink off;EmitReport(off,1,true);
    CHECK(!frame.active && frame.categories[Model].draws==0 && off.lines==0);
    kich_r4_record::Request(true);Begin(1);CHECK(frame.Current(1));
    Input input;input.index_count=9;frame.Observe(Model,input);
    CHECK(frame.categories[Model].reasons[UnknownMaterial]==1);
    CHECK(frame.categories[Model].reasons[NoAuthoredBump]==0 && frame.categories[Model].authored==0);
    input.known=true;CHECK(Classify(input)==NoAuthoredBump);
    input.authored=true;CHECK(Classify(input)==MissingBasis);
    input.basis=true;CHECK(Classify(input)==MissingMetadata);
    input.metadata=true;CHECK(Classify(input)==MapsUnavailable);
    input.dependency=false;CHECK(Classify(input)==DependencyUnavailable);
    input.dependency=true;input.mapped=true;CHECK(Classify(input)==Mapped);
    input.path_enabled=false;CHECK(Classify(input)==PathDisabled);
    input.known=false;CHECK(Classify(input)==UnknownMaterial);
    input.known=true;input.path_enabled=true;input.identity=17;input.attributes=7;input.flags=1;
    input.resource="model\\actor";input.base="base";input.bump="bump";input.auxiliary="bump#";
    frame.Observe(Model,input);CHECK(frame.categories[Model].draws==2 && frame.categories[Model].mapped==1);
    CHECK(frame.categories[Model].mapped_indices==9 && frame.categories[Model].indices==18);
    CHECK(frame.categories[Model].example.reason==UnknownMaterial); // Keep the actionable first failure.
    input.mapped=false;input.basis=false;input.index_count=3;frame.Observe(Model,input);
    CHECK(frame.categories[Model].reasons[MissingBasis]==1 && frame.categories[Model].example.reason==MissingBasis);
    CHECK(frame.categories[Model].authored==2 && frame.categories[Model].mapped==1); // Valid/bad partition subsets.
    char long_name[400];std::memset(long_name,'x',sizeof(long_name));long_name[sizeof(long_name)-1]=0;long_name[1]='\n';long_name[2]='"';
    input.basis=true;input.resource=long_name;frame.Observe(Model,input);
    CHECK(frame.categories[Model].example.reason==MapsUnavailable && frame.categories[Model].example.truncated);
    CHECK(std::strlen(frame.categories[Model].example.resource)==95 && frame.categories[Model].example.resource[1]=='_' && frame.categories[Model].example.resource[2]=='_');
    // Same-priority samples must not copy names or allocate, however many draws.
    input.resource=input.base=input.bump=input.auxiliary=reinterpret_cast<const char*>(1);
    for(unsigned i=0;i<10000;++i)frame.Observe(Model,input);
    CHECK(frame.categories[Model].draws==10004 && frame.categories[Model].example.identity==17);
    Input unknown;unknown.base="unresolved";frame.Observe(Static,unknown);
    Input disabled;disabled.known=true;disabled.path_enabled=false;frame.Observe(HUD,disabled);
    Input terrain;terrain.known=terrain.authored=terrain.basis=terrain.metadata=true;terrain.dependency=false;terrain.index_count=300;frame.Observe(Terrain,terrain);
    CHECK(frame.categories[Terrain].reasons[DependencyUnavailable]==1 && frame.categories[Terrain].reasons[MapsUnavailable]==0);
    CHECK(frame.categories[HUD].reasons[PathDisabled]==1 && frame.categories[HUD].reasons[NoAuthoredBump]==0);
    Sink report;EmitReport(report,1,true);CHECK(report.lines==8);
    CHECK(std::strstr(report.text[1],"UNKNOWN_MATERIAL") && std::strstr(report.text[3],"MAPS_UNAVAILABLE"));
    CHECK(std::strstr(report.text[7],"TERRAIN_MASK_OR_DIFFUSE_UNAVAILABLE"));
    CHECK(allocations==baseline);
    Sink stale;EmitReport(stale,2,true);CHECK(stale.lines==1 && std::strstr(stale.text[0],"no_current_submission_samples"));
    Sink no_world;EmitReport(no_world,1,false);CHECK(no_world.lines==1);
    Begin(2);CHECK(frame.categories[Model].draws==0 && !frame.categories[Model].example.valid && frame.Current(2));
    frame.Observe(Category(99),bad);CHECK(frame.categories[Model].draws==0);
    kich_r4_record::Request(false);Begin(3);frame.Observe(Model,bad);Sink off_again;EmitReport(off_again,3,true);CHECK(off_again.lines==0 && !frame.Current(3));
    kich_r4_record::Request(true);kich_r4_record::BeginFrame(2);frame.Observe(Model,bad);CHECK(!frame.Current(2)); // Same numeric frame, different epoch.
    frame.BeginFrame(2);CHECK(frame.Current(2));
    Begin(UINT32_MAX);CHECK(frame.Current(UINT32_MAX));Begin(0);CHECK(frame.Current(0));
    CHECK(frame.categories[Terrain].draws==0 && allocations==baseline);
    std::printf("PASS: bounded normal-map probe (%u checks), OFF/epoch/frame guards, cause classification and four examples\n",checks);
}
