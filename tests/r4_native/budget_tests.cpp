#include "../r4_capture/fixture.h"
#include "../../xr_3da/KichR4CaptureData.h"
#include "../../xr_3da/KichR4Capture.h"
#include <stdio.h>
using namespace kich_r4_capture;
State* KichR4_CaptureState();
namespace {
int checks=0;
void Require(bool b,const char* text){++checks;if(!b)throw std::runtime_error(text);}
template<class F>std::string Failure(F f){try{f();}catch(const std::exception& e){return e.what();}throw std::runtime_error("expected budget failure");}
}
int main(){try{
    Batch b;b.Begin(800,600,42);
    b.indices.resize((32u*1024u*1024u)/4-3);
    b.Reserve(0,3,"exact boundary",2);
    const size_t old_size=b.indices.size();
    std::string why=Failure([&](){b.Reserve(0,4,"ui/icons",2);});
    Require(why.find("frame CPU geometry budget")!=std::string::npos && why.find("frame=42")!=std::string::npos,"original cause and frame retained");
    Require(why.find("used_bytes=33554420")!=std::string::npos && why.find("request_bytes=16")!=std::string::npos &&
        why.find("total_bytes=33554436")!=std::string::npos && why.find("limit_bytes=33554432")!=std::string::npos,"budget logs exact used/requested/limit bytes");
    Require(why.find("source=ui/icons")!=std::string::npos && why.find("layer=2")!=std::string::npos && b.indices.size()==old_size,"failed reservation identifies source without mutating geometry");
    b.Begin(800,600,43);why=Failure([&](){b.Reserve(UINT32_MAX,0,"huge request",0);});
    Require(why.find("request_bytes=240518168520")!=std::string::npos,"request byte arithmetic is 64 bit on Win32");
    b.commands.assign(16384,Command(0,0,0,"",Style()));why=Failure([&](){b.Reserve(0,0,"draw count",1);});
    Require(why.find("frame draw limit")!=std::string::npos && why.find("commands=16384")!=std::string::npos,"command limit remains independently diagnosed");
    KichR4_CaptureEnable(true);KichR4_CaptureBeginFrame(800,600,44);
    const auto raw=capture_fixture::Model(0);void* id=reinterpret_cast<void*>(1);
    KichR4_CaptureLoad(id,raw.data(),static_cast<uint32_t>(raw.size()),"actors/test_budget");
    State* state=KichR4_CaptureState();state->frame.indices.resize((32u*1024u*1024u)/4);
    ModelInput input={};input.visual=id;input.count=3;input.triangles=1;input.style=Style();
    const Matrix identity=kich_r4_scene::Identity();memcpy(input.world,identity.m,64);memcpy(input.vp,identity.m,64);
    KichR4_CaptureSubmitModel(input);
    const std::string error=KichR4_CapturedFrame()->error;
    Require(error.find("source=actors/test_budget")!=std::string::npos && error.find("frame CPU geometry budget")!=std::string::npos,"real capture model failure retains OGF identity through guards");
    Require(!KichR4_CapturedFrame()->Ready(),"overbudget frame is never silently accepted");
    KichR4_CaptureBeginFrame(800,600,45);Require(KichR4_CapturedFrame()->error.empty(),"next frame resets diagnostic state");
    KichR4_CaptureEnable(false);
    printf("G0 capture-budget diagnostics: %d checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
