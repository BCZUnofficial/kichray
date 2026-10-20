#include "fixture.h"
#include "../../xr_3da/KichR4LevelCamera.h"
#include "../../xr_3da/KichR4Dds.h"
#include "../../xr_3da/KichR4LevelOption.h"
#include <stdio.h>
#include <cmath>
using namespace kich_r4_level;
namespace {
int checks=0;
void Require(bool good,const char* why){++checks;if(!good)throw std::runtime_error(why);}
bool Near(float a,float b){return fabsf(a-b)<.00001f;}
Span View(const fixture::Bytes& b){return Span(b.data(),static_cast<uint32_t>(b.size()));}
void Data(){fixture::Level file;Scene s;Parse(View(file.level),View(file.geometry),s);
    Require(s.visuals==6 && s.skipped==2 && s.draws.size()==4 && s.trees==2 && s.triangles==4,"static/progressive/trees included once, hierarchy/LOD proxy skipped");
    Require(s.materials.size()==3 && s.materials[1].texture=="base" && s.materials[2].cutout,"material texture list and alpha-cutout mapping");
    const Vertex& normal=s.vertices[0].vertices[0];Require(Near(normal.uv[0],(-1024.f+128.f/255.f)/1024.f) && Near(normal.uv[1],2049.f/1024.f),"SoC SHORT2 UV fractions from tangent/binormal alpha");
    Require(Near(s.vertices[1].vertices[0].uv[0],.5f) && Near(s.vertices[1].vertices[0].uv[1],1),"tree SHORT4 UV scale is 1/2048");
    const uint32_t expected[]={1,2,3,4,5,6,0,1,2,0,1,2};
    Require(s.indices.size()==1 && s.indices[0].size()==12,"highest-detail SWI slice only");
    for(unsigned i=0;i<12;++i)Require(s.indices[0][i]==expected[i],"per-visual base vertex added exactly once");
    Require(Near(s.draws[2].world.m[12],10) && Near(s.draws[3].world.m[12],10),"tree world transform preserved");
    Require(Near(s.low.x,-1) && Near(s.high.x,11),"world bounds cover static and instanced meshes");
    fixture::Bytes unrelated(4,0);fixture::Chunk(file.level,0x80000055u,unrelated);Scene ignored;
    Parse(View(file.level),View(file.geometry),ignored);Require(ignored.draws.size()==4,"unrelated compressed chunks ignored");
    file=fixture::Level();
    fixture::Level separate(true);Scene split;Parse(View(separate.level),View(separate.geometry),split);
    Require(split.indices==s.indices && split.draws.size()==s.draws.size(),"separate VCONTAINER/ICONTAINER retain the same geometry");
    for(size_t n=0;n<file.level.size();++n){Scene partial;bool rejected=false;try{Parse(Span(file.level.data(),static_cast<uint32_t>(n)),View(file.geometry),partial);}catch(const std::exception&){rejected=true;}Require(rejected,"every truncated level rejected");}
    for(size_t n=0;n<file.geometry.size();++n){Scene partial;bool rejected=false;try{Parse(View(file.level),Span(file.geometry.data(),static_cast<uint32_t>(n)),partial);}catch(const std::exception&){rejected=true;}Require(rejected,"every truncated geometry file rejected");}
    for(unsigned mode=0;mode<5;++mode){fixture::Level bad;const Span chunk=Chunk(View(bad.geometry),mode<3?9:10);const size_t at=static_cast<size_t>(chunk.p-bad.geometry.data());
        if(mode==0)fixture::Set(bad.geometry,at,UINT32_MAX);else if(mode==1)bad.geometry[at+8]=255;
        else if(mode==2)fixture::Set(bad.geometry,at+4+7*8,UINT32_MAX);else if(mode==3)fixture::Set(bad.geometry,at+4,UINT32_MAX);
        else bad.geometry[at+8+2]=255;
        Scene partial;bool rejected=false;try{Parse(View(bad.level),View(bad.geometry),partial);}catch(const std::exception&){rejected=true;}Require(rejected,"bad buffer declaration/count/index rejected");}
    Require(!SafeName("../base") && !SafeName("C:\\base") && SafeName("brick\\wall"),"texture path validation");
}
void Dds(){using namespace kich_r4_dds;
    const uint32_t formats[]={0,FourCC('D','X','T','1'),FourCC('D','X','T','3'),FourCC('D','X','T','5')};
    for(unsigned f=0;f<4;++f){fixture::Bytes data=fixture::Dds(8,8,4,formats[f]);Image image={};
        Require(Parse(data.data(),static_cast<uint32_t>(data.size()),2048,image) && image.count==4 && image.width==8,"DDS full mip chain");
        Require(Parse(data.data(),static_cast<uint32_t>(data.size()),4,image) && image.width==4 && image.count==3,"DDS skips large mips without copying");
        for(size_t n=0;n<data.size();++n){Image output={};output.width=123;
            Require(!Parse(data.data(),static_cast<uint32_t>(n),2048,output) && output.width==123,"truncated DDS never publishes partial output");}
    }
    const fixture::Bytes good=fixture::Dds();
    for(unsigned mode=0;mode<10;++mode){fixture::Bytes b=good;
        switch(mode){case 0:fixture::Set(b,0,0);break;case 1:fixture::Set(b,4,123);break;case 2:fixture::Set(b,16,0);break;
        case 3:fixture::Set(b,12,16385);break;case 4:fixture::Set(b,28,UINT32_MAX);break;case 5:fixture::Set(b,76,0);break;
        case 6:fixture::Set(b,112,0x200);break;case 7:fixture::Set(b,24,5);break;case 8:fixture::Set(b,92,123);break;
        case 9:fixture::Set(b,8,0x100f);fixture::Set(b,20,36);break;}
        Image image={};Require(!Parse(b.data(),static_cast<uint32_t>(b.size()),2048,image),"invalid DDS header rejected");
    }
    fixture::Bytes dx=fixture::Dds(8,8,1,FourCC('D','X','T','5'));dx.insert(dx.begin()+128,20,0);fixture::Set(dx,84,FourCC('D','X','1','0'));
    fixture::Set(dx,128,98);fixture::Set(dx,132,3);fixture::Set(dx,140,1);Image image={};
    Require(Parse(dx.data(),static_cast<uint32_t>(dx.size()),2048,image) && image.format==BC7,"DX10 BC7 2D DDS");
    fixture::Set(dx,140,2);Require(!Parse(dx.data(),static_cast<uint32_t>(dx.size()),2048,image),"DDS arrays rejected");
    const unsigned char color[]={10,20,30,40};unsigned char rgba[4]={};
    ToRGBA(BGRA,color,rgba);Require(rgba[0]==30 && rgba[1]==20 && rgba[2]==10 && rgba[3]==40,"BGRA channel order preserved");
    ToRGBA(BGRX,color,rgba);Require(rgba[0]==30 && rgba[3]==255,"BGRX unused alpha forced opaque");
    ToRGBA(RGBX,color,rgba);Require(rgba[0]==10 && rgba[3]==255,"RGBX unused alpha forced opaque");
    ToRGBA(R8,color,rgba);Require(rgba[0]==10 && rgba[1]==10 && rgba[2]==10 && rgba[3]==255,"legacy luminance stays grayscale");
}
void Cameras(){Camera camera;camera.Reset({-100,-20,-100},{100,50,100});const Matrix vp=camera.ViewProjection(960,540);
    Draw d={};d.low={-10,-10,-10};d.high={10,10,10};Require(Visible(d,vp),"overview contains level center");
    const Vec3 original=camera.position;camera.Advance(Forward,.1f);Require(!Near(original.z,camera.position.z),"free camera moves forward");
    for(unsigned i=0;i<2000;++i)camera.Advance(PitchUp|YawRight,.1f);
    Require(camera.pitch<=1.5f && fabsf(camera.yaw)<6.284f,"camera angles stay bounded");
    const Matrix a=camera.ViewProjection(540,960);for(unsigned i=0;i<16;++i)Require(std::isfinite(a.m[i]),"portrait camera matrix finite");
    d.low={2,0,.2f};d.high={3,1,.8f};Require(!Visible(d,kich_r4_scene::Identity()),"frustum rejects right plane");
    d.low={-.5f,-.5f,.2f};d.high={.5f,.5f,.8f};Require(Visible(d,kich_r4_scene::Identity()),"frustum keeps intersecting box");
    d.low.z=-2;d.high.z=-1;Require(!Visible(d,kich_r4_scene::Identity()),"D3D zero near plane");
    const float before=camera.position.x;Require(!camera.Advance(Forward,std::numeric_limits<float>::quiet_NaN()) && camera.position.x==before,"invalid frame delta ignored");
}
}
int main(){try{Data();Dds();Cameras();printf("B4.2 production level/DDS/camera: %d checks passed\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
