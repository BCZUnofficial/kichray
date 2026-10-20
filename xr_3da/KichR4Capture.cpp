/* EXE-owned CPU mirror. Borrowed stream pointers are consumed before Unlock. */
#include "KichR4Capture.h"
#include "KichR4CaptureData.h"
namespace {
kich_r4_capture::State* capture=NULL;
template<class F>void GuardCapture(F action){if(!capture)return;try{action(*capture);}
    catch(const std::exception& e){try{capture->frame.Fail(e.what());}catch(...) {}}
    catch(...){try{capture->frame.Fail("CPU capture exception");}catch(...) {}}}
}
kich_r4_capture::State* KichR4_CaptureState(){return capture;}
const kich_r4_capture::Batch* KichR4_CapturedFrame(){return capture?&capture->frame:NULL;}
void KichR4_CaptureEnable(bool enabled,void (*log)(const char*)){if(enabled){if(!capture)try{capture=new kich_r4_capture::State();}catch(...) {}if(capture)capture->log=log;}
    else {delete capture;capture=NULL;}}
bool KichR4_CaptureActive(){return capture && capture->frame.width && capture->frame.error.empty();}
void KichR4_CaptureBeginFrame(uint32_t w,uint32_t h,uint32_t frame){GuardCapture([&](kich_r4_capture::State& c){c.Begin(w,h,frame);});}
void KichR4_CaptureHUD(bool begin){GuardCapture([&](kich_r4_capture::State& c){c.frame.ui_seen=true;
    if(begin)++c.frame.ui_scope;else {kich_r4_level::Check(c.frame.ui_scope!=0,"HUD scope imbalance");--c.frame.ui_scope;}});}
void KichR4_CaptureLoad(void* visual,const void* data,uint32_t bytes,const char* name){GuardCapture([&](kich_r4_capture::State& c){
    kich_r4_level::Check(visual && data && bytes,"empty model source");c.Load(visual,kich_r4_level::Span(static_cast<const unsigned char*>(data),bytes),name);});}
void KichR4_CaptureCopy(void* visual,const void* source){GuardCapture([&](kich_r4_capture::State& c){c.Copy(visual,source);});}
void KichR4_CaptureForget(void* visual){if(capture)capture->models.erase(visual);}
void KichR4_CaptureLock(void* buffer,const void* data,uint32_t at,uint32_t bytes,uint32_t discard){GuardCapture([&](kich_r4_capture::State& c){c.Lock(buffer,data,at,bytes,discard);});}
void KichR4_CaptureUnlock(void* buffer,uint32_t bytes){GuardCapture([&](kich_r4_capture::State& c){c.Unlock(buffer,bytes);});}
void KichR4_CaptureBuffer(void* buffer,const void* data,uint32_t bytes){GuardCapture([&](kich_r4_capture::State& c){
    kich_r4_capture::Buffer& b=c.buffers[buffer];b.segments.clear();b.Put(data,0,bytes);b.fixed=true;});}
void KichR4_CaptureForgetBuffer(void* buffer){if(capture)capture->buffers.erase(buffer);}
void KichR4_CaptureFail(const char* why){GuardCapture([&](kich_r4_capture::State& c){c.frame.Fail(why);});}
void KichR4_CaptureModelFail(void* visual,const char* why){GuardCapture([&](kich_r4_capture::State& c){
    const std::map<const void*,kich_r4_capture::Entry>::const_iterator found=c.models.find(visual);
    c.frame.Fail(found==c.models.end()?why:kich_r4_capture::ModelError(found->second,why).c_str());});}
bool KichR4_CaptureUIActive(){return KichR4_CaptureActive() && capture->frame.ui_scope!=0;}
bool KichR4_CaptureModelNeeded(void* visual,uint32_t& bones){bool needed=false;bones=0;
    GuardCapture([&](kich_r4_capture::State& c){c.frame.renderer_seen=true;
        const std::map<const void*,kich_r4_capture::Entry>::const_iterator found=c.models.find(visual);
        kich_r4_level::Check(found!=c.models.end(),"visual missing CPU registration; rebuild XR_3DA and renderer");
        kich_r4_level::Check(found->second.error.empty(),found->second.error.c_str());
        if(found->second.mesh){bones=found->second.mesh->bones;needed=true;}});return needed;
}
void KichR4_CaptureSubmitModel(const kich_r4_capture::ModelInput& input){GuardCapture([&](kich_r4_capture::State& c){
    using namespace kich_r4_capture;const std::map<const void*,Entry>::const_iterator found=c.models.find(input.visual);
    Check(found!=c.models.end() && found->second.mesh,"model registration changed during capture");
    Matrix world,vp;memcpy(world.m,input.world,sizeof(world));memcpy(vp.m,input.vp,sizeof(vp));
    Matrix bones[256];Check(input.bone_count<=256 && (!input.bone_count || input.bones),"model pose snapshot");
    if(input.bone_count)memcpy(bones,input.bones,input.bone_count*sizeof(Matrix));
    Mesh& mesh=*found->second.mesh;
    if(mesh.two_weights && (mesh.ignored_weights || mesh.outside_weights) && input.skin_mode>=1 && input.skin_mode<=2){
        const uint32_t bit=1u<<input.skin_mode;if(!(mesh.diagnostic_modes&bit)){mesh.diagnostic_modes|=bit;
            c.Diagnostic("2W skin path: model="+found->second.name+"; mode="+(input.skin_mode==KICH_R4_SKIN_HARDWARE?"hardware clamp/u8":"software raw")+"; "+mesh.first_weight_detail);}}
    try{c.frame.Model(input.visual,mesh,world,vp,bones,input.bone_count,input.count,input.first,input.triangles,input.style,input.skin_mode);}
    catch(const std::exception& e){c.frame.Fail(ModelError(found->second,e.what()).c_str());}
});}

