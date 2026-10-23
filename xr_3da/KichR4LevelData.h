#ifndef KICH_R4_LEVEL_DATA_H
#define KICH_R4_LEVEL_DATA_H
/* SoC XRLC v14 / OGF v4 reader. Constants/layouts follow this repository's
 * Fmesh.h, xrLevel.h, xrLC/OGF_Face_Save.cpp and xrMU_Model_export_geometry.cpp.
 * The viewer decodes base UVs, selects highest-detail SWI records and draws
 * each static/tree leaf once; hierarchy and billboard proxy nodes are skipped.
 */
#include "KichR4TestScene.h"
#include "KichR4LevelFiles.h"
#include "KichR4Material.h"
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
struct Material {std::string shader,texture;bool cutout;uint32_t kind;std::string layers[4],texture_list;
    kich_r4_material::Descriptor descriptor;
    uint32_t blend,source,destination,alpha_func;float alpha_ref;
    Material(const std::string& s="",const std::string& t="",bool c=false):shader(s),texture(t),cutout(c),kind(0),blend(0),source(5),destination(6),alpha_func(c?7u:8u),alpha_ref(c?200.f/255.f:0.f){}
};
/* The source channels are independent. In particular tree tc.z is a wind
 * fraction, while TEXCOORD1 is the static lightmap UV (XRLC /32768). */
enum Attribute {ATTR_NORMAL=1,ATTR_TANGENT=2,ATTR_BINORMAL=4,ATTR_UV1=8,
    ATTR_HEMI=16,ATTR_SUN=32,ATTR_COLOR=64,ATTR_TREE=128};
