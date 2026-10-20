#ifndef KICH_R4_TEST_SCENE_H
#define KICH_R4_TEST_SCENE_H
/* Portable B4.1 scene data/math. Row-major matrices, row vectors, LH camera,
 * depth 0..1; no engine/D3DX dependency. Used by the real EXE test runner. */
#include "KichR4Geometry.h"
#include <math.h>
#include <string.h>
namespace kich_r4_scene {
struct Vec3 {float x,y,z;};
struct Matrix {float m[16];};
struct Vertex {float position[3],color[4];};
struct Constants {Matrix mvp;float tint[4];};
struct Mesh {Vertex vertices[27];uint32_t indices[39];};
struct Controls {uint32_t mode,cull,depth;bool paused;float angle,yaw,pitch,distance;};
static_assert(sizeof(Vertex)==28,"vertex stride");
static_assert(sizeof(Constants)==80,"SM5 cbuffer layout");
inline Matrix Identity(){Matrix a={};for(unsigned i=0;i<4;++i)a.m[i*4+i]=1.f;return a;}
inline Matrix Multiply(const Matrix& a,const Matrix& b){Matrix c={};
    for(unsigned i=0;i<4;++i){for(unsigned j=0;j<4;++j){for(unsigned k=0;k<4;++k){
        c.m[i*4+j]+=a.m[i*4+k]*b.m[k*4+j];}}}
    return c;
}
inline Matrix Translation(float x,float y,float z){Matrix a=Identity();a.m[12]=x;a.m[13]=y;a.m[14]=z;return a;}
inline Matrix Scale(float x){Matrix a=Identity();a.m[0]=a.m[5]=a.m[10]=x;return a;}
inline Matrix RotationY(float angle){Matrix a=Identity();const float c=cosf(angle),s=sinf(angle);
    a.m[0]=c;a.m[2]=-s;a.m[8]=s;a.m[10]=c;return a;}
inline Matrix RotationX(float angle){Matrix a=Identity();const float c=cosf(angle),s=sinf(angle);
    a.m[5]=c;a.m[6]=s;a.m[9]=-s;a.m[10]=c;return a;}
inline float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 Cross(Vec3 a,Vec3 b){const Vec3 c={a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};return c;}
inline Vec3 Normalize(Vec3 a){const float n=sqrtf(Dot(a,a));if(n>0.f){a.x/=n;a.y/=n;a.z/=n;}return a;}
inline Matrix LookAt(Vec3 eye,Vec3 target){const Vec3 delta={target.x-eye.x,target.y-eye.y,target.z-eye.z};
    const Vec3 forward=Normalize(delta),up_hint={0,1,0},right=Normalize(Cross(up_hint,forward)),up=Cross(forward,right);
    const Matrix a={{right.x,up.x,forward.x,0,right.y,up.y,forward.y,0,right.z,up.z,forward.z,0,
        -Dot(right,eye),-Dot(up,eye),-Dot(forward,eye),1}};return a;}
inline Matrix Perspective(float aspect){const float near_z=.1f,far_z=100.f,y=1.f/tanf(.55f),range=far_z/(far_z-near_z);
    Matrix a={};a.m[0]=y/aspect;a.m[5]=y;a.m[10]=range;a.m[11]=1.f;a.m[14]=-near_z*range;return a;}
inline void ResetCamera(Controls& c){c.yaw=-.25f;c.pitch=.18f;c.distance=6.2f;}
inline Controls Defaults(){Controls c={};c.mode=1;c.cull=KICH_R4_CULL_BACK;c.depth=KICH_R4_DEPTH_ON;ResetCamera(c);return c;}
enum Action {Triangle,Cubes,Depth,Cull,Pause,CameraReset,Left,Right,Up,Down,Closer,Farther};
inline void Apply(Controls& c,Action action){switch(action){
    case Triangle:c.mode=1;break;case Cubes:c.mode=2;break;case Depth:c.depth=1-c.depth;break;
    case Cull:c.cull=(c.cull+1)%3;break;case Pause:c.paused=!c.paused;break;case CameraReset:ResetCamera(c);break;
    case Left:c.yaw-=.12f;break;case Right:c.yaw+=.12f;break;case Up:c.pitch+=.12f;break;case Down:c.pitch-=.12f;break;
    case Closer:c.distance-=.4f;break;case Farther:c.distance+=.4f;break;}
    if(c.pitch>1.1f)c.pitch=1.1f;
    if(c.pitch< -1.1f)c.pitch=-1.1f;
    if(c.distance<3.f)c.distance=3.f;
    if(c.distance>12.f)c.distance=12.f;
    c.yaw=fmodf(c.yaw,6.2831853f);
}
inline Mesh BuildMesh(){Mesh mesh={};
    const Vertex triangle[]={{{-.75f,-.65f,.5f},{1,0,0,1}},{{0,.7f,.5f},{0,1,0,1}},{{.75f,-.65f,.5f},{0,0,1,1}}};
    memcpy(mesh.vertices,triangle,sizeof(triangle));mesh.indices[0]=0;mesh.indices[1]=1;mesh.indices[2]=2;
    const float faces[6][4][3]={
        {{-1,-1,-1},{-1,1,-1},{1,1,-1},{1,-1,-1}},
        {{1,-1,1},{1,1,1},{-1,1,1},{-1,-1,1}},
        {{-1,-1,1},{-1,1,1},{-1,1,-1},{-1,-1,-1}},
        {{1,-1,-1},{1,1,-1},{1,1,1},{1,-1,1}},
        {{-1,1,-1},{-1,1,1},{1,1,1},{1,1,-1}},
        {{-1,-1,1},{-1,-1,-1},{1,-1,-1},{1,-1,1}}};
    const float colors[6][4]={{1,.25f,.18f,1},{.2f,.9f,.35f,1},{.2f,.4f,1,1},
        {1,.8f,.15f,1},{.8f,.2f,1,1},{.1f,.9f,.9f,1}};
    const uint32_t indices[]={0,1,2,0,2,3};
    for(unsigned face=0;face<6;++face){for(unsigned v=0;v<4;++v){Vertex& out=mesh.vertices[3+face*4+v];
        memcpy(out.position,faces[face][v],sizeof(out.position));memcpy(out.color,colors[face],sizeof(out.color));}
        for(unsigned i=0;i<6;++i)mesh.indices[3+face*6+i]=3+face*4+indices[i];}
    return mesh;
}
inline Constants ObjectConstants(const Controls& c,uint32_t w,uint32_t h,unsigned object){Constants out={};
    const float aspect=static_cast<float>(w)/static_cast<float>(h);out.mvp=Identity();
    out.tint[0]=out.tint[1]=out.tint[2]=out.tint[3]=1.f;
    if(c.mode==1){out.mvp.m[0]=aspect>=1.f?1.f/aspect:1.f;out.mvp.m[5]=aspect>=1.f?1.f:aspect;return out;}
    const Vec3 eye={sinf(c.yaw)*cosf(c.pitch)*c.distance,sinf(c.pitch)*c.distance,-cosf(c.yaw)*cosf(c.pitch)*c.distance};
    const Vec3 target={0,0,.4f};
    Matrix world=Multiply(RotationX(c.angle*.45f),RotationY(object?-c.angle*.7f:c.angle));
    world=Multiply(world,Scale(object?.85f:.8f));
    world=Multiply(world,Translation(object?.55f:-.5f,object?.15f:0.f,object?1.6f:0.f));
    out.mvp=Multiply(Multiply(world,LookAt(eye,target)),Perspective(aspect));
    if(object){out.tint[0]=.45f;out.tint[1]=.75f;out.tint[2]=1.f;}
    return out;
}
inline const char* VertexShader(){return
    "cbuffer Frame : register(b0) { row_major float4x4 mvp; float4 tint; };"
    "struct Input {float3 position:POSITION; float4 color:COLOR0;};"
    "struct Output {float4 position:SV_Position; float4 color:COLOR0;};"
    "Output main(Input v){Output o;o.position=mul(float4(v.position,1),mvp);o.color=v.color*tint;return o;}";}
inline const char* PixelShader(){return
    "float4 main(float4 position:SV_Position,float4 color:COLOR0):SV_Target {return color;}";}
}
#endif
