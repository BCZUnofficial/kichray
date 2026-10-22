#ifndef KICH_R4_CAPTURE_DATA_H
#define KICH_R4_CAPTURE_DATA_H
#include "KichR4LevelData.h"
#include "KichR4Stream.h"
#include "KichR4CaptureInput.h"
#include "KichR4LightingMath.h"
#include <memory>
#include <set>
#include <map>
#include <stdio.h>
namespace kich_r4_capture {
using kich_r4_level::Span;using kich_r4_level::Reader;using kich_r4_level::Check;
using kich_r4_level::Chunk;using kich_r4_level::Finite;using kich_r4_scene::Matrix;
struct Vertex {float p[4],uv[2];uint32_t color;float normal[4],world[3];};
static_assert(sizeof(Vertex)==56,"B4.4 native vertex stride");
struct SourceVertex {float p[3],uv[2],weight;uint16_t bone[2];uint32_t normal;};
static_assert(sizeof(SourceVertex)==32,"B4.4 compact CPU source vertex");
inline uint32_t PackNormal(float x,float y,float z){
    if(!Finite(x) || !Finite(y) || !Finite(z))return 0;
    const kich_r4_scene::Vec3 n=kich_r4_scene::Normalize(kich_r4_scene::Vec3{x,y,z});
    if(kich_r4_scene::Dot(n,n)<.1f)return 0;
    const float values[]={n.x,n.y,n.z};uint32_t bits=0;
    for(unsigned i=0;i<3;++i)bits|=static_cast<uint32_t>(std::max(0.f,std::min(1023.f,(values[i]+1)*511.5f+.5f)))<<(i*10);
    return bits;
}
inline kich_r4_scene::Vec3 UnpackNormal(uint32_t bits){if(!bits)return kich_r4_scene::Vec3{0,0,0};
    return kich_r4_scene::Normalize(kich_r4_scene::Vec3{(bits&1023)*(2.f/1023)-1,((bits>>10)&1023)*(2.f/1023)-1,((bits>>20)&1023)*(2.f/1023)-1});}
struct Mesh {
    std::vector<SourceVertex> vertices;std::vector<uint16_t> indices;std::string texture;
    uint32_t bones,ignored_weights,outside_weights,diagnostic_modes;bool skinned,two_weights;
    std::string first_weight_detail;
    Mesh():bones(0),ignored_weights(0),outside_weights(0),diagnostic_modes(0),skinned(false),two_weights(false){}
    uint64_t Bytes()const{return uint64_t(vertices.size())*sizeof(SourceVertex)+uint64_t(indices.size())*2;}
};
inline std::string WeightDetail(uint32_t vertex,uint32_t b0,uint32_t b1,uint32_t bits,const char* why){
    char text[224];float w;memcpy(&w,&bits,4);
    if((bits&0x7f800000u)==0x7f800000u)snprintf(text,sizeof(text),"%s: vertex=%u bones=%u/%u weight=nonfinite bits=0x%08X",why,vertex,b0,b1,bits);
    else snprintf(text,sizeof(text),"%s: vertex=%u bones=%u/%u weight=%.9g bits=0x%08X",why,vertex,b0,b1,static_cast<double>(w),bits);
    return text;
}
inline float SkinWeight(float w,uint32_t mode){
    Check(Finite(w),"nonfinite active 2W skin weight");
    if(mode==KICH_R4_SKIN_SOFTWARE)return w;
    Check(mode==KICH_R4_SKIN_HARDWARE,"2W skin mode handshake missing; rebuild R1/R2");
    /* Match vertHW_2W::set, but reject values beyond its safe signed iFloor
     * range rather than invoking an overflowing float-to-int conversion. */
    Check(w>=-8388607.f && w<=8388607.f,"2W hardware weight exceeds safe quantization range");
    if(w<=0){return 0;}if(w>=1){return 1;}
    return floorf(w*255.f+.5f)/255.f;
}
/* Decode original OGF data, before the DX9 renderer quantizes it. Containers
 * belong to the static level and are already drawn by B4.2. */
inline std::shared_ptr<Mesh> Decode(Span source) {
    Reader header(Chunk(source,1));Check(header.Left()>=44,"model OGF header size");const unsigned char* first=header.Take(2);
    Check(first[0]==4,"model OGF version");const unsigned type=first[1];
    if(type!=0 && type!=2 && type!=4 && type!=5)return std::shared_ptr<Mesh>();
    if(Chunk(source,21,false).p || Chunk(source,7,false).p)return std::shared_ptr<Mesh>();
    std::shared_ptr<Mesh> mesh(new Mesh());mesh->skinned=type==4 || type==5;
    Reader texture(Chunk(source,2));mesh->texture=kich_r4_level::Lower(texture.Z());
    const size_t comma=mesh->texture.find(',');if(comma!=std::string::npos)mesh->texture.resize(comma);
    if(mesh->texture.size()>4 && mesh->texture.substr(mesh->texture.size()-4)==".dds")mesh->texture.resize(mesh->texture.size()-4);
    Check(mesh->texture.empty() || (kich_r4_level::SafeName(mesh->texture) && mesh->texture[0]!='$'),"model texture name");
    Reader vertices(Chunk(source,3));const uint32_t format=vertices.U32(),count=vertices.U32();
    Check(count && count<=65536,"model vertex limit");uint32_t stride=0,uv=0,pos=0;
    if(mesh->skinned){Check(format==0x12071980u || format==0x240e3300u,"model bone format");
        stride=format==0x12071980u?60:64;pos=stride==64?4:0;uv=stride==64?56:48;mesh->two_weights=stride==64;
    }else {
        /* SoC inline FVF: XYZ, optional normal/point size/color, float UVs. */
        Check((format&0x400eu)==2 && !(format&0x8000u),"model position FVF");stride=12;
        if(format&0x10u){stride+=12;}if(format&0x20u){stride+=4;}
        if(format&0x40u){stride+=4;}if(format&0x80u){stride+=4;}
        uv=stride;const uint32_t sets=(format>>8)&15;Check(sets && sets<=8,"model UV sets");
        for(uint32_t s=0;s<sets;++s){const uint32_t dims=(format>>(16+s*2))&3;
            Check(!s?dims==0:true,"model base UV format");stride+=dims==0?8:dims==1?12:dims==2?16:4;}
    }
    Check(count<=vertices.Left()/stride,"truncated model vertices");mesh->vertices.resize(count);
    for(uint32_t i=0;i<count;++i){const unsigned char* p=vertices.Take(stride);SourceVertex& v=mesh->vertices[i];v={};
        Reader xyz(Span(p+pos,12)),tc(Span(p+uv,8));for(unsigned a=0;a<3;++a)v.p[a]=xyz.Float();
        v.uv[0]=tc.Float();v.uv[1]=tc.Float();
        if(mesh->skinned || (format&0x10u)){Reader normal(Span(p+(mesh->skinned?pos+12:12),12));
            const float nx=normal.Float(),ny=normal.Float(),nz=normal.Float();v.normal=PackNormal(nx,ny,nz);}
        if(mesh->skinned){if(stride==60){Reader b(Span(p+56,4));const uint32_t id=b.U32();Check(id<256,"1W model bone limit");v.bone[0]=v.bone[1]=static_cast<uint16_t>(id);}
            else {Reader b(Span(p,4)),w(Span(p+52,4));v.bone[0]=b.U16();v.bone[1]=b.U16();const uint32_t bits=w.U32();memcpy(&v.weight,&bits,4);
                if(v.bone[0]>=256 || v.bone[1]>=256)Check(false,WeightDetail(i,v.bone[0],v.bone[1],bits,"2W bone index outside 0..255").c_str());
                const bool ieee_finite=(bits&0x7f800000u)!=0x7f800000u;
                if(v.bone[0]==v.bone[1]){
                    /* xrSkin2W_x86 ignores w entirely for an identical pair.
                     * In hardware both matrix results are equal as well. */
                    if(!ieee_finite || v.weight<0 || v.weight>1){++mesh->ignored_weights;
                        if(mesh->first_weight_detail.empty())mesh->first_weight_detail=WeightDetail(i,v.bone[0],v.bone[1],bits,"unused equal-bone weight");}
                    v.weight=0;
                }else {
                    if(!ieee_finite || !Finite(v.weight))Check(false,WeightDetail(i,v.bone[0],v.bone[1],bits,"invalid active 2W weight").c_str());
                    if(v.weight<0 || v.weight>1){++mesh->outside_weights;
                        if(mesh->first_weight_detail.empty())mesh->first_weight_detail=WeightDetail(i,v.bone[0],v.bone[1],bits,"active weight outside 0..1");}
                }}
            mesh->bones=std::max(mesh->bones,uint32_t(std::max(v.bone[0],v.bone[1]))+1);}
    }
    Reader indices(Chunk(source,4));const uint32_t n=indices.U32();Check(n && n%3==0 && n<=3u*1024u*1024u && n<=indices.Left()/2,"model index limit");
    mesh->indices.resize(n);for(uint32_t i=0;i<n;++i){mesh->indices[i]=indices.U16();Check(mesh->indices[i]<count,"model index range");}
    Check(mesh->Bytes()<=8u*1024u*1024u,"single model CPU budget");return mesh;
}
inline void Transform(const float* p,const Matrix& m,float* out) {
    for(unsigned c=0;c<4;++c)out[c]=p[0]*m.m[c]+p[1]*m.m[4+c]+p[2]*m.m[8+c]+m.m[12+c];
}
inline uint32_t RGBA(uint32_t argb){return (argb&0xff00ff00u)|((argb&0xffu)<<16)|((argb>>16)&0xffu);}
struct Command {uint32_t first,count,layer;std::string texture;Style style;uint32_t effect;std::string secondary,resource,shader,skin;
    bool console;
    Command(uint32_t f,uint32_t n,uint32_t l,const std::string& t,const Style& s):first(f),count(n),layer(l),texture(t),style(s),effect(0),console(false){}
};
struct Batch {
    uint32_t width,height,engine_frame,models,hands,ui;bool renderer_seen,ui_seen;unsigned ui_scope;
    std::vector<Vertex> vertices;std::vector<uint32_t> indices;std::vector<Command> commands;std::string error;
    std::set<std::string> model_keys;
    Batch():width(0),height(0),engine_frame(0),models(0),hands(0),ui(0),renderer_seen(false),ui_seen(false),ui_scope(0){}
    void Begin(uint32_t w,uint32_t h,uint32_t f){width=w;height=h;engine_frame=f;models=hands=ui=0;renderer_seen=ui_seen=false;ui_scope=0;
        vertices.clear();indices.clear();commands.clear();error.clear();model_keys.clear();}
    bool Ready()const{return width && height && renderer_seen && ui_seen && !ui_scope && error.empty();}
    void Fail(const char* why){if(error.empty())error=why;}
    void Reserve(uint32_t v,uint32_t i,const char* source="<unspecified>",uint32_t layer=UINT32_MAX){
        const uint64_t used=uint64_t(vertices.size())*sizeof(Vertex)+uint64_t(indices.size())*4;
        const uint64_t requested=uint64_t(v)*sizeof(Vertex)+uint64_t(i)*4;
        const uint64_t limit=32u*1024u*1024u;
        const char* why=commands.size()>=16384?"frame draw limit":used+requested>limit?"frame CPU geometry budget":NULL;
        if(!why)return;
        char detail[768];snprintf(detail,sizeof(detail),
            "%s; frame=%u used_bytes=%llu request_bytes=%llu total_bytes=%llu limit_bytes=%llu "
            "vertices=%llu indices=%llu request_vertices=%u request_indices=%u commands=%llu models=%u hands=%u UI=%u layer=%u source=%.180s",
            why,engine_frame,static_cast<unsigned long long>(used),static_cast<unsigned long long>(requested),
            static_cast<unsigned long long>(used+requested),static_cast<unsigned long long>(limit),
            static_cast<unsigned long long>(vertices.size()),static_cast<unsigned long long>(indices.size()),v,i,
            static_cast<unsigned long long>(commands.size()),models,hands,ui,layer,source?source:"<unnamed>");
        throw std::runtime_error(detail);
    }
    void Model(const void* identity,const Mesh& mesh,const Matrix& world,const Matrix& vp,const Matrix* bones,uint32_t bone_count,
        uint32_t count,uint32_t first,uint32_t tris,const Style& style,uint32_t skin_mode=KICH_R4_SKIN_SOFTWARE,const char* name="<unnamed model>",bool diagnostic=false) {
        Check(count && count<=mesh.vertices.size() && tris && uint64_t(first)+uint64_t(tris)*3<=mesh.indices.size(),"model draw range");
        Check(!mesh.skinned || (bones && mesh.bones<=bone_count),"model bone pose range");
        Check(!mesh.two_weights || skin_mode==KICH_R4_SKIN_SOFTWARE || skin_mode==KICH_R4_SKIN_HARDWARE,
            "2W skin mode handshake missing; rebuild R1/R2");
        const uint32_t layer=style.max_z<.1f?1:0;
        std::string key(reinterpret_cast<const char*>(&identity),sizeof(identity));
        key.append(reinterpret_cast<const char*>(&world),sizeof(world));key.append(reinterpret_cast<const char*>(&vp),sizeof(vp));
        key.append(reinterpret_cast<const char*>(&first),sizeof(first));key.append(reinterpret_cast<const char*>(&tris),sizeof(tris));
        key.append(reinterpret_cast<const char*>(&count),sizeof(count));
        key.append(reinterpret_cast<const char*>(&skin_mode),sizeof(skin_mode));
        key.append(reinterpret_cast<const char*>(&layer),sizeof(layer));
        if(!model_keys.insert(key).second)return;
        Reserve(count,tris*3,name,layer);const uint32_t base=static_cast<uint32_t>(vertices.size());const Matrix mvp=kich_r4_scene::Multiply(world,vp);
        const Matrix normal_world=kich_r4_lighting::NormalMatrix(world);
        Command cmd={static_cast<uint32_t>(indices.size()),tris*3,layer,mesh.texture,style};
        if(diagnostic){cmd.resource=name;cmd.skin=mesh.skinned?"CPU_CAPTURE_SKIN":"CPU_CAPTURE_RIGID";}
        for(uint32_t i=0;i<count;++i){const SourceVertex& src=mesh.vertices[i];float p[4]={src.p[0],src.p[1],src.p[2],1};
            kich_r4_scene::Vec3 n=UnpackNormal(src.normal);
            if(mesh.skinned){float a[4];Transform(src.p,bones[src.bone[0]],a);
                const kich_r4_scene::Vec3 na=kich_r4_lighting::Direction(n,bones[src.bone[0]]);
                if(src.bone[0]==src.bone[1]){for(unsigned c=0;c<3;++c)p[c]=a[c];}
                else{float b[4];Transform(src.p,bones[src.bone[1]],b);const float weight=SkinWeight(src.weight,skin_mode);
                    const float inverse=1.f-weight;for(unsigned c=0;c<3;++c)p[c]=a[c]*inverse+b[c]*weight;}
                if(src.bone[0]==src.bone[1])n=na;
                else {const kich_r4_scene::Vec3 nb=kich_r4_lighting::Direction(n,bones[src.bone[1]]);const float w=SkinWeight(src.weight,skin_mode);
                    n=kich_r4_scene::Vec3{na.x*(1-w)+nb.x*w,na.y*(1-w)+nb.y*w,na.z*(1-w)+nb.z*w};}}
            Vertex v={};Transform(p,mvp,v.p);for(unsigned c=0;c<4;++c)Check(Finite(v.p[c]),"model clip coordinates");
            float wp[4];Transform(p,world,wp);for(unsigned c=0;c<3;++c){Check(Finite(wp[c]),"model world coordinates");v.world[c]=wp[c];}
            n=kich_r4_scene::Normalize(kich_r4_lighting::Direction(n,normal_world));
            v.normal[0]=n.x;v.normal[1]=n.y;v.normal[2]=n.z;v.normal[3]=1;
            for(unsigned c=0;c<3;++c)Check(Finite(v.normal[c]),"model world normal");
            v.uv[0]=src.uv[0];v.uv[1]=src.uv[1];v.color=0xffffffffu;vertices.push_back(v);}
        for(uint32_t i=0;i<tris*3;++i){const uint32_t at=mesh.indices[first+i];Check(at<count,"progressive model vertex range");indices.push_back(base+at);}
        commands.push_back(cmd);if(layer)++hands;else ++models;
    }
    Vertex Screen(float x,float y,float z,float rhw,float u,float v,uint32_t argb)const{
        Check(Finite(x) && Finite(y) && Finite(z) && Finite(rhw) && rhw>0 && Finite(u) && Finite(v),"HUD vertex coordinates");
        const float w=1.f/rhw;Check(Finite(w),"HUD reciprocal RHW");Vertex out={};out.p[0]=((x+.5f)*2.f/width-1.f)*w;
        out.p[1]=(1.f-(y+.5f)*2.f/height)*w;out.p[2]=z*w;out.p[3]=w;
        for(unsigned c=0;c<4;++c){Check(Finite(out.p[c]),"HUD clip coordinates");}out.uv[0]=u;out.uv[1]=v;out.color=RGBA(argb);return out;
    }
};
struct Entry {std::shared_ptr<Mesh> mesh;std::string error,name;};
inline std::string ModelError(const Entry& entry,const char* why){return "model="+entry.name+"; "+why;}
struct Segment {uint32_t offset;std::vector<unsigned char> bytes;};
struct Buffer {
    std::vector<Segment> segments;const void* pending;uint32_t offset,capacity,discard;bool fixed;
    Buffer():pending(NULL),offset(0),capacity(0),discard(0),fixed(false){}
    void Put(const void* data,uint32_t at,uint32_t count){Check(data && count && uint64_t(at)+count<=8u*1024u*1024u,"HUD buffer range");
        Segment segment;segment.offset=at;const unsigned char* p=static_cast<const unsigned char*>(data);segment.bytes.assign(p,p+count);segments.push_back(segment);}
    const unsigned char* Read(uint32_t at,uint32_t count)const {
        for(size_t i=segments.size();i;--i){const Segment& s=segments[i-1];if(at>=s.offset && uint64_t(at)+count<=uint64_t(s.offset)+s.bytes.size())return s.bytes.data()+at-s.offset;}
        throw std::runtime_error("HUD draw has no matching CPU buffer data");
    }
};
struct State {
    Batch frame;std::map<const void*,Entry> models;std::map<void*,Buffer> buffers;
    std::map<const void*,KichR4LocalLight> lights;std::set<const void*> unshadowed_lights;
    uint64_t stream_bytes;
    uint32_t diagnostics;void (*log)(const char*);
    State():stream_bytes(0),diagnostics(0),log(NULL){}
    void Light(const void* id,const KichR4LocalLight& light,bool active){
        if(!id)return;
        if(!active || !kich_r4_lighting::ValidLight(light)){lights.erase(id);return;}
        if(lights.size()<4096 || lights.find(id)!=lights.end())lights[id]=light;
    }
    void SelectLights(const float* camera,KichR4LightingFrame& out)const {
        struct Choice {float score;KichR4LocalLight light;bool shadow;};std::vector<Choice> choices;out.count=0;out.shadow_mask=0;
        for(std::map<const void*,KichR4LocalLight>::const_iterator it=lights.begin();it!=lights.end();++it){
            const KichR4LocalLight& l=it->second;float distance=0;
            for(unsigned a=0;a<3;++a){const float d=l.position[a]-camera[a];distance+=d*d;}
            const float brightness=std::max(l.color[0],std::max(l.color[1],l.color[2]));
            if(distance>(150+l.range)*(150+l.range) || brightness<=0)continue;
            Choice c={brightness*l.range*l.range/(distance+1)+(l.type==2 && distance<4?1.e10f:0),l,unshadowed_lights.count(it->first)==0};choices.push_back(c);
        }
        std::stable_sort(choices.begin(),choices.end(),[](const Choice& a,const Choice& b){return a.score>b.score;});
        out.count=static_cast<uint32_t>(std::min(size_t(8),choices.size()));for(unsigned i=0;i<out.count;++i){out.lights[i]=choices[i].light;if(choices[i].shadow)out.shadow_mask|=1u<<i;}
    }
    void Diagnostic(const std::string& message){if(log && diagnostics<64){++diagnostics;log(("[R4 bridge B4.3b.1] "+message).c_str());}}
    void Begin(uint32_t w,uint32_t h,uint32_t f){frame.Begin(w,h,f);stream_bytes=0;
        for(std::map<void*,Buffer>::iterator i=buffers.begin();i!=buffers.end();++i)if(!i->second.fixed){i->second.segments.clear();i->second.pending=NULL;}}
    uint64_t ModelBytes()const {uint64_t result=0;std::set<const Mesh*> counted;
        for(std::map<const void*,Entry>::const_iterator i=models.begin();i!=models.end();++i){if(i->second.mesh && counted.insert(i->second.mesh.get()).second)result+=i->second.mesh->Bytes();}return result;}
    void Load(void* visual,Span source,const char* name=NULL){Entry& entry=models[visual];entry=Entry();entry.name=name && *name?std::string(name).substr(0,240):"<unnamed>";
        try{entry.mesh=Decode(source);if(entry.mesh)Check(ModelBytes()<=96u*1024u*1024u,"model registry CPU budget");}
        catch(const std::exception& e){entry.mesh.reset();entry.error=ModelError(entry,e.what());Diagnostic("model rejected: "+entry.error);return;}
        if(entry.mesh && (entry.mesh->ignored_weights || entry.mesh->outside_weights)){char counts[128];snprintf(counts,sizeof(counts),"; ignored_unused=%u active_outside_01=%u; ",entry.mesh->ignored_weights,entry.mesh->outside_weights);
            Diagnostic("2W compatibility: model="+entry.name+counts+entry.mesh->first_weight_detail);}}
    void Copy(void* visual,const void* source){const std::map<const void*,Entry>::const_iterator i=models.find(source);
        if(i!=models.end())models[visual]=i->second;else{Entry e;e.error="model copy without source registration";models[visual]=e;}}
    void Lock(void* buffer,const void* data,uint32_t at,uint32_t capacity,uint32_t discard){if(!frame.ui_scope)return;
        Buffer& b=buffers[buffer];Check(!b.pending && !b.fixed,"HUD nested buffer lock");if(b.discard!=discard)b.segments.clear();
        b.pending=data;b.offset=at;b.capacity=capacity;b.discard=discard;}
    void Unlock(void* buffer,uint32_t bytes){std::map<void*,Buffer>::iterator i=buffers.find(buffer);if(i==buffers.end() || !i->second.pending)return;
        Buffer& b=i->second;Check(bytes<=b.capacity,"HUD unlock range");const void* data=b.pending;b.pending=NULL;
        if(bytes){Check(stream_bytes+bytes<=8u*1024u*1024u,"HUD stream CPU budget");b.Put(data,b.offset,bytes);stream_bytes+=bytes;}}
};
inline const char* VertexShader(){return "struct I{float4 p:POSITION;float2 uv:TEXCOORD0;float4 c:COLOR0;};"
    "struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;float4 c:COLOR0;};O main(I v){O o;o.p=v.p;o.uv=v.uv;o.c=v.c;return o;}";}
inline const char* PixelShader(){return "cbuffer Draw:register(b0){float4 alpha;};Texture2D image:register(t0);SamplerState surface:register(s0);"
    "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0,float4 color:COLOR0):SV_Target{float4 c=image.Sample(surface,uv)*color;"
    "float a=c.a;float r=alpha.x;int f=(int)alpha.y;bool visible=f==8 || (f==2 && a<r) || (f==3 && a==r) || (f==4 && a<=r) ||"
    "(f==5 && a>r) || (f==6 && a!=r) || (f==7 && a>=r);if(!visible)discard;return c;}";}
}
#endif
