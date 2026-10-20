#include "../../xr_3da/KichR4GeometrySession.h"
#include "../../xr_3da/xrRender_R4/R4GeometryRules.h"
#include "../../xr_3da/xrRender_R4/R4FrameController.h"
#include <map>
#include <vector>
#include <string>
#include <stdexcept>
#include <stdio.h>
#include <cmath>
using namespace kich_r4;
using namespace kich_r4_scene;
namespace {
int checks=0;
const int32_t error=static_cast<int32_t>(UINT32_C(0x887a0005));
void Require(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
bool Near(float a,float b){return fabsf(a-b)<.0001f;}
void Transform(const Matrix& m,const float* v,float* out){
    for(unsigned col=0;col<4;++col){out[col]=0;for(unsigned row=0;row<4;++row)out[col]+=v[row]*m.m[row*4+col];}
}
KichR4PipelineDesc Pipeline(){KichR4PipelineDesc p={};p.struct_size=sizeof(p);p.vertex_stride=28;
    p.element_count=2;p.vertex_shader=1;p.pixel_shader=2;
    p.elements[0]={KICH_R4_POSITION,0,KICH_R4_FLOAT3,0};p.elements[1]={KICH_R4_COLOR,0,KICH_R4_FLOAT4,12};return p;}
void Rules(){
    const KichR4PipelineDesc good=Pipeline();Require(ValidPipeline(good),"position/color layout");
    for(unsigned n=0;n<16;++n){KichR4PipelineDesc p=good;switch(n){
        case 0:--p.struct_size;break;case 1:p.vertex_stride=0;break;case 2:p.vertex_stride=2049;break;
        case 3:p.element_count=0;break;case 4:p.element_count=9;break;case 5:p.flags=1;break;
        case 6:p.vertex_shader=0;break;case 7:p.pixel_shader=0;break;case 8:p.elements[0].semantic=0;break;
        case 9:p.elements[0].semantic=5;break;case 10:p.elements[0].semantic_index=16;break;
        case 11:p.elements[0].format=0;break;case 12:p.elements[1].byte_offset=UINT32_MAX;break;
        case 13:p.elements[1].byte_offset=13;break;case 14:p.elements[1].byte_offset=8;break;
        case 15:p.elements[1].semantic=KICH_R4_POSITION;break;}
        Require(!ValidPipeline(p),"malformed layout rejected");
    }
    for(uint32_t format=1;format<=4;++format){KichR4PipelineDesc p=good;p.element_count=1;
        p.elements[0].format=format;p.vertex_stride=VertexFormatBytes(format);Require(ValidPipeline(p),"supported vertex format");}
    const KichR4DrawIndexedDesc good_draw={sizeof(good_draw),1,2,3,4,36,3,KICH_R4_CULL_BACK,KICH_R4_DEPTH_ON};
    Require(ValidDraw(good_draw,156,4) && ValidDraw(good_draw,78,2),"exact end of IB, 16/32-bit");
    for(unsigned n=0;n<12;++n){KichR4DrawIndexedDesc d=good_draw;switch(n){
        case 0:--d.struct_size;break;case 1:d.pipeline=0;break;case 2:d.vertex_buffer=0;break;
        case 3:d.index_buffer=0;break;case 4:d.constant_buffer=0;break;case 5:d.index_count=0;break;
        case 6:d.index_count=35;break;case 7:d.start_index=4;break;case 8:d.start_index=UINT32_MAX;break;
        case 9:d.index_count=UINT32_MAX;break;case 10:d.cull_mode=3;break;case 11:d.depth_mode=2;break;}
        Require(!ValidDraw(d,156,4),"bad draw / overflowing index range rejected");
    }
    Require(!ValidDraw(good_draw,156,0) && !ValidDraw(good_draw,156,3),"unsupported index stride");
}
void Scene(){
    const Mesh mesh=BuildMesh();
    for(unsigned i=0;i<39;++i)Require(mesh.indices[i]<27,"every index addresses a vertex");
    for(unsigned f=0;f<6;++f){const float* a=mesh.vertices[3+f*4].position;
        const float* b=mesh.vertices[4+f*4].position;const float* c=mesh.vertices[5+f*4].position;
        const Vec3 ab={b[0]-a[0],b[1]-a[1],b[2]-a[2]},ac={c[0]-a[0],c[1]-a[1],c[2]-a[2]},point={a[0],a[1],a[2]};
        Require(Dot(Cross(ab,ac),point)>0,"all cube faces have outward normals");
    }
    const Matrix projection=Perspective(16.f/9.f);
    float near_point[]={0,0,.1f,1},far_point[]={0,0,100,1},clip[4];
    Transform(projection,near_point,clip);Require(Near(clip[2]/clip[3],0),"D3D near plane maps to zero");
    Transform(projection,far_point,clip);Require(Near(clip[2]/clip[3],1),"D3D far plane maps to one");
    Require(Near(projection.m[5]/projection.m[0],16.f/9.f),"projection uses viewport aspect");
    const float origin[]={0,0,0,1};
    Transform(Multiply(Scale(2),Translation(3,4,5)),origin,clip);
    Require(Near(clip[0],3) && Near(clip[1],4) && Near(clip[2],5),"row-vector scale before world translation");
    Controls c=Defaults();
    for(unsigned size=0;size<2;++size){const unsigned w=size?540:960,h=size?960:540;
        const Constants a=ObjectConstants(c,w,h,0);
        Require(Near(a.mvp.m[0]*w,a.mvp.m[5]*h),"triangle proportions survive portrait and landscape resize");
        const float p0[]={-.75f,-.65f,.5f,1},p1[]={0,.7f,.5f,1},p2[]={.75f,-.65f,.5f,1};
        float v0[4],v1[4],v2[4];Transform(a.mvp,p0,v0);Transform(a.mvp,p1,v1);Transform(a.mvp,p2,v2);
        Require((v1[0]-v0[0])*(v2[1]-v0[1])-(v1[1]-v0[1])*(v2[0]-v0[0])<0,
            "triangle becomes clockwise after viewport Y flip");
    }
    Apply(c,Cubes);Apply(c,Pause);Apply(c,Depth);Require(c.mode==2 && c.paused && !c.depth,"scene switches");
    for(unsigned i=0;i<3;++i)Apply(c,Cull);
    Require(c.cull==KICH_R4_CULL_BACK,"culling cycles back/front/none");
    for(unsigned i=0;i<100;++i){Apply(c,Up);Apply(c,Closer);Apply(c,Right);}
    Require(Near(c.pitch,1.1f) && Near(c.distance,3) && fabsf(c.yaw)<6.284f,"upper pitch / near zoom clamp");
    for(unsigned i=0;i<100;++i){Apply(c,Down);Apply(c,Farther);}
    Require(Near(c.pitch,-1.1f) && Near(c.distance,12),"lower pitch / far zoom clamp");Apply(c,CameraReset);
    Require(Near(c.distance,6.2f) && Near(c.yaw,-.25f),"camera reset");
    for(unsigned pose=0;pose<120;++pose){c.angle=pose*.1f;
        for(unsigned object=0;object<2;++object){const Constants a=ObjectConstants(c,960,540,object);
            for(unsigned i=0;i<16;++i)Require(std::isfinite(a.mvp.m[i]),"finite animated camera/world/projection matrix");
            for(unsigned i=3;i<27;++i){const Vertex& v=mesh.vertices[i];const float p[]={v.position[0],v.position[1],v.position[2],1};
                Transform(a.mvp,p,clip);Require(clip[3]>0 && clip[2]>=0 && clip[2]<=clip[3],"default cubes stay between clip planes");}
        }
    }
}
struct Driver:FrameDriver {
    bool chain,view;Driver():chain(false),view(false){}
    int32_t Create(void*,uint32_t,uint32_t){chain=true;return 0;}
    void DropView(){view=false;}int32_t Resize(uint32_t,uint32_t){return 0;}
    int32_t MakeView(){view=true;return 0;}void Bind(uint32_t,uint32_t){}void Clear(const float*){}
    int32_t Present(uint32_t,bool){return 0;}int32_t RemovedReason(){return 0;}
    void Close(){chain=view=false;}
};
struct Object {uint32_t kind;std::vector<unsigned char> data;};
struct Fixture {
    Driver driver;FrameController frame;bool live;int calls,fail;int32_t failure;
    uint64_t next;uint32_t created,released,updates;std::map<KichR4Handle,Object> objects;
    std::vector<std::string> trace;std::vector<KichR4DrawIndexedDesc> draws;std::vector<Constants> constants;
    Fixture():frame(driver),live(false),calls(0),fail(0),failure(error),next(10),created(0),released(0),updates(0){}
    int32_t Call(const char* name){trace.push_back(name);return ++calls==fail?failure:0;}
    KichR4Handle Add(uint32_t kind,const void* data=NULL,uint32_t bytes=0){Object o={kind,{}};
        if(data){const unsigned char* begin=static_cast<const unsigned char*>(data);o.data.assign(begin,begin+bytes);}
        const KichR4Handle h=next++;objects[h]=o;++created;return h;}
};
Fixture* f;
template<class... Args>int32_t Unused(Args...){throw std::runtime_error("unexpected resource operation");}
int32_t CreateDevice(KichR4Handle* out){*out=0;const int32_t hr=f->Call("device");if(hr)return hr;f->live=true;*out=1;return 0;}
int32_t CreateTarget(KichR4Handle,const KichR4FrameTargetDesc* d){const int32_t hr=f->Call("target");return hr?hr:f->frame.Create(*d);}
int32_t Compile(KichR4Handle,const KichR4ShaderDesc* d,const char* text,uint32_t bytes,KichR4Handle* out,char*,uint32_t){
    *out=0;const int32_t hr=f->Call(d->kind==KICH_R4_VERTEX_SHADER?"vs":"ps");if(hr)return hr;
    Require(d->struct_size==sizeof(*d) && !strcmp(d->entry,"main") && bytes==strlen(text),"complete shader descriptor/source");
    Require(strstr(text,d->kind==KICH_R4_VERTEX_SHADER?"row_major":"SV_Target")!=NULL,"actual HLSL scene sources");
    *out=f->Add(d->kind);return 0;
}
int32_t Buffer(KichR4Handle,const KichR4BufferDesc* d,const void* data,uint32_t bytes,KichR4Handle* out){
    *out=0;const int32_t hr=f->Call(d->kind==KICH_R4_VERTEX_BUFFER?"vb":d->kind==KICH_R4_INDEX_BUFFER?"ib":"cb");if(hr)return hr;
    Require(d->byte_size==bytes && data,"complete initialized geometry buffer");*out=f->Add(d->kind,data,bytes);return 0;
}
int32_t CreatePipeline(KichR4Handle,const KichR4PipelineDesc* p,KichR4Handle* out){
    *out=0;const int32_t hr=f->Call("pipeline");if(hr)return hr;
    Require(ValidPipeline(*p) && p->vertex_stride==sizeof(Vertex),"scene input layout validates");
    const KichR4Handle vs=p->vertex_shader,ps=p->pixel_shader;
    Require(f->objects.at(vs).kind==KICH_R4_VERTEX_SHADER && f->objects.at(ps).kind==KICH_R4_PIXEL_SHADER,"pipeline uses owned shaders");
    *out=f->Add(KICH_R4_GRAPHICS_PIPELINE);return 0;
}
int32_t Update(KichR4Handle,KichR4Handle h,const void* data,uint32_t bytes){
    const int32_t hr=f->Call("update");if(hr)return hr;Object& o=f->objects.at(h);
    Require(o.kind==KICH_R4_CONSTANT_BUFFER && bytes==sizeof(Constants) && bytes==o.data.size(),"full per-object cbuffer update");
    memcpy(o.data.data(),data,bytes);++f->updates;return 0;
}
int32_t DrawIndexed(KichR4Handle,const KichR4DrawIndexedDesc* d){
    const int32_t hr=f->Call("draw");if(hr)return hr;Require(f->frame.CanDraw(),"draw occurs between clear and present");
    /* Packed ABI fields may have 4-byte alignment. Copy before passing a
     * handle to STL functions that bind a naturally aligned uint64 reference. */
    const KichR4Handle ih=d->index_buffer,vh=d->vertex_buffer,ch=d->constant_buffer;
    const Object& ib=f->objects.at(ih);Require(ValidDraw(*d,static_cast<uint32_t>(ib.data.size()),4),"scene draw fits IB");
    Require(f->objects.at(vh).data.size()==sizeof(Mesh::vertices),"complete shared triangle/cube VB");
    Constants constants;memcpy(&constants,f->objects.at(ch).data.data(),sizeof(constants));
    f->constants.push_back(constants);f->draws.push_back(*d);return 0;
}
int32_t Begin(KichR4Handle){const int32_t hr=f->Call("begin");return hr?hr:f->frame.Begin();}
int32_t Clear(KichR4Handle,const KichR4FrameColor* c){const int32_t hr=f->Call("clear");return hr?hr:f->frame.Clear(*c);}
int32_t Present(KichR4Handle,uint32_t interval){const int32_t hr=f->Call("present");return hr?hr:f->frame.Present(interval);}
int32_t Destroy(KichR4Handle,KichR4GeometryReport* out,uint32_t bytes){
    const int32_t hr=f->Call("destroy");if(hr)return hr;f->frame.Close();f->released+=static_cast<uint32_t>(f->objects.size());f->objects.clear();f->live=false;
    if(out){Require(bytes==sizeof(*out),"final report capacity");memset(out,0,sizeof(*out));out->struct_size=sizeof(*out);out->frame=f->frame.Stats();
        out->resources.struct_size=sizeof(out->resources);out->resources.created=f->created;out->resources.released=f->released;}
    return 0;
}
KichR4GeometrySession Geometry(){KichR4GeometrySession g={};
    g.resources={sizeof(g.resources),KICH_R4_RESOURCE_ABI,sizeof(void*),0,Unused,Unused,Unused,Buffer,Compile,Unused,Unused,Unused,Unused,Unused};
    g.api={sizeof(g.api),KICH_R4_GEOMETRY_ABI,sizeof(void*),0,CreatePipeline,Update,DrawIndexed,Unused,Destroy};return g;}
KichR4FrameSession Frame(){KichR4FrameSession s={};s.api={sizeof(s.api),KICH_R4_FRAME_ABI,sizeof(void*),0,
    CreateDevice,Unused,CreateTarget,Unused,Unused,Begin,Clear,Present,Unused};return s;}
int32_t Render(KichR4FrameSession& s,KichR4GeometrySession& g,const Controls& c){
    const KichR4FrameColor color={sizeof(color),{0,0,0,1}};int32_t hr=s.BeginClear(color);
    if(hr==0)hr=g.Draw(s,c,960,540);
    return hr==0?s.Present():hr;
}
void Session(int fail,int32_t failure){Fixture fixture;f=&fixture;f->fail=fail;f->failure=failure;
    KichR4FrameSession s=Frame();KichR4GeometrySession g=Geometry();Controls c=Defaults();
    Require(KichR4GeometrySession::ValidAPI(g.api) && KichR4GeometrySession::ValidResources(g.resources),"valid API tables");
    if(s.Start(reinterpret_cast<void*>(1),960,540)==0 && g.Start(s)==0 && Render(s,g,c)==0){Apply(c,Cubes);Render(s,g,c);}
    KichR4GeometryReport report={};
    if(g.Stop(s,&report)<0)Require(g.Stop(s,NULL)==0,"retry failed geometry cleanup");
    Require(!s.device && !f->live && f->objects.empty() && !f->driver.chain && !f->driver.view,"all partial resources / target / device released");
    Require(f->created==f->released,"all six resource kinds balanced");
    if(fail)Require(s.failure==(failure<0?failure:KICH_R4_E_STATE),"first failure retained after cleanup");
    else{
        const char* expected[]={"device","target","vs","ps","vb","ib","cb","pipeline","begin","clear","update","draw","present",
            "begin","clear","update","draw","update","draw","present","destroy"};
        Require(f->trace.size()==21 && !s.failure && s.rendered==2 && g.triangle_frames==1 && g.cube_frames==1,"both scene modes present");
        for(unsigned i=0;i<21;++i)Require(f->trace[i]==expected[i],"actual host create/draw/destroy order");
        Require(f->created==6 && f->updates==3 && f->draws.size()==3,"one pipeline, two shaders, three buffers / per-object updates");
        Require(f->draws[0].index_count==3 && f->draws[0].start_index==0,"triangle draw uses first IB slice");
        Require(f->draws[1].index_count==36 && f->draws[2].start_index==3,"two cubes reuse cube IB slice");
        Require(f->constants[1].tint[0]>f->constants[2].tint[0] && memcmp(&f->constants[1].mvp,&f->constants[2].mvp,sizeof(Matrix)),"near cube submitted before distinct far cube");
    }
    const size_t calls=f->trace.size();Require(g.Stop(s,NULL)==0 && f->trace.size()==calls,"idempotent stopped session");
}
void Tables(){const KichR4GeometrySession good=Geometry();
    for(unsigned n=0;n<9;++n){KichR4GeometryAPI a=good.api;switch(n){
        case 0:++a.struct_size;break;case 1:++a.abi_version;break;case 2:++a.pointer_size;break;case 3:a.flags=1;break;
        case 4:a.create_pipeline=NULL;break;case 5:a.update_buffer=NULL;break;case 6:a.draw_indexed=NULL;break;
        case 7:a.get_stats=NULL;break;case 8:a.destroy_device=NULL;break;}
        Require(!KichR4GeometrySession::ValidAPI(a),"bad geometry table rejected");
    }
    for(unsigned n=0;n<14;++n){KichR4ResourceAPI a=good.resources;switch(n){
        case 0:++a.struct_size;break;case 1:++a.abi_version;break;case 2:++a.pointer_size;break;case 3:a.flags=1;break;
        case 4:a.create_device=NULL;break;case 5:a.destroy_device=NULL;break;case 6:a.create_texture=NULL;break;
        case 7:a.create_buffer=NULL;break;case 8:a.compile_shader=NULL;break;case 9:a.get_info=NULL;break;
        case 10:a.get_binding=NULL;break;case 11:a.get_variable=NULL;break;case 12:a.release_resource=NULL;break;case 13:a.get_stats=NULL;break;}
        Require(!KichR4GeometrySession::ValidResources(a),"bad B2 table rejected before geometry creation");
    }
}
}
int main(){try{Rules();Scene();Tables();Session(0,error);for(int i=1;i<=21;++i)Session(i,error);
    const int positive_failures[]={3,4,5,6,7,8,11,12,16,17,18,19,21};
    for(unsigned i=0;i<sizeof(positive_failures)/sizeof(positive_failures[0]);++i)Session(positive_failures[i],7);
    printf("B4.1 production rules/scene/session: %d checks; 21 failing call positions passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
