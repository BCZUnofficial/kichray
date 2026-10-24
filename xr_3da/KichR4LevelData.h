#ifndef KICH_R4_LEVEL_DATA_H
#define KICH_R4_LEVEL_DATA_H
/* SoC XRLC v14 / OGF v4 reader. Constants/layouts follow this repository's
 * Fmesh.h, xrLevel.h, xrLC/OGF_Face_Save.cpp and xrMU_Model_export_geometry.cpp.
 * The viewer decodes base UVs and retains progressive SWI records for existing
 * static/tree leaves; hierarchy and billboard proxy nodes are not drawn.
 */
#include "KichR4TestScene.h"
#include "KichR4LevelFiles.h"
#include "KichR4Material.h"
#include "KichR4LodPolicy.h"
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
    uint32_t vb,ib,first,count,material,available_attributes,visual;Matrix world;Vec3 low,high;
    float tree_scale[5],tree_bias[5];bool tree;uint32_t lod;
};
struct DrawRange {uint32_t first,count,available_attributes;};
// Portal topology is optional: unsupported/corrupt topology must never make
// otherwise valid static geometry disappear.
struct Portal {uint32_t front,back;std::vector<Vec3> vertices;Vec3 normal;float distance;};
struct Sector {uint32_t root;std::vector<uint32_t> portals,draws;};
struct Topology {
    std::vector<Portal> portals;std::vector<Sector> sectors;bool valid;
    Topology():valid(false){}
};
struct VisualNode {
    std::vector<uint32_t> children;uint32_t draw;
    VisualNode():draw(UINT32_MAX){}
};
struct Scene {
    std::vector<Material> materials;std::vector<Buffer> vertices;
    std::vector<std::vector<uint32_t> > indices;std::vector<Draw> draws;
    std::vector<std::vector<DrawRange> > lods;
    std::vector<VisualNode> nodes;Topology topology;
    std::vector<unsigned char> sector_owned;bool hierarchy_valid,keep_lods;
    Vec3 low,high;uint32_t visuals,skipped,trees,triangles,lod_budget_fallbacks;uint64_t cpu_bytes,geometry_budget;
    Scene():hierarchy_valid(true),keep_lods(true),visuals(0),skipped(0),trees(0),triangles(0),lod_budget_fallbacks(0),cpu_bytes(0),geometry_budget(192u*1024u*1024u){low={1.e20f,1.e20f,1.e20f};high={-1.e20f,-1.e20f,-1.e20f};}
};
inline DrawRange SelectDrawRange(const Scene& scene,const Draw& draw,float detail){
    if(draw.lod && draw.lod<=scene.lods.size()){
        const std::vector<DrawRange>& ranges=scene.lods[draw.lod-1];
        if(!ranges.empty())return ranges[kich_r4_lod::Window(detail,static_cast<uint32_t>(ranges.size()))];}
    const DrawRange range={draw.first,draw.count,draw.available_attributes};return range;
}
struct GeometryBudgetExceeded:std::runtime_error {GeometryBudgetExceeded():std::runtime_error("level geometry exceeds viewer budget"){} };
inline void Budget(Scene& s,uint64_t bytes){if(s.cpu_bytes>s.geometry_budget || bytes>s.geometry_budget-s.cpu_bytes)throw GeometryBudgetExceeded();s.cpu_bytes+=bytes;}
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
inline std::vector<Window> ReadWindows(Span data){Reader r(data);r.Take(16);const uint32_t count=r.U32();Check(count && count<=r.Left()/8,"invalid progressive window count");
    std::vector<Window> windows;windows.reserve(count);
    for(uint32_t i=0;i<count;++i){const Window w={r.U32(),r.U16(),r.U16()};Check(w.triangles && w.vertices,"empty progressive window");windows.push_back(w);}
    Check(!r.Left(),"unexpected progressive window tail");return windows;}
