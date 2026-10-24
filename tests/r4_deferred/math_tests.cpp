#include "../../xr_3da/KichR4CaptureData.h"
#include "../../xr_3da/xrRender_R4/R4DeferredRules.h"
#include "../r4_capture/fixture.h"
#include <stdio.h>
#include <stdexcept>
#include <limits>
namespace {
unsigned checks=0;
void Require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
bool Near(float a,float b,float eps=.0001f){return fabsf(a-b)<eps;}
}
int main(){try{
    using namespace kich_r4_lighting;
    const Matrix vp=Multiply(StableView(Vec3{3,2,-5},Vec3{0,0,1}),Projection(1.1f,.1f,200));Matrix inverse;
    Require(Inverse(vp,inverse),"perspective inverse exists");const Matrix unit=Multiply(vp,inverse);
    for(unsigned i=0;i<16;++i)Require(Near(unit.m[i],i%5==0?1.f:0.f),"row-vector projection inverse roundtrip");
    const float source[3]={4,3,12};float clip[4],restored[4];kich_r4_capture::Transform(source,vp,clip);
    float ndc[3]={clip[0]/clip[3],clip[1]/clip[3],clip[2]/clip[3]};kich_r4_capture::Transform(ndc,inverse,restored);
    for(unsigned i=0;i<3;++i)Require(Near(restored[i]/restored[3],source[i],.001f),"depth/world position reconstruction");
    Matrix singular={};Require(!Inverse(singular,inverse),"singular inverse rejected");
    Matrix scale=Identity();scale.m[0]=2;scale.m[5]=3;scale.m[10]=4;const Matrix normal=NormalMatrix(scale);
    Require(Near(normal.m[0],.5f) && Near(normal.m[5],1.f/3) && Near(normal.m[10],.25f),"normal uses inverse transpose");
    for(unsigned a=0;a<3;++a){float values[3]={};values[a]=1;
        const auto n=kich_r4_capture::UnpackNormal(kich_r4_capture::PackNormal(values[0],values[1],values[2]));
        Require(Near(a==0?n.x:a==1?n.y:n.z,1,.001f),"packed source normals retain unit axes");}
    const float camera[3]={5,4,8},direction[3]={0,-1,0};const Matrix sun=SunShadow(camera,direction);
    kich_r4_capture::Transform(camera,sun,clip);Require(Near(clip[0],0) && Near(clip[1],0) && Near(clip[2],.5f),"vertical sun view stable and centred");
    {
        fixture::Bytes vertices,geometry;fixture::U32(vertices,1);fixture::Element(vertices,0,2,0);fixture::Element(vertices,12,4,3);fixture::Element(vertices,16,1,5);fixture::End(vertices);fixture::U32(vertices,1);
        fixture::Float(vertices,1);fixture::Float(vertices,2);fixture::Float(vertices,3);fixture::U32(vertices,0x4080ff80);fixture::Float(vertices,.3f);fixture::Float(vertices,.7f);fixture::Chunk(geometry,9,vertices);
        kich_r4_level::Scene parsed;kich_r4_level::Vertices({geometry.data(),static_cast<uint32_t>(geometry.size())},parsed);const auto& v=parsed.vertices[0].vertices[0];
        Require(Near(v.normal[0],128*(2.f/255)-1) && Near(v.normal[1],1) && Near(v.normal[2],128*(2.f/255)-1),"static normal decodes D3DCOLOR BGRA memory order");
        Require(Near(v.hemi,64.f/255) && Near(v.uv[0],.3f) && Near(v.uv[1],.7f),"static hemi preserved independently of UVs");
    }
    {
        fixture::Bytes raw=capture_fixture::Model(2);const auto source=kich_r4_level::Chunk({raw.data(),static_cast<uint32_t>(raw.size())},3);
        const unsigned start=static_cast<unsigned>(source.p-raw.data());float one=1;uint32_t bits;memcpy(&bits,&one,4);
        for(unsigned i=0;i<3;++i)fixture::Set(raw,start+8+i*64+16,bits);
        const auto mesh=kich_r4_capture::Decode({raw.data(),static_cast<uint32_t>(raw.size())});kich_r4_capture::Batch b;b.Begin(640,480,1);
        Matrix bones[2]={RotationY(.5f),RotationY(-.7f)},world=Identity();world.m[0]=2;world.m[5]=3;
        for(unsigned mode=1;mode<=2;++mode){b.Begin(640,480,1);kich_r4_capture::Style style;b.Model(reinterpret_cast<void*>(1),*mesh,world,Identity(),bones,2,3,3,1,style,mode);
            const float w=kich_r4_capture::SkinWeight(.25f,mode);const Vec3 a=Direction(Vec3{1,0,0},bones[0]),c=Direction(Vec3{1,0,0},bones[1]);
            const Vec3 expected=Normalize(Direction(Vec3{a.x*(1-w)+c.x*w,a.y*(1-w)+c.y*w,a.z*(1-w)+c.z*w},NormalMatrix(world)));
            Require(Near(b.vertices[0].normal[0],expected.x,.003f) && Near(b.vertices[0].normal[2],expected.z,.003f),"posed normals use actual software/hardware weights then world inverse transpose");
            const auto& v=b.vertices[0];Require(Near(v.world[0],v.p[0]) && Near(v.world[1],v.p[1]) && Near(v.world[2],v.p[2]),"identity camera world positions match posed clip coordinates");}
    }
    KichR4LocalLight light={};light.type=1;light.range=20;light.color[0]=1;
    Require(ValidLight(light),"valid point light");
    const Vec3 axes[6]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for(unsigned face=0;face<6;++face){const float p[3]={axes[face].x*5,axes[face].y*5,axes[face].z*5};kich_r4_capture::Transform(p,LocalShadow(light,face),clip);
        Require(fabsf(clip[0])<.001f && fabsf(clip[1])<.001f && clip[2]/clip[3]>0 && clip[2]/clip[3]<1,"each point-light cube face points to correct axis");}
    light.type=2;light.direction[2]=1;light.cone=1;Require(ValidLight(light),"valid spot snapshot");
    light.range=0;Require(!ValidLight(light),"invalid light range rejected");light.range=20;
    kich_r4_capture::State state;light.type=1;state.Light(reinterpret_cast<void*>(1),light,true);
    light.type=2;state.Light(reinterpret_cast<void*>(2),light,true);
    KichR4LightingFrame lighting={};state.SelectLights(camera,lighting);Require(lighting.count==2,"active point and spot snapshots selected");
    state.Light(reinterpret_cast<void*>(1),light,false);Require(state.lights.size()==1,"deactivation removes snapshot");
    for(unsigned i=3;i<15;++i){light.type=1;light.position[0]=static_cast<float>(i);state.Light(reinterpret_cast<void*>(static_cast<uintptr_t>(i)),light,true);}
    state.SelectLights(camera,lighting);Require(lighting.count==13,"all active local lights reach GPU planner without top8 cutoff");
    KichR4PassDesc pass={};pass.struct_size=sizeof(pass);pass.width=960;pass.height=540;pass.color_count=1;pass.depth_value=1;
    Require(kich_r4::ValidPass(pass),"backbuffer pass supported");pass.color_count=2;Require(!kich_r4::ValidPass(pass),"null MRT handles rejected");
    pass.colors[0]=1;pass.colors[1]=2;Require(kich_r4::ValidPass(pass),"two color attachments valid");
    pass.depth=2;Require(!kich_r4::ValidPass(pass),"color/depth alias rejected");pass.depth=3;
    pass.colors[1]=1;Require(!kich_r4::ValidPass(pass),"duplicate MRT rejected");pass.colors[1]=2;
    pass.color[0]=std::numeric_limits<float>::quiet_NaN();Require(!kich_r4::ValidPass(pass),"NaN clear rejected");
    printf("B4.4 matrix/depth/normal/light/pass rules: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
