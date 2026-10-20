#ifndef KICH_R4_LEVEL_CAMERA_H
#define KICH_R4_LEVEL_CAMERA_H
#include "KichR4LevelData.h"
namespace kich_r4_level {
enum Move {Forward=1,Back=2,Left=4,Right=8,Up=16,Down=32,YawLeft=64,YawRight=128,PitchUp=256,PitchDown=512,Fast=1024,Slow=2048};
struct Camera {
    Vec3 position,center;float yaw,pitch,radius,speed,far_plane;
    Camera():yaw(0),pitch(0),radius(1),speed(2),far_plane(3000){position={0,0,0};center={0,0,0};}
    void Reset(Vec3 low,Vec3 high){center={(low.x+high.x)*.5f,(low.y+high.y)*.5f,(low.z+high.z)*.5f};
        const Vec3 extent={high.x-low.x,high.y-low.y,high.z-low.z};radius=std::max(1.f,sqrtf(kich_r4_scene::Dot(extent,extent))*.5f);
        position={center.x,center.y+radius*1.1f,center.z-radius*2.4f};yaw=0;pitch=-atan2f(1.1f,2.4f);
        speed=std::max(2.f,radius*.12f);far_plane=std::max(3000.f,radius*10.f);}
    Vec3 Direction()const{const Vec3 f={sinf(yaw)*cosf(pitch),sinf(pitch),cosf(yaw)*cosf(pitch)};return f;}
    bool Advance(uint32_t keys,float seconds){if(!(keys&1023u) || !(seconds>0.f) || !Finite(seconds))return false;seconds=std::min(seconds,.1f);
        const float rotation=seconds*1.2f;
        if(keys&YawLeft)yaw-=rotation;
        if(keys&YawRight)yaw+=rotation;
        if(keys&PitchUp)pitch+=rotation;
        if(keys&PitchDown)pitch-=rotation;
        yaw=fmodf(yaw,6.2831853f);pitch=std::max(-1.5f,std::min(1.5f,pitch));
        const Vec3 f=Direction(),right={cosf(yaw),0,-sinf(yaw)};Vec3 v={0,0,0};
        const float forward=((keys&Forward)?1.f:0.f)-((keys&Back)?1.f:0.f),side=((keys&Right)?1.f:0.f)-((keys&Left)?1.f:0.f);
        v.x=f.x*forward+right.x*side;v.y=f.y*forward+((keys&Up)?1.f:0.f)-((keys&Down)?1.f:0.f);v.z=f.z*forward+right.z*side;
        const float length=sqrtf(kich_r4_scene::Dot(v,v));if(length>1.f){v.x/=length;v.y/=length;v.z/=length;}
        const float distance=speed*seconds*((keys&Fast)?4.f:1.f)*((keys&Slow)?.2f:1.f);
        position.x+=v.x*distance;position.y+=v.y*distance;position.z+=v.z*distance;
        position.x=std::max(-1.e6f,std::min(1.e6f,position.x));position.y=std::max(-1.e6f,std::min(1.e6f,position.y));position.z=std::max(-1.e6f,std::min(1.e6f,position.z));
        return (keys&1023u)!=0;}
    Matrix ViewProjection(uint32_t w,uint32_t h)const{const Vec3 f=Direction(),target={position.x+f.x,position.y+f.y,position.z+f.z};
        Matrix projection=kich_r4_scene::Perspective(static_cast<float>(w)/static_cast<float>(h));
        const float near_plane=.5f;projection.m[10]=far_plane/(far_plane-near_plane);projection.m[14]=-near_plane*projection.m[10];
        return kich_r4_scene::Multiply(kich_r4_scene::LookAt(position,target),projection);}
};
inline bool Visible(const Draw& draw,const Matrix& view_projection){
    /* OGF header bounds are already world-space, including tree instances. */
    bool outside[6]={true,true,true,true,true,true};
    for(unsigned corner=0;corner<8;++corner){const float p[]={corner&1?draw.high.x:draw.low.x,corner&2?draw.high.y:draw.low.y,corner&4?draw.high.z:draw.low.z,1};float c[4]={};
        for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row)c[col]+=p[row]*view_projection.m[row*4+col];
        outside[0]=outside[0]&&(c[0]<-c[3]);outside[1]=outside[1]&&(c[0]>c[3]);outside[2]=outside[2]&&(c[1]<-c[3]);
        outside[3]=outside[3]&&(c[1]>c[3]);outside[4]=outside[4]&&(c[2]<0);outside[5]=outside[5]&&(c[2]>c[3]);
    }
    for(unsigned i=0;i<6;++i)if(outside[i])return false;
    return true;
}
}
#endif