struct Vertex {
    float position[3],uv[2],normal[3],hemi,tangent[3],binormal[3],uv1[2],sun;
    uint32_t color,attributes;float tree_fraction;
};
static_assert(sizeof(Vertex)==84,"G3c static source vertex stride");
struct Buffer {std::vector<Vertex> vertices;uint32_t available_attributes,basis_vertices;
    float tree_fraction_max;
    Buffer():available_attributes(0),basis_vertices(0),tree_fraction_max(0){}
};
struct Draw {
    uint32_t vb,ib,first,count,material;Matrix world;Vec3 low,high;
    float tree_scale[5],tree_bias[5];bool tree;
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
inline bool UnitVector(float* v){
    if(!Finite(v[0]) || !Finite(v[1]) || !Finite(v[2]))return false;
    const double length=double(v[0])*v[0]+double(v[1])*v[1]+double(v[2])*v[2];
    if(!(length>1.e-12) || length>1.e40)return false;
    const float scale=static_cast<float>(1./sqrt(length));for(unsigned i=0;i<3;++i)v[i]*=scale;return true;
}
/* Reject collapsed/invalid bases; do not invent a tangent. Retain the source
 * binormal's orientation so mirrored UV islands keep their handedness. */
inline bool Basis(float* n,float* t,float* b){
    if(!UnitVector(n) || !UnitVector(t) || !UnitVector(b))return false;
    const float nt=n[0]*t[0]+n[1]*t[1]+n[2]*t[2];for(unsigned i=0;i<3;++i)t[i]-=n[i]*nt;
    if(!UnitVector(t))return false;
    const float cross[]={n[1]*t[2]-n[2]*t[1],n[2]*t[0]-n[0]*t[2],n[0]*t[1]-n[1]*t[0]};
    const float hand=cross[0]*b[0]+cross[1]*b[1]+cross[2]*b[2];if(!Finite(hand) || fabsf(hand)<1.e-4f)return false;
    for(unsigned i=0;i<3;++i)b[i]=cross[i]*(hand<0?-1.f:1.f);
    return true;
}
inline bool VectorElement(const unsigned char* p,const Element& e,float* out){
    if(!e.found)return false;
    p+=e.offset;
    if(e.type==4){out[0]=p[2]*(2.f/255)-1;out[1]=p[1]*(2.f/255)-1;out[2]=p[0]*(2.f/255)-1;}
    else if(e.type==2 || e.type==3){memcpy(out,p,12);}else return false;
    return UnitVector(out);
}
inline bool UVElement(const unsigned char* p,const Element& e,float* out,float short_scale){
    if(!e.found)return false;
    p+=e.offset;
    if(e.type==1 || e.type==3){Reader r(Span(p,8));out[0]=r.Float();out[1]=r.Float();}
    else if(e.type==15 || e.type==16){out[0]=Half(p);out[1]=Half(p+2);}
    else if(e.type==6 || e.type==7){out[0]=Short(p)/short_scale;out[1]=Short(p+2)/short_scale;}
    else return false;
    return true;
}
inline void Vertices(Span file,Scene& out){Reader r(Chunk(file,9));const uint32_t count=r.U32();Check(count && count<=4096,"invalid vertex buffer count");out.vertices.resize(count);
    for(uint32_t b=0;b<count;++b){Element pos,uv,uv1,normal,tangent,binormal,color;uint32_t stride=0;bool end=false;
        for(unsigned d=0;d<65;++d){const unsigned char* e=r.Take(8);const uint32_t stream=uint32_t(e[0])|(uint32_t(e[1])<<8),offset=uint32_t(e[2])|(uint32_t(e[3])<<8),type=e[4];
            if(stream==255 && type==17){end=true;break;}
            Check(stream==0 && e[5]==0 && TypeBytes(type),"unsupported vertex declaration");
            Check(offset+TypeBytes(type)<=2048,"vertex stride limit");stride=std::max(stride,offset+TypeBytes(type));
            Element* target=NULL;if(e[7]==0){if(e[6]==0)target=&pos;else if(e[6]==3)target=&normal;else if(e[6]==5)target=&uv;else if(e[6]==6)target=&tangent;else if(e[6]==7)target=&binormal;else if(e[6]==10)target=&color;}
            else if(e[7]==1 && e[6]==5)target=&uv1;
            if(target){Check(!target->found,"duplicate vertex semantic");target->found=true;target->offset=offset;target->type=type;}
        }
        Check(end && stride && pos.found && (pos.type==2 || pos.type==3),"expected FLOAT3/4 position");
        Check(uv.found && (uv.type==1 || uv.type==3 || uv.type==6 || uv.type==7 || uv.type==15 || uv.type==16),"unsupported base UV declaration");
        const uint32_t n=r.U32();Check(n && n<=r.Left()/stride && uint64_t(n)*sizeof(Vertex)<=64u*1024u*1024u,"invalid vertex buffer size");
        Budget(out,uint64_t(n)*sizeof(Vertex));const unsigned char* source=r.Take(n*stride);Buffer& vb=out.vertices[b];vb.vertices.resize(n);vb.available_attributes=~0u;
        for(uint32_t i=0;i<n;++i){const unsigned char* p=source+i*stride;Vertex& v=vb.vertices[i];v={};v.hemi=v.sun=1;v.color=0xffffffffu;Reader xyz(Span(p+pos.offset,12));
            if(VectorElement(p,normal,v.normal))v.attributes|=ATTR_NORMAL;
            else for(unsigned a=0;a<3;++a)v.normal[a]=0;
            if(normal.found && normal.type==4){v.hemi=p[normal.offset+3]/255.f;v.attributes|=ATTR_HEMI;}
            if(VectorElement(p,tangent,v.tangent) && VectorElement(p,binormal,v.binormal) && Basis(v.normal,v.tangent,v.binormal)){
                v.attributes|=ATTR_TANGENT|ATTR_BINORMAL;++vb.basis_vertices;
            }else for(unsigned a=0;a<3;++a)v.tangent[a]=v.binormal[a]=0;
            for(unsigned a=0;a<3;++a){v.position[a]=xyz.Float();Check(fabsf(v.position[a])<=1.e6f,"vertex position limit");}
            Check(UVElement(p,uv,v.uv,uv.type==7?2048.f:1024.f),"unsupported base UV declaration");
            if(uv.type==6 && tangent.found && binormal.found && tangent.type==4 && binormal.type==4){
                v.uv[0]+=p[tangent.offset+3]/(255.f*1024.f);v.uv[1]+=p[binormal.offset+3]/(255.f*1024.f);}
            if(UVElement(p,uv1,v.uv1,32768.f))v.attributes|=ATTR_UV1;
            if(uv.type==7){v.tree_fraction=Short(p+uv.offset+4)/2048.f;v.attributes|=ATTR_TREE;vb.tree_fraction_max=std::max(vb.tree_fraction_max,fabsf(v.tree_fraction));}
            if(color.found && color.type==4){const unsigned char* c=p+color.offset;
                v.color=uint32_t(c[2])|(uint32_t(c[1])<<8)|(uint32_t(c[0])<<16)|(uint32_t(c[3])<<24);
                v.sun=c[3]/255.f;v.attributes|=ATTR_COLOR|ATTR_SUN;}
            vb.available_attributes&=v.attributes;
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
            m.texture_list=textures;m.texture=Lower(textures.substr(0,comma));if(m.texture.size()>4 && m.texture.substr(m.texture.size()-4)==".dds")m.texture.resize(m.texture.size()-4);
            if(!SafeName(m.texture) || m.texture[0]=='$')m.texture.clear();
            m.descriptor.shader=m.shader;m.descriptor.base=m.texture;m.descriptor.source="XRLC_SHADER_TABLE";
            m.descriptor.unsupported_reason="compiled_material_metadata_not_registered";}
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
            /* FTreeVisual::Load halves both _5color records (rgb/hemi/sun). */
            for(unsigned a=0;a<5;++a)draw.tree_scale[a]=tree.Float()*.5f;
            for(unsigned a=0;a<5;++a)draw.tree_bias[a]=tree.Float()*.5f;
            draw.tree=true;++out.trees;
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
