#ifndef KICH_R4_LEVEL_DATA_H
#define KICH_R4_LEVEL_DATA_H
/* SoC XRLC v14 / OGF v4 reader. Constants/layouts follow this repository's
 * Fmesh.h, xrLevel.h, xrLC/OGF_Face_Save.cpp and xrMU_Model_export_geometry.cpp.
 * The viewer decodes base UVs, selects highest-detail SWI records and draws
 * each static/tree leaf once; hierarchy and billboard proxy nodes are skipped.
 */
#include "KichR4TestScene.h"
#include "KichR4LevelFiles.h"
#include <vector>
#include <string>
#include <stdexcept>
#include <limits>
#include <algorithm>
namespace kich_r4_level {
using kich_r4_scene::Matrix;
using kich_r4_scene::Vec3;
inline void Check(bool valid,const char* why){if(!valid)throw std::runtime_error(why);}
inline bool Finite(float n){return n==n && n>=-1.e20f && n<=1.e20f;}
inline float& Axis(Vec3& v,unsigned a){return a==0?v.x:a==1?v.y:v.z;}
struct Span {const unsigned char* p;uint32_t n;Span():p(NULL),n(0){}Span(const unsigned char* a,uint32_t b):p(a),n(b){}};
struct Reader {
    Span data;uint32_t at;explicit Reader(Span s):data(s),at(0){}
    uint32_t Left()const{return data.n-at;}
    const unsigned char* Take(uint32_t n){Check(n<=Left(),"truncated level record");const unsigned char* p=data.p+at;at+=n;return p;}
    uint32_t U32(){const unsigned char* p=Take(4);return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
    uint16_t U16(){const unsigned char* p=Take(2);return uint16_t(uint32_t(p[0])|(uint32_t(p[1])<<8));}
    float Float(){const uint32_t bits=U32();float n;memcpy(&n,&bits,4);Check(Finite(n),"non-finite level float");return n;}
    std::string Z(){std::string s;while(Left()){const char c=static_cast<char>(*Take(1));if(!c)return s;Check(s.size()<4096,"level string too long");s+=c;}throw std::runtime_error("unterminated level string");}
};
inline Span Chunk(Span source,uint32_t wanted,bool required=true){Reader r(source);Span found;
    while(r.Left()){const uint32_t id=r.U32(),bytes=r.U32();
        const unsigned char* p=r.Take(bytes);if((id&0x7fffffffu)==wanted){Check(!(id&0x80000000u),"compressed requested chunk unsupported");Check(!found.p,"duplicate level chunk");found=Span(p,bytes);}}
    Check(found.p || !required,"required level chunk missing");return found;
}
inline void Header(Span file){Reader h(Chunk(file,1));Check(h.U16()==14,"expected SoC XRLC version 14");h.U16();}
inline bool SafeName(const std::string& s){if(s.empty() || s.size()>120 || s[0]=='\\' || s[0]=='/' || s.find("..")!=std::string::npos)return false;
    for(size_t i=0;i<s.size();++i){const unsigned char c=static_cast<unsigned char>(s[i]);if(c<32 || c==':' || c=='"' || c=='<' || c=='>' || c=='|' || c=='*' || c=='?')return false;}return true;}
inline std::string Lower(std::string s){for(size_t i=0;i<s.size();++i){if(s[i]>='A' && s[i]<='Z')s[i]=static_cast<char>(s[i]-'A'+'a');if(s[i]=='/')s[i]='\\';}return s;}
struct Material {std::string shader,texture;bool cutout;};
struct Vertex {float position[3],uv[2];};
static_assert(sizeof(Vertex)==20,"B4.2 vertex stride");
struct Buffer {std::vector<Vertex> vertices;};
struct Draw {
    uint32_t vb,ib,first,count,material;Matrix world;Vec3 low,high;
};
struct Scene {
    std::vector<Material> materials;std::vector<Buffer> vertices;
    std::vector<std::vector<uint32_t> > indices;std::vector<Draw> draws;
    Vec3 low,high;uint32_t visuals,skipped,trees,triangles;uint64_t cpu_bytes;
    Scene():visuals(0),skipped(0),trees(0),triangles(0),cpu_bytes(0){low={1.e20f,1.e20f,1.e20f};high={-1.e20f,-1.e20f,-1.e20f};}
};
inline void Budget(Scene& s,uint64_t bytes){Check(bytes<=192u*1024u*1024u-s.cpu_bytes,"level geometry exceeds 192 MiB viewer budget");s.cpu_bytes+=bytes;}
inline uint32_t TypeBytes(uint32_t type){const unsigned char sizes[]={4,8,12,16,4,4,4,8,4,4,8,4,8,4,4,4,8};return type<17?sizes[type]:0;}
struct Element {uint32_t offset,type;bool found;Element():offset(0),type(0),found(false){}};
inline float Short(const unsigned char* p){const uint32_t n=uint32_t(p[0])|(uint32_t(p[1])<<8);return static_cast<float>(n>=32768?int32_t(n)-65536:int32_t(n));}
inline float Half(const unsigned char* p){const uint32_t h=uint32_t(p[0])|(uint32_t(p[1])<<8),ex=(h>>10)&31,m=h&1023;
    Check(ex!=31,"non-finite half UV");const float n=ex?ldexpf(static_cast<float>(1024+m),static_cast<int>(ex)-25):ldexpf(static_cast<float>(m),-24);return h&32768?-n:n;}
inline void Vertices(Span file,Scene& out){Reader r(Chunk(file,9));const uint32_t count=r.U32();Check(count && count<=4096,"invalid vertex buffer count");out.vertices.resize(count);
    for(uint32_t b=0;b<count;++b){Element pos,uv,tangent,binormal;uint32_t stride=0;bool end=false;
        for(unsigned d=0;d<65;++d){const unsigned char* e=r.Take(8);const uint32_t stream=uint32_t(e[0])|(uint32_t(e[1])<<8),offset=uint32_t(e[2])|(uint32_t(e[3])<<8),type=e[4];
            if(stream==255 && type==17){end=true;break;}
            Check(stream==0 && e[5]==0 && TypeBytes(type),"unsupported vertex declaration");
            Check(offset+TypeBytes(type)<=2048,"vertex stride limit");stride=std::max(stride,offset+TypeBytes(type));
            Element* target=NULL;if(e[7]==0){if(e[6]==0)target=&pos;else if(e[6]==5)target=&uv;else if(e[6]==6)target=&tangent;else if(e[6]==7)target=&binormal;}
            if(target){Check(!target->found,"duplicate vertex semantic");target->found=true;target->offset=offset;target->type=type;}
        }
        Check(end && stride && pos.found && (pos.type==2 || pos.type==3),"expected FLOAT3/4 position");
        Check(uv.found && (uv.type==1 || uv.type==3 || uv.type==6 || uv.type==7 || uv.type==15 || uv.type==16),"unsupported base UV declaration");
        const uint32_t n=r.U32();Check(n && n<=r.Left()/stride && uint64_t(n)*sizeof(Vertex)<=64u*1024u*1024u,"invalid vertex buffer size");
        Budget(out,uint64_t(n)*sizeof(Vertex));const unsigned char* source=r.Take(n*stride);Buffer& vb=out.vertices[b];vb.vertices.resize(n);
        for(uint32_t i=0;i<n;++i){const unsigned char* p=source+i*stride;Vertex& v=vb.vertices[i];Reader xyz(Span(p+pos.offset,12));
            for(unsigned a=0;a<3;++a){v.position[a]=xyz.Float();Check(fabsf(v.position[a])<=1.e6f,"vertex position limit");}
            const unsigned char* tc=p+uv.offset;
            if(uv.type==1 || uv.type==3){Reader rr(Span(tc,8));v.uv[0]=rr.Float();v.uv[1]=rr.Float();}
            else if(uv.type==15 || uv.type==16){v.uv[0]=Half(tc);v.uv[1]=Half(tc+2);}
            else{const float quant=uv.type==7?2048.f:1024.f;v.uv[0]=Short(tc)/quant;v.uv[1]=Short(tc+2)/quant;
                if(uv.type==6 && tangent.found && binormal.found && tangent.type==4 && binormal.type==4){
                    v.uv[0]+=static_cast<float>(p[tangent.offset+3])/(255.f*quant);v.uv[1]+=static_cast<float>(p[binormal.offset+3])/(255.f*quant);}}
        }
    }Check(!r.Left(),"unexpected vertex buffer tail");
}
inline std::vector<Span> Indices(Span file){Reader r(Chunk(file,10));const uint32_t count=r.U32();Check(count && count<=4096,"invalid index buffer count");std::vector<Span> buffers;
    for(uint32_t i=0;i<count;++i){const uint32_t n=r.U32();Check(n && n<=r.Left()/2,"invalid index buffer size");buffers.push_back(Span(r.Take(n*2),n*2));}
    Check(!r.Left(),"unexpected index buffer tail");return buffers;
}
struct Window {uint32_t offset,triangles,vertices;};
inline Window FirstWindow(Span data){Reader r(data);r.Take(16);const uint32_t count=r.U32();Check(count && count<=r.Left()/8,"invalid progressive window count");
    const Window w={r.U32(),r.U16(),r.U16()};Check(w.triangles && w.vertices,"empty progressive window");return w;}
inline std::vector<Window> TreeWindows(Span file){const Span c=Chunk(file,11,false);std::vector<Window> list;if(!c.p)return list;
    Reader r(c);const uint32_t count=r.U32();Check(count<=100000,"tree SWI limit");
    for(uint32_t i=0;i<count;++i){const uint32_t start=r.at;r.Take(16);const uint32_t n=r.U32();Check(n && n<=r.Left()/8,"invalid tree SWI");
        list.push_back(FirstWindow(Span(c.p+start,20+n*8)));r.Take(n*8);}
    Check(!r.Left(),"unexpected tree SWI tail");return list;
}
inline void Materials(Span level,Scene& out){Reader r(Chunk(level,2));const uint32_t count=r.U32();Check(count && count<=65536,"shader table limit");
    for(uint32_t i=0;i<count;++i){const std::string entry=r.Z();Material m={"","",false};const size_t slash=entry.find('/');
        if(slash!=std::string::npos){m.shader=Lower(entry.substr(0,slash));std::string textures=entry.substr(slash+1);const size_t comma=textures.find(',');
            m.texture=Lower(textures.substr(0,comma));if(m.texture.size()>4 && m.texture.substr(m.texture.size()-4)==".dds")m.texture.resize(m.texture.size()-4);
            if(!SafeName(m.texture) || m.texture[0]=='$')m.texture.clear();
            m.cutout=m.shader.find("aref")!=std::string::npos || m.shader.find("flora")!=std::string::npos || m.shader.find("tree")!=std::string::npos;}
        out.materials.push_back(m);
    }Check(!r.Left(),"unexpected shader table tail");
}
inline void Visuals(Span level,const std::vector<Span>& ibs,const std::vector<Window>& swis,Scene& out){Reader table(Chunk(level,3));
    while(table.Left()){const uint32_t id=table.U32(),size=table.U32();Check(id==out.visuals && id<100000,"non-sequential or excessive visual table");
        const Span visual(table.Take(size),size);++out.visuals;Reader h(Chunk(visual,1));const unsigned char* prefix=h.Take(2);
        Check(prefix[0]==4,"expected OGF format 4");const uint32_t type=prefix[1],shader=h.U16();
        Draw draw={};draw.material=shader;draw.world=kich_r4_scene::Identity();
        for(unsigned a=0;a<3;++a)Axis(draw.low,a)=h.Float();
        for(unsigned a=0;a<3;++a){Axis(draw.high,a)=h.Float();Check(Axis(draw.low,a)<=Axis(draw.high,a) && fabsf(Axis(draw.low,a))<=1.e6f && fabsf(Axis(draw.high,a))<=1.e6f,"invalid visual bounds");}h.Take(16);
        if(type!=0 && type!=2 && type!=7 && type!=11){++out.skipped;continue;}
        Check(shader<out.materials.size(),"visual shader index out of range");
        const Span combined=Chunk(visual,21,false);uint32_t vb,vbase,vcount,ib,ibase,icount;
        if(combined.p){Reader g(combined);vb=g.U32();vbase=g.U32();vcount=g.U32();ib=g.U32();ibase=g.U32();icount=g.U32();}
        else{Reader v(Chunk(visual,7)),i(Chunk(visual,8));vb=v.U32();vbase=v.U32();vcount=v.U32();ib=i.U32();ibase=i.U32();icount=i.U32();}
        Check(vb<out.vertices.size() && ib<ibs.size(),"geometry container index out of range");
        const size_t capacity=out.vertices[vb].vertices.size();Check(vcount && vbase<=capacity && vcount<=capacity-vbase,"vertex container range");
        uint32_t offset=0,count=icount;
        if(type==2){const Window w=FirstWindow(Chunk(visual,6));offset=w.offset;count=w.triangles*3;Check(w.vertices<=vcount,"progressive vertex range");}
        if(type==7 || type==11){Reader tree(Chunk(visual,12));
            for(unsigned a=0;a<16;++a){if(a%4==3){tree.Take(4);draw.world.m[a]=a==15?1.f:0.f;}
                else{draw.world.m[a]=tree.Float();Check(fabsf(draw.world.m[a])<=1.e6f,"tree transform limit");}}
            ++out.trees;
            if(type==11){Reader link(Chunk(visual,20));const uint32_t sw=link.U32();Check(sw<swis.size(),"tree SWI index out of range");const Window w=swis[sw];offset=w.offset;count=w.triangles*3;Check(w.vertices<=vcount,"tree progressive vertex range");}}
        Check(count && count%3==0 && offset<=icount && count<=icount-offset,"visual triangle range");
        const Span indices=ibs[ib];const uint32_t available=indices.n/2;Check(ibase<=available && icount<=available-ibase,"index container range");
        if(out.indices.empty() || out.indices.back().size()+count>4u*1024u*1024u)out.indices.push_back(std::vector<uint32_t>());
        Check(count<=4u*1024u*1024u,"single visual index limit");Budget(out,uint64_t(count)*4);
        draw.vb=vb;draw.ib=static_cast<uint32_t>(out.indices.size()-1);draw.first=static_cast<uint32_t>(out.indices.back().size());draw.count=count;
        Reader source(Span(indices.p+(ibase+offset)*2,count*2));std::vector<uint32_t>& target=out.indices.back();
        for(uint32_t i=0;i<count;++i){const uint32_t vertex=source.U16();Check(vertex<vcount,"index addresses outside visual vertex slice");target.push_back(vbase+vertex);}
        out.triangles+=count/3;out.draws.push_back(draw);
        for(unsigned a=0;a<3;++a){Axis(out.low,a)=std::min(Axis(out.low,a),Axis(draw.low,a));Axis(out.high,a)=std::max(Axis(out.high,a),Axis(draw.high,a));}
    }
    Check(!out.draws.empty(),"no supported static geometry in level");
}
inline void Parse(Span level,Span geometry,Scene& out){Header(level);Header(geometry);Materials(level,out);Vertices(geometry,out);
    const std::vector<Span> ibs=Indices(geometry);const std::vector<Window> swis=TreeWindows(geometry);Visuals(level,ibs,swis,out);}
struct File {
    const KichR4LevelFiles& api;KichR4FileView view;
    explicit File(const KichR4LevelFiles& a):api(a){memset(&view,0,sizeof(view));}
    ~File(){if(view.owner)api.close(&view);}
    int32_t Open(const char* alias,const char* name){return api.open(alias,name,&view);}
    Span Data()const{return Span(view.data,view.bytes);}
private:File(const File&);File& operator=(const File&);
};
inline const char* VertexShader(){return "cbuffer Frame:register(b0){row_major float4x4 mvp;float4 tint;};"
    "struct I{float3 p:POSITION;float2 uv:TEXCOORD0;};struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;};"
    "O main(I v){O o;o.p=mul(float4(v.p,1),mvp);o.uv=v.uv;return o;}";}
inline const char* PixelShader(){return "cbuffer Frame:register(b0){row_major float4x4 mvp;float4 tint;};"
    "Texture2D base_texture:register(t0);SamplerState surface:register(s0);"
    "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{float4 c=base_texture.Sample(surface,uv);clip(c.a-tint.a);return float4(c.rgb*tint.rgb,1);}";}
}
#endif