inline Window FirstWindow(Span data){return ReadWindows(data).front();}
inline std::vector<std::vector<Window> > TreeWindows(Span file){const Span c=Chunk(file,11,false);std::vector<std::vector<Window> > list;if(!c.p)return list;
    Reader r(c);const uint32_t count=r.U32();Check(count<=100000,"tree SWI limit");
    for(uint32_t i=0;i<count;++i){const uint32_t start=r.at;r.Take(16);const uint32_t n=r.U32();Check(n && n<=r.Left()/8,"invalid tree SWI");
        list.push_back(ReadWindows(Span(c.p+start,20+n*8)));r.Take(n*8);}
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
inline void Visuals(Span level,const std::vector<Span>& ibs,const std::vector<std::vector<Window> >& swis,Scene& out){Reader table(Chunk(level,3));uint64_t hierarchy_links=0;
    while(table.Left()){const uint32_t id=table.U32(),size=table.U32();Check(id==out.visuals && id<100000,"non-sequential or excessive visual table");
        const Span visual(table.Take(size),size);++out.visuals;Reader h(Chunk(visual,1));const unsigned char* prefix=h.Take(2);
        Check(prefix[0]==4,"expected OGF format 4");const uint32_t type=prefix[1],shader=h.U16();
        out.nodes.push_back(VisualNode());
        try{const Span child=Chunk(visual,10,false);if(child.p){Reader r(child);const uint32_t n=r.U32();hierarchy_links+=n;
                Check(n<=100000 && n<=r.Left()/4 && hierarchy_links<=4000000,"invalid/excessive visual child count");
                out.nodes.back().children.reserve(n);for(uint32_t k=0;k<n;++k)out.nodes.back().children.push_back(r.U32());Check(!r.Left(),"unexpected visual children tail");}
            else if(type==1 || type==6)out.hierarchy_valid=false;
        }catch(const std::runtime_error&){out.hierarchy_valid=false;}
        Draw draw={};draw.visual=id;draw.material=shader;draw.world=kich_r4_scene::Identity();
        for(unsigned a=0;a<3;++a)Axis(draw.low,a)=h.Float();
        for(unsigned a=0;a<3;++a){Axis(draw.high,a)=h.Float();Check(Axis(draw.low,a)<=Axis(draw.high,a) && fabsf(Axis(draw.low,a))<=1.e6f && fabsf(Axis(draw.high,a))<=1.e6f,"invalid visual bounds");}h.Take(16);
        if(type!=0 && type!=2 && type!=7 && type!=11){++out.skipped;continue;}
        Check(shader<out.materials.size(),"visual shader index out of range");
        const Span combined=Chunk(visual,21,false);uint32_t vb,vbase,vcount,ib,ibase,icount;
        if(combined.p){Reader g(combined);vb=g.U32();vbase=g.U32();vcount=g.U32();ib=g.U32();ibase=g.U32();icount=g.U32();}
        else{Reader v(Chunk(visual,7)),i(Chunk(visual,8));vb=v.U32();vbase=v.U32();vcount=v.U32();ib=i.U32();ibase=i.U32();icount=i.U32();}
        Check(vb<out.vertices.size() && ib<ibs.size(),"geometry container index out of range");
        const size_t capacity=out.vertices[vb].vertices.size();Check(vcount && vbase<=capacity && vcount<=capacity-vbase,"vertex container range");
        std::vector<Window> windows;
        if(type==2)windows=ReadWindows(Chunk(visual,6));
        if(type==7 || type==11){Reader tree(Chunk(visual,12));
            for(unsigned a=0;a<16;++a){if(a%4==3){tree.Take(4);draw.world.m[a]=a==15?1.f:0.f;}
                else{draw.world.m[a]=tree.Float();Check(fabsf(draw.world.m[a])<=1.e6f,"tree transform limit");}}
            /* FTreeVisual::Load halves both _5color records (rgb/hemi/sun). */
            for(unsigned a=0;a<5;++a)draw.tree_scale[a]=tree.Float()*.5f;
            for(unsigned a=0;a<5;++a)draw.tree_bias[a]=tree.Float()*.5f;
            draw.tree=true;++out.trees;
            if(type==11){Reader link(Chunk(visual,20));const uint32_t sw=link.U32();Check(sw<swis.size(),"tree SWI index out of range");windows=swis[sw];}}
        if(windows.empty()){Check(icount && icount%3==0,"visual triangle count");const Window window={0,icount/3,vcount};windows.push_back(window);}
        uint32_t first=UINT32_MAX,last=0;
        for(size_t i=0;i<windows.size();++i){const Window& w=windows[i];const uint32_t count=w.triangles*3;
            Check(w.vertices && w.vertices<=vcount && count && w.offset<=icount && count<=icount-w.offset,"progressive visual range");
            first=(std::min)(first,w.offset);last=(std::max)(last,w.offset+count);}
        const Span indices=ibs[ib];const uint32_t available=indices.n/2;Check(ibase<=available && icount<=available-ibase,"index container range");
        // Retain the union once, not one duplicate IB per SWI. If optional LOD
        // storage would exceed the established budget, keep the original max
        // detail slice instead of making an otherwise loadable level fail.
        uint64_t bytes=uint64_t(last-first)*4+(windows.size()>1?uint64_t(windows.size())*sizeof(DrawRange):0);
        if(windows.size()>1 && (!out.keep_lods || last-first>4u*1024u*1024u || out.cpu_bytes>out.geometry_budget || bytes>out.geometry_budget-out.cpu_bytes)){
            windows.resize(1);first=windows[0].offset;last=first+windows[0].triangles*3;bytes=uint64_t(last-first)*4;++out.lod_budget_fallbacks;}
        Check(last-first<=4u*1024u*1024u,"single visual index limit");Budget(out,bytes);
        if(out.indices.empty() || out.indices.back().size()+last-first>4u*1024u*1024u)out.indices.push_back(std::vector<uint32_t>());
        draw.vb=vb;draw.ib=static_cast<uint32_t>(out.indices.size()-1);const uint32_t base=static_cast<uint32_t>(out.indices.back().size());
        std::vector<uint32_t>& target=out.indices.back();target.resize(base+last-first,0);
        std::vector<DrawRange> ranges;ranges.reserve(windows.size());
        for(size_t range=0;range<windows.size();++range){const Window& w=windows[range];const uint32_t count=w.triangles*3;
            Reader source(Span(indices.p+(ibase+w.offset)*2,count*2));DrawRange selected={base+w.offset-first,count,~0u};
            for(uint32_t i=0;i<count;++i){const uint32_t vertex=source.U16();Check(vertex<w.vertices,"index addresses outside progressive vertex prefix");
                target[selected.first+i]=vbase+vertex;selected.available_attributes&=out.vertices[vb].vertices[vbase+vertex].attributes;}
            ranges.push_back(selected);}
        draw.first=ranges[0].first;draw.count=ranges[0].count;draw.available_attributes=ranges[0].available_attributes;
        if(ranges.size()>1){out.lods.push_back(ranges);draw.lod=static_cast<uint32_t>(out.lods.size());}
        out.nodes.back().draw=static_cast<uint32_t>(out.draws.size());out.triangles+=draw.count/3;out.draws.push_back(draw);
        for(unsigned a=0;a<3;++a){Axis(out.low,a)=std::min(Axis(out.low,a),Axis(draw.low,a));Axis(out.high,a)=std::max(Axis(out.high,a),Axis(draw.high,a));}
    }
    Check(!out.draws.empty(),"no supported static geometry in level");
}
/* Matches xrRender_R2/r2_loader.cpp b_portal and r__sector.cpp::load.
 * svector stores its six vertices BEFORE its u32 count. Unused vertex bytes
 * are unspecified and deliberately not interpreted. */
inline bool LoadTopology(Span level,Topology& result){
    result=Topology();
    try{Topology t;const Span sectors=Chunk(level,8,false),portals=Chunk(level,4,false);
        if(!sectors.p || !portals.p)return false;
        Check(portals.n%80==0 && portals.n/80<=65536,"invalid portal table");
        Reader sr(sectors);uint64_t link_count=0;
        while(sr.Left()){const uint32_t id=sr.U32(),bytes=sr.U32();Check(id==t.sectors.size() && id<65535,"invalid sector table");
            const Span sector(sr.Take(bytes),bytes);Reader root(Chunk(sector,2));Sector s;s.root=root.U32();Check(!root.Left(),"invalid sector root");
            Reader links(Chunk(sector,1));link_count+=links.Left()/2;
            Check(links.Left()%2==0 && link_count<=2u*(portals.n/80),"invalid/excessive sector portal links");
            while(links.Left())s.portals.push_back(links.U16());
            t.sectors.push_back(s);}
        Check(!t.sectors.empty(),"empty sector table");Reader pr(portals);
        while(pr.Left()){Portal p;p.front=pr.U16();p.back=pr.U16();const Span vertices(pr.Take(72),72);const uint32_t n=pr.U32();
            Check(p.front<t.sectors.size() && p.back<t.sectors.size() && p.front!=p.back && n>=3 && n<=6,"invalid portal record");
            Reader vr(vertices);for(uint32_t i=0;i<n;++i){Vec3 v={vr.Float(),vr.Float(),vr.Float()};p.vertices.push_back(v);}
            p.normal={0,0,0};for(uint32_t i=2;i<n;++i){const Vec3& a=p.vertices[0];const Vec3& b=p.vertices[i-1];const Vec3& c=p.vertices[i];
                const Vec3 ab={b.x-a.x,b.y-a.y,b.z-a.z},ac={c.x-a.x,c.y-a.y,c.z-a.z};Vec3 normal=kich_r4_scene::Cross(ab,ac);
                if(UnitVector(&normal.x)){p.normal.x+=normal.x;p.normal.y+=normal.y;p.normal.z+=normal.z;}}
            Check(UnitVector(&p.normal.x),"degenerate portal");p.distance=-kich_r4_scene::Dot(p.normal,p.vertices[0]);
            for(uint32_t i=1;i<n;++i)Check(fabsf(kich_r4_scene::Dot(p.normal,p.vertices[i])+p.distance)<.05f,"non-planar portal");
            t.portals.push_back(p);}
        std::vector<unsigned char> linked(t.portals.size(),0);
        for(uint32_t i=0;i<t.sectors.size();++i){const Sector& s=t.sectors[i];for(size_t j=0;j<s.portals.size();++j){const uint32_t id=s.portals[j];
                Check(id<t.portals.size(),"portal link out of range");const Portal& p=t.portals[id];
                const unsigned char bit=p.front==i?1:p.back==i?2:0;Check(bit && !(linked[id]&bit),"inconsistent/duplicate portal link");linked[id]|=bit;}}
        for(size_t i=0;i<linked.size();++i)Check(linked[i]==3,"missing reciprocal portal link");
        t.valid=true;result=t;return true;
    }catch(const std::runtime_error&){return false;}
}
inline void MapSectorDraws(Scene& scene){
    scene.sector_owned.assign(scene.draws.size(),0);
    if(!scene.topology.valid || !scene.hierarchy_valid){scene.topology.valid=false;return;}
    try{const size_t count=scene.nodes.size();std::vector<uint32_t> incoming(count,0),pending;
        uint64_t edges=0;for(size_t i=0;i<count;++i)for(size_t j=0;j<scene.nodes[i].children.size();++j){const uint32_t child=scene.nodes[i].children[j];
            Check(child<count && ++edges<=4000000,"invalid/excessive visual hierarchy");++incoming[child];}
        for(uint32_t i=0;i<count;++i)if(!incoming[i])pending.push_back(i);
        for(size_t at=0;at<pending.size();++at){const VisualNode& node=scene.nodes[pending[at]];
            for(size_t j=0;j<node.children.size();++j)if(!--incoming[node.children[j]])pending.push_back(node.children[j]);}
        Check(pending.size()==count,"cyclic visual hierarchy");
        std::vector<uint32_t> visited(count,UINT32_MAX);uint64_t steps=0;
        for(uint32_t sector=0;sector<scene.topology.sectors.size();++sector){Sector& s=scene.topology.sectors[sector];s.draws.clear();
            Check(s.root<count,"sector visual root out of range");pending.clear();pending.push_back(s.root);
            for(size_t at=0;at<pending.size();++at){const uint32_t id=pending[at];if(visited[id]==sector)continue;visited[id]=sector;
                Check(++steps<=4000000,"excessive sector hierarchy traversal");const VisualNode& node=scene.nodes[id];
                if(node.draw!=UINT32_MAX){Check(node.draw<scene.draws.size(),"invalid sector draw");s.draws.push_back(node.draw);scene.sector_owned[node.draw]=1;}
                for(size_t child=0;child<node.children.size();++child)pending.push_back(node.children[child]);}}
    }catch(const std::runtime_error&){scene.topology.valid=false;scene.sector_owned.assign(scene.draws.size(),0);}
}
inline void ParseContents(Span level,Span geometry,Scene& out){Header(level);Header(geometry);Materials(level,out);Vertices(geometry,out);
    const std::vector<Span> ibs=Indices(geometry);const std::vector<std::vector<Window> > swis=TreeWindows(geometry);Visuals(level,ibs,swis,out);
    LoadTopology(level,out.topology);MapSectorDraws(out);}
inline void Parse(Span level,Span geometry,Scene& out){
    try{ParseContents(level,geometry,out);}
    catch(const GeometryBudgetExceeded&){
        if(!out.keep_lods || out.lods.empty())throw;
        // Earlier optional tables must not consume storage needed by a later
        // mandatory visual. Retry the established max-detail representation.
        const uint64_t budget=out.geometry_budget;out=Scene();out.geometry_budget=budget;out.keep_lods=false;ParseContents(level,geometry,out);
    }
}
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