void KichR4_CaptureSubmitUI(const kich_r4_capture::UIInput& input) {
    GuardCapture([&](kich_r4_capture::State& c){
        using namespace kich_r4_capture;Batch& frame=c.frame;if(!frame.ui_scope || !input.primitives)return;
        const uint32_t stride=input.stride,topology=input.topology,base=input.base,start=input.start,vertex_count=input.vertex_count,first=input.first,primitives=input.primitives;
        const uint32_t position=input.position,color=input.color,uv=input.uv;const bool indexed=input.indexed;
        Check(topology>=2 && topology<=6 && primitives<=131072,"HUD primitive topology/count");
        const uint32_t count=indexed?vertex_count:topology==2?primitives*2:topology==3?primitives+1:topology==4?primitives*3:primitives+2;
        Check(count && count<=262144 && stride>=20 && stride<=2048,"HUD vertex layout/count");
        Check(position!=UINT32_MAX && position<=stride-16 && color!=UINT32_MAX && color<=stride-4 &&
            (uv==UINT32_MAX || uv<=stride-8),"HUD vertex semantics");
        const std::map<void*,Buffer>::const_iterator source=c.buffers.find(input.vb);Check(source!=c.buffers.end(),"HUD vertex stream missing CPU mirror");
        const Style& style=input.style;std::string texture;
        if(input.texture && *input.texture && strcmp(input.texture,"$null")){texture=kich_r4_level::Lower(input.texture);
            if(texture.size()>4 && texture.substr(texture.size()-4)==".dds")texture.resize(texture.size()-4);
            Check(kich_r4_level::SafeName(texture) && texture[0]!='$',"HUD texture path");}
        Check(uint64_t(base)+start+count<=UINT32_MAX/stride,"HUD vertex offset overflow");
        frame.Reserve(count,primitives*6);std::vector<Vertex> data;data.reserve(count);
        for(uint32_t i=0;i<count;++i){const unsigned char* p=source->second.Read((base+start+i)*stride,stride);float xyzw[4],tc[2]={};uint32_t argb;
            memcpy(xyzw,p+position,16);memcpy(&argb,p+color,4);if(uv!=UINT32_MAX)memcpy(tc,p+uv,8);
            data.push_back(frame.Screen(xyzw[0],xyzw[1],xyzw[2],xyzw[3],tc[0],tc[1],argb));}
        std::vector<uint32_t> order;const uint32_t required=topology==2?primitives*2:topology==3?primitives+1:topology==4?primitives*3:primitives+2;
        Check(required<=786432,"HUD index count");order.reserve(required);
        const Buffer* indices=NULL;if(indexed){const std::map<void*,Buffer>::const_iterator ii=c.buffers.find(input.ib);Check(ii!=c.buffers.end(),"HUD index stream missing CPU mirror");indices=&ii->second;
            Check(uint64_t(first)+required<=UINT32_MAX/2,"HUD index offset overflow");}
        for(uint32_t i=0;i<required;++i){uint32_t at=i;if(indexed){uint16_t value;memcpy(&value,indices->Read((first+i)*2,2),2);Check(value>=start,"HUD min vertex index");at=value-start;}
            Check(at<count,"HUD vertex index range");order.push_back(at);}
        const uint32_t vertex_base=static_cast<uint32_t>(frame.vertices.size());Command cmd={static_cast<uint32_t>(frame.indices.size()),0,2,texture,style};
        if(topology>=4){frame.vertices.insert(frame.vertices.end(),data.begin(),data.end());
            for(uint32_t i=0;i<primitives;++i){uint32_t a,b,d;
                if(topology==4){a=order[i*3];b=order[i*3+1];d=order[i*3+2];}
                else if(topology==5){a=order[i+(i&1)];b=order[i+1-(i&1)];d=order[i+2];}
                else {a=order[0];b=order[i+1];d=order[i+2];}
                frame.indices.push_back(vertex_base+a);frame.indices.push_back(vertex_base+b);frame.indices.push_back(vertex_base+d);}}
        else {for(uint32_t i=0;i<primitives;++i){const Vertex& a=data[order[topology==2?i*2:i]];const Vertex& b=data[order[topology==2?i*2+1:i+1]];
                const float dx=(b.p[0]/b.p[3]-a.p[0]/a.p[3])*frame.width*.5f;
                const float dy=(b.p[1]/b.p[3]-a.p[1]/a.p[3])*frame.height*.5f;const float length=sqrtf(dx*dx+dy*dy);if(length<1.e-6f)continue;
                frame.Reserve(4,6);Vertex line[4]={a,a,b,b};const float ox=-dy/length/frame.width,oy=dx/length/frame.height;
                for(unsigned j=0;j<4;++j){const float sign=(j&1)?-1.f:1.f;line[j].p[0]+=sign*ox*line[j].p[3];line[j].p[1]+=sign*oy*line[j].p[3];}
                const uint32_t at=static_cast<uint32_t>(frame.vertices.size());frame.vertices.insert(frame.vertices.end(),line,line+4);
                const uint32_t ix[]={0,1,2,2,1,3};for(unsigned j=0;j<6;++j)frame.indices.push_back(at+ix[j]);}}
        cmd.count=static_cast<uint32_t>(frame.indices.size())-cmd.first;if(cmd.count){frame.commands.push_back(cmd);++frame.ui;}
    });
}
