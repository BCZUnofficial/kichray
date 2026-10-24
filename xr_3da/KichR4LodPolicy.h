#ifndef KICH_R4_LOD_POLICY_H
#define KICH_R4_LOD_POLICY_H
#include "KichR4TestScene.h"
#include <cmath>
#include <algorithm>
#include <stdint.h>
namespace kich_r4_lod {
inline bool Finite(double value){return std::isfinite(value);}
struct Parameters {double screen,start,end;};
// Same SSA and thresholds as R2 r2_R_calculate/calcLOD. Invalid settings fail
// toward full detail; they must never remove geometry or corrupt a SWI index.
inline Parameters CameraParameters(uint32_t width,uint32_t height,float fov,float bias,float start=256.f,float end=64.f){
    if(!(fov>0 && fov<180) || !Finite(fov))fov=90.f;
    if(!(bias>0) || !Finite(bias))bias=1.f;
    if(!(start>end && end>=0) || !Finite(start) || !Finite(end)){start=256.f;end=64.f;}
    const double factor=90.0/fov;
    const Parameters p={double(width)*height*factor*factor*(.0000001+bias),double(start)*start/9.0,double(end)*end/9.0};return p;
}
inline float Area(double area,double start,double end){
    if(!Finite(area) || !Finite(start) || !Finite(end) || !(start>end))return 1.f;
    const double fraction=(area-end)/(start-end);
    return static_cast<float>(std::sqrt((std::max)(0.0,(std::min)(1.0,fraction))));
}
inline float Geometry(float radius,double distance_squared,const Parameters& p){
    if(!(radius>=0) || !Finite(radius) || !(distance_squared>=0) || !Finite(distance_squared) || !(p.screen>0))return 1.f;
    return Area(double(radius)*p.screen/(distance_squared+.00001),p.start,p.end);
}
inline float Geometry(const float* low,const float* high,const float* camera,const Parameters& p){
    double radius_squared=0,distance_squared=0;
    for(unsigned i=0;i<3;++i){if(!Finite(low[i]) || !Finite(high[i]) || high[i]<low[i] || !Finite(camera[i]))return 1.f;
        const double extent=(double(high[i])-low[i])*.5,delta=(double(high[i])+low[i])*.5-camera[i];
        radius_squared+=extent*extent;distance_squared+=delta*delta;}
    return Geometry(static_cast<float>(std::sqrt(radius_squared)),distance_squared,p);
}
// Upper bound on projected AABB extent. Crossing the eye/near plane keeps full
// detail; this also protects local shadows of casters close to the source.
inline float Projected(const float* low,const float* high,const kich_r4_scene::Matrix& vp,uint32_t width,uint32_t height,float start=256.f,float end=64.f){
    if(!width || !height)return 1.f;
    for(unsigned i=0;i<3;++i)if(!Finite(low[i]) || !Finite(high[i]) || high[i]<low[i])return 1.f;
    double min_x=1.e300,min_y=1.e300,max_x=-1.e300,max_y=-1.e300;
    for(unsigned corner=0;corner<8;++corner){const double point[4]={corner&1?high[0]:low[0],corner&2?high[1]:low[1],corner&4?high[2]:low[2],1};double clip[4]={};
        for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)clip[c]+=point[r]*vp.m[r*4+c];
        for(unsigned i=0;i<4;++i)if(!Finite(clip[i]))return 1.f;
        if(clip[3]<=.000001 || clip[2]<=0)return 1.f;
        const double x=clip[0]/clip[3],y=clip[1]/clip[3];
        min_x=(std::min)(min_x,x);max_x=(std::max)(max_x,x);min_y=(std::min)(min_y,y);max_y=(std::max)(max_y,y);}
    const double x=(max_x-min_x)*width*.25,y=(max_y-min_y)*height*.25;
    if(!(start>end && end>=0) || !Finite(start) || !Finite(end)){start=256.f;end=64.f;}
    return Area((std::max)(x*x,y*y),double(start)*start/9.0,double(end)*end/9.0);
}
inline uint32_t Window(float detail,uint32_t count){
    if(count<2 || !Finite(detail))return 0;
    const double clamped=(std::max)(0.0,(std::min)(1.0,double(detail)));
    return static_cast<uint32_t>((1.0-clamped)*(count-1)+.5);
}
}
#endif
