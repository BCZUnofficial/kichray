#include "fixture.h"
#include "../../xr_3da/KichR4CaptureData.h"
#include "legacy_skin_reference.h"
#include <stdio.h>
#include <limits>
using namespace kich_r4_capture;
namespace {
int checks=0;std::vector<std::string> logs;
void Require(bool good,const char* why){++checks;if(!good)throw std::runtime_error(why);}
void Log(const char* text){logs.push_back(text);}
Span View(const fixture::Bytes& b){return Span(b.data(),static_cast<uint32_t>(b.size()));}
uint32_t Bits(float w){uint32_t bits;memcpy(&bits,&w,4);return bits;}
fixture::Bytes Model(uint16_t b0,uint16_t b1,uint32_t weight){fixture::Bytes raw=capture_fixture::Model(2);const Span v=Chunk(View(raw),3);
    const uint32_t offset=static_cast<uint32_t>(v.p-raw.data())+8;
    for(unsigned i=0;i<3;++i){fixture::Set(raw,offset+i*64,uint32_t(b0)|(uint32_t(b1)<<16));fixture::Set(raw,offset+i*64+52,weight);}return raw;}
void Compare(uint16_t b0,uint16_t b1,uint32_t bits){const fixture::Bytes raw=Model(b0,b1,bits);const std::shared_ptr<Mesh> mesh=Decode(View(raw));
    Matrix bones[2]={kich_r4_scene::Identity(),kich_r4_scene::Identity()};bones[0].m[12]=10;bones[1].m[12]=20;bones[1].m[5]=2;
    const Span source=Chunk(View(raw),3);vertBoned2W legacy_source[3];memcpy(legacy_source,source.p+8,sizeof(legacy_source));
    CBoneInstance legacy_bones[2];for(unsigned i=0;i<2;++i)legacy_bones[i].mRenderTransform.data=bones[i];
    vertRender output[3];xrSkin2W_x86(output,legacy_source,3,legacy_bones);
    Batch cpu;cpu.Begin(960,540,1);Style s;const Matrix identity=kich_r4_scene::Identity();
    cpu.Model(mesh.get(),*mesh,identity,identity,bones,2,3,3,1,s,KICH_R4_SKIN_SOFTWARE);
    for(unsigned i=0;i<3;++i){Require(fabsf(cpu.vertices[i].p[0]-output[i].P.x)<.0001f && fabsf(cpu.vertices[i].p[1]-output[i].P.y)<.0001f &&
        fabsf(cpu.vertices[i].p[2]-output[i].P.z)<.0001f,"CPU pose matches unchanged production xrSkin2W_x86");}
    if(b0!=b1){float w;memcpy(&w,&bits,4);for(unsigned i=0;i<3;++i)legacy_source[i].w=LegacyHardwareWeight(w);xrSkin2W_x86(output,legacy_source,3,legacy_bones);}
    Batch gpu;gpu.Begin(960,540,2);gpu.Model(mesh.get(),*mesh,identity,identity,bones,2,3,3,1,s,KICH_R4_SKIN_HARDWARE);
    for(unsigned i=0;i<3;++i)Require(fabsf(gpu.vertices[i].p[0]-output[i].P.x)<.0001f && fabsf(gpu.vertices[i].p[1]-output[i].P.y)<.0001f,
        "hardware pose matches production vertHW_2W byte quantization");
}
void Compatibility(){static_assert(sizeof(vertBoned2W)==64,"actual production source stride");static_assert(offsetof(vertBoned2W,w)==52,"actual production weight offset");
    const float weights[]={-.25f,-.00001f,0,.1f,.25f,.3f,.5f,.99999f,1,1.00001f,1.25f,100.f,-100.f};
    for(float w:weights){Compare(0,1,Bits(w));Compare(1,1,Bits(w));}
    Compare(1,1,0x7fc12345u);Compare(0,0,0x7f800000u);Compare(1,1,0xff800000u);Compare(0,0,0x7f812345u);
    const fixture::Bytes anomalous=Model(0,1,Bits(1.25f));State state;state.log=Log;state.Load(reinterpret_cast<void*>(1),View(anomalous),"actors\\outdoor_npc:2");
    Require(state.models.begin()->second.mesh && state.models.begin()->second.mesh->outside_weights==3,"finite out-of-range active weights accepted for legacy skin paths");
    Require(!logs.empty() && logs.back().find("actors\\outdoor_npc:2")!=std::string::npos && logs.back().find("weight=1.25")!=std::string::npos && logs.back().find("vertex=0 bones=0/1")!=std::string::npos,"compatibility log names exact model, vertex, bones and weight");
    state.Copy(reinterpret_cast<void*>(2),reinterpret_cast<void*>(1));state.models.erase(reinterpret_cast<void*>(1));
    Require(state.models.begin()->second.name=="actors\\outdoor_npc:2","model clone retains diagnostic source name");
    for(uint32_t bits:{0x7fc12345u,0x7f800000u,0xff800000u}){const fixture::Bytes bad=Model(0,1,bits);state.Load(reinterpret_cast<void*>(3),View(bad),"actors\\broken:1");
        const Entry& e=state.models.at(reinterpret_cast<void*>(3));Require(!e.mesh && e.error.find("model=actors\\broken:1")!=std::string::npos &&
            e.error.find("invalid active 2W weight: vertex=0 bones=0/1")!=std::string::npos,"active NaN/Inf remain rejected with model/vertex diagnostic");}
    const fixture::Bytes bad_index=Model(0,256,Bits(.25f));state.Load(reinterpret_cast<void*>(4),View(bad_index),"actors\\bad_bone:3");
    Require(!state.models.at(reinterpret_cast<void*>(4)).mesh && state.models.at(reinterpret_cast<void*>(4)).error.find("bones=0/256")!=std::string::npos,"invalid bone indices still rejected before pose reads");
    const fixture::Bytes ignored=Model(1,1,0x7fc12345u);state.Load(reinterpret_cast<void*>(5),View(ignored),"actors\\equal_bone:1");
    Require(state.models.at(reinterpret_cast<void*>(5)).mesh->ignored_weights==3,"nonfinite unused equal-bone field is safely ignored");
}
}
int main(){try{Compatibility();printf("B4.3b.1 actual legacy 2W reference / compatibility / diagnostics: %d checks passed\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
