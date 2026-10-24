#ifndef KICH_R4_VISIBILITY_H
#define KICH_R4_VISIBILITY_H
#include "KichR4LightingMath.h"
#include <algorithm>
#include <cmath>
namespace kich_r4_visibility {
using kich_r4_scene::Matrix;
struct Bounds {float min[3],max[3];bool valid;Bounds():valid(false){for(unsigned i=0;i<3;++i)min[i]=max[i]=0;}};
inline bool Visible(const float* low,const float* high,const Matrix& vp){
    if(!low || !high)return true;
    for(unsigned a=0;a<3;++a)if(!kich_r4_lighting::Finite(low[a]) || !kich_r4_lighting::Finite(high[a]) || low[a]>high[a])return true;
    for(unsigned a=0;a<16;++a)if(!kich_r4_lighting::Finite(vp.m[a]))return true;
    unsigned outside[6]={};
    for(unsigned corner=0;corner<8;++corner){const float p[3]={corner&1?high[0]:low[0],corner&2?high[1]:low[1],corner&4?high[2]:low[2]};float c[4];
        for(unsigned a=0;a<4;++a)c[a]=p[0]*vp.m[a]+p[1]*vp.m[4+a]+p[2]*vp.m[8+a]+vp.m[12+a];
        for(unsigned a=0;a<4;++a)if(!kich_r4_lighting::Finite(c[a]))return true;
        /* D3D clip interval: -w<=x,y<=w and 0<=z<=w. Padding avoids
         * rejecting a caster exactly on a shadow/camera plane. */
        const float e=1.e-4f*(1.f+std::fabs(c[3]));
        outside[0]+=c[0]<-c[3]-e;outside[1]+=c[0]>c[3]+e;
        outside[2]+=c[1]<-c[3]-e;outside[3]+=c[1]>c[3]+e;
        outside[4]+=c[2]<-e;outside[5]+=c[2]>c[3]+e;}
    for(unsigned plane=0;plane<6;++plane)if(outside[plane]==8)return false;
    return true;
}
inline bool Visible(const Bounds& b,const Matrix& vp){return !b.valid || Visible(b.min,b.max,vp);}
inline bool VisibleSphere(const float* center,float radius,const Matrix& vp){
    if(!center || !kich_r4_lighting::Finite(radius) || radius<0)return true;
    for(unsigned a=0;a<3;++a)if(!kich_r4_lighting::Finite(center[a]))return true;
    for(unsigned a=0;a<16;++a)if(!kich_r4_lighting::Finite(vp.m[a]))return true;
    // Row-vector D3D clip planes: columns w +/- x/y, z, and w - z.
    for(unsigned plane=0;plane<6;++plane){float p[4];
        const unsigned column=plane<4?plane/2:2;const float sign=(plane&1)?-1.f:1.f;
        for(unsigned a=0;a<4;++a)p[a]=plane==4?vp.m[a*4+2]:vp.m[a*4+3]+sign*vp.m[a*4+column];
        const float length=sqrtf(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
        if(length<1.e-12f)continue; // malformed/degenerate plane cannot reject
        const float distance=p[0]*center[0]+p[1]*center[1]+p[2]*center[2]+p[3];
        if(distance<-(radius+1.e-4f)*length)return false;
    }
    return true;
}
// Receiver frustum extruded infinitely toward the sun, as in the R2 caster
// model. This is an ADDITIONAL caster test: keep the existing cascade frustum.
// In particular an object outside the camera may still shadow a visible
// receiver, and must not be tested against the camera frustum directly.
struct SunCasterVolume {
    double planes[16][4];unsigned count;bool valid;
    SunCasterVolume():count(0),valid(false){}
    bool Visible(const float* low,const float* high)const {
        if(!valid || !low || !high)return true;
        for(unsigned a=0;a<3;++a)if(!kich_r4_lighting::Finite(low[a]) || !kich_r4_lighting::Finite(high[a]) || low[a]>high[a])return true;
        for(unsigned i=0;i<count;++i){const double* p=planes[i];double maximum=p[3];
            for(unsigned a=0;a<3;++a)maximum+=p[a]*(p[a]>=0?high[a]:low[a]);
            if(maximum<0)return false;
        }
        return true;
    }
    bool VisibleSphere(const float* center,float radius)const {
        if(!valid || !center || !kich_r4_lighting::Finite(radius) || radius<0)return true;
        for(unsigned a=0;a<3;++a)if(!kich_r4_lighting::Finite(center[a]))return true;
        for(unsigned i=0;i<count;++i){const double* p=planes[i];double distance=p[3];
            for(unsigned a=0;a<3;++a)distance+=p[a]*center[a];
            if(distance < -double(radius))return false;
        }
        return true;
    }
};
inline SunCasterVolume BuildSunCasterVolume(const Matrix& camera_vp,const float* sun_direction){
    SunCasterVolume out;if(!sun_direction)return out;
    double upstream[3],length=0;
    for(unsigned a=0;a<3;++a){if(!kich_r4_lighting::Finite(sun_direction[a]))return out;upstream[a]=-double(sun_direction[a]);length+=upstream[a]*upstream[a];}
    if(length<1.e-12)return out;
    length=std::sqrt(length);for(unsigned a=0;a<3;++a)upstream[a]/=length;
    // Invert in double precision: float inverse loses useful far-plane
    // precision when near/far distances differ by several orders of magnitude.
    double row[4][8]={};
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){if(!kich_r4_lighting::Finite(camera_vp.m[i*4+j]))return out;
        row[i][j]=camera_vp.m[i*4+j];row[i][j+4]=i==j?1:0;}
    for(unsigned c=0;c<4;++c){unsigned pivot=c;for(unsigned r=c+1;r<4;++r)if(std::fabs(row[r][c])>std::fabs(row[pivot][c]))pivot=r;
        if(std::fabs(row[pivot][c])<1.e-12)return out;
        for(unsigned j=0;j<8;++j)std::swap(row[pivot][j],row[c][j]);
        const double divisor=row[c][c];for(unsigned j=0;j<8;++j)row[c][j]/=divisor;
        for(unsigned r=0;r<4;++r)if(r!=c){const double factor=row[r][c];for(unsigned j=0;j<8;++j)row[r][j]-=factor*row[c][j];}}
    double corners[8][3],magnitude=1;
    for(unsigned i=0;i<8;++i){const double clip[4]={i&1?1.:-1.,i&2?1.:-1.,i&4?1.:0.,1.};double point[4]={};
        for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b)point[a]+=clip[b]*row[b][a+4];
        // A projective pole or reversed/infinite projection cannot prove a
        // rejection. The ordinary cascade still provides its existing bound.
        if(!std::isfinite(point[3]) || point[3]<=1.e-12)return SunCasterVolume();
        for(unsigned a=0;a<3;++a){corners[i][a]=point[a]/point[3];if(!std::isfinite(corners[i][a]) || std::fabs(corners[i][a])>1.e8)return SunCasterVolume();
            magnitude=(std::max)(magnitude,std::fabs(corners[i][a]));}}
    // Covers float world bounds, matrix quantization and boundary receivers.
    const double padding=.1+1.e-6*magnitude;
    auto add_plane=[&](double x,double y,double z){const double n=std::sqrt(x*x+y*y+z*z);if(n<1.e-12)return;
        double* p=out.planes[out.count];p[0]=x/n;p[1]=y/n;p[2]=z/n;
        double minimum=1.e300;for(unsigned c=0;c<8;++c){double d=0;for(unsigned a=0;a<3;++a)d+=p[a]*corners[c][a];minimum=(std::min)(minimum,d);}
        p[3]=-minimum+padding;++out.count;};
    // Retain only camera faces that remain satisfied while moving upstream.
    // Parallel faces are supplied by the silhouette hull below.
    for(unsigned plane=0;plane<6;++plane){double p[3];const unsigned column=plane<4?plane/2:2;const double sign=plane&1?-1.:1.;
        for(unsigned a=0;a<3;++a)p[a]=plane==4?camera_vp.m[a*4+2]:camera_vp.m[a*4+3]+sign*camera_vp.m[a*4+column];
        const double dot=p[0]*upstream[0]+p[1]*upstream[1]+p[2]*upstream[2];
        if(dot>1.e-10)add_plane(p[0],p[1],p[2]);}
    // The convex silhouette of the eight receiver corners defines the side
    // planes, all parallel to the sunlight. Build its 2D hull (at most 8 edges).
    unsigned axis=0;for(unsigned a=1;a<3;++a)if(std::fabs(upstream[a])<std::fabs(upstream[axis]))axis=a;
    double right[3]={},up[3];right[(axis+1)%3]=upstream[(axis+2)%3];right[(axis+2)%3]=-upstream[(axis+1)%3];
    length=std::sqrt(right[0]*right[0]+right[1]*right[1]+right[2]*right[2]);
    for(unsigned a=0;a<3;++a)right[a]/=length;
    for(unsigned a=0;a<3;++a)up[a]=upstream[(a+1)%3]*right[(a+2)%3]-upstream[(a+2)%3]*right[(a+1)%3];
    struct Point {double x,y;};Point points[8],hull[16];
    for(unsigned i=0;i<8;++i){points[i].x=points[i].y=0;for(unsigned a=0;a<3;++a){const double d=corners[i][a]-corners[0][a];points[i].x+=right[a]*d;points[i].y+=up[a]*d;}}
    std::sort(points,points+8,[](const Point& a,const Point& b){return a.x<b.x || (a.x==b.x && a.y<b.y);});
    auto cross=[](const Point& a,const Point& b,const Point& c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);};
    unsigned n=0;for(unsigned i=0;i<8;++i)if(!n || points[i].x!=points[n-1].x || points[i].y!=points[n-1].y)points[n++]=points[i];
    if(n<3)return SunCasterVolume();
    unsigned size=0;for(unsigned i=0;i<n;++i){while(size>=2 && cross(hull[size-2],hull[size-1],points[i])<=0)--size;hull[size++]=points[i];}
    const unsigned lower=size+1;for(int i=int(n)-2;i>=0;--i){while(size>=lower && cross(hull[size-2],hull[size-1],points[i])<=0)--size;hull[size++]=points[i];}
    if(size<4)return SunCasterVolume();
    --size;
    for(unsigned i=0;i<size;++i){const Point& a=hull[i];const Point& b=hull[(i+1)%size];const double x=-(b.y-a.y),y=b.x-a.x;
        add_plane(x*right[0]+y*up[0],x*right[1]+y*up[1],x*right[2]+y*up[2]);}
    out.valid=out.count>0;return out;
}
// Conservative normalized viewport rectangle. Used to bound light shading;
// its corners enclose the sphere, so it never trims valid illumination.
struct ScreenRect {
    float left,top,right,bottom;
    ScreenRect():left(0),top(0),right(1),bottom(1){}
};
inline ScreenRect LightScreenRect(const KichR4LocalLight& light,const Matrix& vp){
    ScreenRect full;
    if(!kich_r4_lighting::Finite(light.range) || light.range<0)return full;
    for(unsigned a=0;a<3;++a)if(!kich_r4_lighting::Finite(light.position[a]))return full;
    for(unsigned a=0;a<16;++a)if(!kich_r4_lighting::Finite(vp.m[a]))return full;
    float low[2]={1.e20f,1.e20f},high[2]={-1.e20f,-1.e20f};
    for(unsigned corner=0;corner<8;++corner){float p[3],clip[4];
        for(unsigned a=0;a<3;++a)p[a]=light.position[a]+((corner&(1u<<a))?light.range:-light.range);
        for(unsigned a=0;a<4;++a)clip[a]=p[0]*vp.m[a]+p[1]*vp.m[4+a]+p[2]*vp.m[8+a]+vp.m[12+a];
        for(unsigned a=0;a<4;++a)if(!kich_r4_lighting::Finite(clip[a]))return full;
        // Projecting a box across the near plane is unbounded. Fall open for
        // camera-inside lights and behind-camera/invalid caller descriptors.
        if(clip[3]<=1.e-5f || clip[2]<=1.e-5f)return full;
        const float uv[2]={.5f+.5f*clip[0]/clip[3],.5f-.5f*clip[1]/clip[3]};
        for(unsigned a=0;a<2;++a){low[a]=std::min(low[a],uv[a]);high[a]=std::max(high[a],uv[a]);}
    }
    ScreenRect result;const float pad=1.e-4f;
    result.left=std::max(0.f,std::min(1.f,low[0]-pad));result.right=std::max(0.f,std::min(1.f,high[0]+pad));
    result.top=std::max(0.f,std::min(1.f,low[1]-pad));result.bottom=std::max(0.f,std::min(1.f,high[1]+pad));
    return result;
}
// Dynamic shadowed sun only: R2 static and disabled sun render no sun maps.
inline bool SunEnabled(const KichR4LightingFrame& lighting){
    if(lighting.sun_mode==KICH_R4_SUN_STATIC || lighting.sun_mode==KICH_R4_SUN_DISABLED)return false;
    const kich_r4_scene::Vec3 dir=kich_r4_scene::Normalize(kich_r4_scene::Vec3{lighting.sun_direction[0],lighting.sun_direction[1],lighting.sun_direction[2]});
    return kich_r4_scene::Dot(dir,dir)>.5f && (lighting.sun_color[0]>0 || lighting.sun_color[1]>0 || lighting.sun_color[2]>0);
}
inline void SelectShadowLights(const KichR4LightingFrame& lighting,bool native,std::vector<uint8_t>& selected){
    selected.assign(lighting.count,0);
    for(unsigned i=0;i<lighting.count && i<lighting.lights.size();++i)
        selected[i]=(!native || lighting.CastsShadow(i))?1:0;
}
}
#endif
