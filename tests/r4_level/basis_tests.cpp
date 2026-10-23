#include "fixture.h"
#include "../../xr_3da/KichR4LevelData.h"
#include "../../xr_3da/KichR4LightingMath.h"
#include "../../xr_3da/KichR4FrameMetrics.h"
#include <stdio.h>
#include <stddef.h>
#include <limits>
using namespace kich_r4_level;
namespace {
int checks=0;
void Require(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
bool Near(float a,float b){return fabsf(a-b)<1.e-5f;}
Span View(const fixture::Bytes& b){return Span(b.data(),static_cast<uint32_t>(b.size()));}
fixture::Bytes Packed(bool lightmap,bool tree=false){fixture::Bytes file,vb;fixture::U32(vb,1);
    fixture::Element(vb,0,2,0);fixture::Element(vb,12,4,3);fixture::Element(vb,16,4,6);fixture::Element(vb,20,4,7);
    if(!lightmap && !tree)fixture::Element(vb,24,4,10);
    fixture::Element(vb,lightmap||tree?24:28,tree?7:6,5);
    if(lightmap){fixture::Element(vb,28,6,5);vb.back()=1;}
    fixture::End(vb);fixture::U32(vb,2);
    for(unsigned i=0;i<2;++i){fixture::Float(vb,0);fixture::Float(vb,0);fixture::Float(vb,0);
        fixture::U32(vb,0x407f7fffu);fixture::U32(vb,0x80ff7f7fu);fixture::U32(vb,i?0xff7f007fu:0xff7fff7fu);
        if(!lightmap && !tree)fixture::U32(vb,0x80402010u);
        fixture::U16(vb,uint16_t(-1024));fixture::U16(vb,2048);
        if(lightmap){fixture::U16(vb,16384);fixture::U16(vb,uint16_t(-8192));}
        if(tree){fixture::U16(vb,1024);fixture::U16(vb,7);}}
    fixture::Chunk(file,9,vb);return file;
}
void Static(){Scene scene;const fixture::Bytes source=Packed(true);Vertices(View(source),scene);
    const Buffer& buffer=scene.vertices[0];const Vertex& v=buffer.vertices[0];const Vertex& mirrored=buffer.vertices[1];
    Require(sizeof(Vertex)==84 && offsetof(Vertex,tangent)==36 && offsetof(Vertex,binormal)==48 && offsetof(Vertex,uv1)==60 && offsetof(Vertex,attributes)==76,"level GPU layout fixed");
    Require(buffer.basis_vertices==2 && (buffer.available_attributes&15)==15,"actual XRLC declaration carries complete N/T/B and UV1");
    Require(Near(v.uv[0],(-1024.f+128.f/255.f)/1024.f) && Near(v.uv[1],(2048.f+1)/1024.f),"base UV fractions use tangent/binormal alpha and 1024 scale");
    Require(Near(v.uv1[0],.5f) && Near(v.uv1[1],-.25f),"lightmap SHORT2 uses independent 32768 scale");
    Require(Near(v.hemi,64.f/255.f) && !(v.attributes&ATTR_SUN),"normal alpha remains hemisphere; missing sun is explicit");
    Require(v.normal[2]>.999f && v.tangent[0]>.999f && v.binormal[1]>.999f && mirrored.binormal[1]<-.999f,"packed BGRA vectors decode xyz with mirrored handedness retained");
    Scene colored;const fixture::Bytes color=Packed(false);Vertices(View(color),colored);const Vertex& c=colored.vertices[0].vertices[0];
    Require(c.color==0x80102040u && Near(c.sun,128.f/255.f) && (c.attributes&(ATTR_COLOR|ATTR_SUN))==(ATTR_COLOR|ATTR_SUN),"vertex COLOR0 RGB and sun alpha retained independently");
    Require(!(c.attributes&ATTR_UV1),"no fabricated lightmap UV for vertex-lit surfaces");
    Scene tree;const fixture::Bytes trees=Packed(false,true);Vertices(View(trees),tree);const Vertex& t=tree.vertices[0].vertices[0];
    Require(Near(t.uv[0],-.5f) && Near(t.uv[1],1) && Near(t.tree_fraction,.5f),"tree SHORT4 base UV and wave fraction use 2048 scale");
    Require((t.attributes&ATTR_TREE) && !(t.attributes&ATTR_UV1),"tree tc.zw must not be misread as lightmap UV1");
}
void Math(){float n[]={0,0,1},t[]={1,0,.1f},b[]={0,-1,0};
    Require(Basis(n,t,b) && Near(t[2],0) && Near(b[1],-1),"orthogonalization retains source mirrored orientation");
    float collapsed[]={0,0,1};Require(!Basis(n,collapsed,b),"parallel normal and tangent explicitly rejected");
    float bad[]={std::numeric_limits<float>::quiet_NaN(),0,1};Require(!Basis(bad,t,b),"nonfinite source basis explicitly rejected");
    float zero[]={0,0,0};Require(!UnitVector(zero),"zero normal does not fabricate basis");
    // For a world reflection and nonuniform scale normals use inverse transpose,
    // tangents use the linear world matrix. Re-orthogonalization keeps the flip.
    Matrix world=kich_r4_scene::Identity();world.m[0]=-2;world.m[5]=3;world.m[10]=4;
    const Vec3 nn=kich_r4_lighting::Direction({0,0,1},kich_r4_lighting::NormalMatrix(world));
    const Vec3 tt=kich_r4_lighting::Direction({1,0,0},world),bb=kich_r4_lighting::Direction({0,1,0},world);
    float wn[]={nn.x,nn.y,nn.z},wt[]={tt.x,tt.y,tt.z},wb[]={bb.x,bb.y,bb.z};
    Require(Basis(wn,wt,wb) && Near(wt[0],-1) && Near(wb[1],1) && Near(wn[2],1),"reflected/nonuniform world transform preserves TBN handedness");
}
void TreeLighting(){fixture::Level source;const Span visuals=Chunk(View(source.level),3);const Span tree_visual=Chunk(visuals,2);const Span tree=Chunk(tree_visual,12);
    const unsigned offset=static_cast<unsigned>(tree.p-source.level.data())+64;float value=.8f;uint32_t bits;memcpy(&bits,&value,4);
    fixture::Set(source.level,offset+3*4,bits);value=.2f;memcpy(&bits,&value,4);fixture::Set(source.level,offset+8*4,bits);
    Scene scene;Parse(View(source.level),View(source.geometry),scene);
    Require(scene.draws[2].tree && Near(scene.draws[2].tree_scale[3],.4f) && Near(scene.draws[2].tree_bias[3],.1f),"tree hemi coefficients follow FTreeVisual::Load half-scale exactly");
}
void DrawBasis(){fixture::Level source;Scene scene;Parse(View(source.level),View(source.geometry),scene);
    scene.draws.clear();scene.indices.clear();scene.triangles=scene.visuals=scene.skipped=scene.trees=0;
    for(auto& buffer:scene.vertices)for(auto& v:buffer.vertices)v.attributes=127;
    scene.vertices[0].vertices[0].attributes=0; // not referenced by either static visual
    scene.vertices[0].vertices[5].attributes&=~ATTR_TANGENT; // referenced only by SWI visual
    scene.vertices[0].available_attributes=0; // shared VB intersection is deliberately unusable
    Visuals(View(source.level),Indices(View(source.geometry)),TreeWindows(View(source.geometry)),scene);
    Require(scene.draws[0].available_attributes==127,"unreferenced bad vertex cannot disable material basis");
    Require(scene.draws[1].available_attributes==(127u&~ATTR_TANGENT),"referenced bad SWI vertex disables bump only in its mesh");
    Require(scene.draws[2].available_attributes==127 && scene.draws[3].available_attributes==127,"tree and progressive tree use their own referenced basis");
}
void Metrics(){kich_r4_metrics::Window window;window.Add(1000,true);window.Add(1010,true);window.Add(1030,true);
    Require(window.count==2 && Near(float(window.Low()),50),"1 percent low uses slowest ceil(samples/100) intervals");
    window.Add(2000,false);window.Add(3000,true);Require(window.count==2,"snapshot and following gap excluded");
    for(unsigned i=1;i<=120;++i)window.Add(3000+i*10,true);
    Require(window.count==120 && Near(float(window.Low()),100),"bounded window overwrites old spikes");
    window.Gap();window.Add(999999,true);Require(window.count==120 && Near(float(window.Low()),100),"suspension excluded");
}
}
int main(){try{Static();Math();TreeLighting();DrawBasis();Metrics();printf("G3e XRLC draw basis / baked channels / frame windows: %d checks passed\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
