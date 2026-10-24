#ifndef KICH_R4_LEVEL_VISIBILITY_H
#define KICH_R4_LEVEL_VISIBILITY_H
#include "KichR4LevelCamera.h"
namespace kich_r4_level {
// The same result is used for texture residency and draw submission. A caller
// must query each light from its own origin; a camera mask is not a caster mask.
struct StaticVisibility {
    std::vector<unsigned char> draws;
    uint32_t sectors,portal_tests,sector_rejected,frustum_rejected;bool portal_used;
    StaticVisibility():sectors(0),portal_tests(0),sector_rejected(0),frustum_rejected(0),portal_used(false){}
};
inline Draw WindBounds(const Scene& scene,const Draw& draw,float wind_x,float wind_z){
    Draw bounds=draw;
    if(draw.tree && draw.material<scene.materials.size() && scene.materials[draw.material].descriptor.tree_wave && draw.vb<scene.vertices.size()){
        const float height=(std::max)(fabsf(draw.low.y-draw.world.m[13]),fabsf(draw.high.y-draw.world.m[13]));
        const float fraction=scene.vertices[draw.vb].tree_fraction_max;
        const float x=fabsf(wind_x)*height*fraction,z=fabsf(wind_z)*height*fraction;
        bounds.low.x-=x;bounds.high.x+=x;bounds.low.z-=z;bounds.high.z+=z;
    }
    return bounds;
}
inline bool PortalVisible(const Portal& portal,const Matrix& vp){
    // Ignore the near plane: a doorway between the eye/light and its near
    // plane can still expose geometry farther into the adjacent sector.
    bool outside[5]={true,true,true,true,true};
    for(size_t i=0;i<portal.vertices.size();++i){const Vec3& p=portal.vertices[i];const float v[4]={p.x,p.y,p.z,1};float c[4]={};
        for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row)c[col]+=v[row]*vp.m[row*4+col];
        const float epsilon=.0001f*(1.f+fabsf(c[3]));
        outside[0]=outside[0] && c[0]<-c[3]-epsilon;outside[1]=outside[1] && c[0]>c[3]+epsilon;
        outside[2]=outside[2] && c[1]<-c[3]-epsilon;outside[3]=outside[3] && c[1]>c[3]+epsilon;
        outside[4]=outside[4] && c[2]>c[3]+epsilon;
    }
    for(unsigned i=0;i<5;++i)if(outside[i])return false;
    return true;
}
inline void SelectStatic(const Scene& scene,const Matrix& vp,uint32_t seed_sector,float wind_x,float wind_z,StaticVisibility& result){
    result=StaticVisibility();result.draws.assign(scene.draws.size(),1);
    const Topology& t=scene.topology;
    if(t.valid && seed_sector<t.sectors.size() && scene.sector_owned.size()==scene.draws.size()){
        result.portal_used=true;
        for(size_t i=0;i<result.draws.size();++i)result.draws[i]=scene.sector_owned[i]?0:1;
        std::vector<unsigned char> reached(t.sectors.size(),0),tested(t.portals.size(),0),visible(t.portals.size(),0);
        std::vector<uint32_t> pending(1,seed_sector);reached[seed_sector]=1;
        // Every edge uses the ORIGINAL view. No first-path portal narrowing:
        // a cycle or second wider entrance cannot hide previously missed draws.
        for(size_t at=0;at<pending.size();++at){const uint32_t current=pending[at];const Sector& sector=t.sectors[current];++result.sectors;
            for(size_t i=0;i<sector.draws.size();++i)result.draws[sector.draws[i]]=1;
            for(size_t i=0;i<sector.portals.size();++i){const uint32_t id=sector.portals[i];const Portal& portal=t.portals[id];
                if(!tested[id]){tested[id]=1;++result.portal_tests;visible[id]=PortalVisible(portal,vp)?1:0;}
                if(!visible[id])continue;
                const uint32_t next=portal.front==current?portal.back:portal.front;
                if(!reached[next]){reached[next]=1;pending.push_back(next);}
            }
        }
    }
    for(size_t i=0;i<scene.draws.size();++i){if(!result.draws[i]){++result.sector_rejected;continue;}
        if(!Visible(WindBounds(scene,scene.draws[i],wind_x,wind_z),vp)){result.draws[i]=0;++result.frustum_rejected;}}
}
inline bool PortalTriangleRay(Vec3 origin,Vec3 direction,Vec3 a,Vec3 b,Vec3 c,float& range){
    const Vec3 ab={b.x-a.x,b.y-a.y,b.z-a.z},ac={c.x-a.x,c.y-a.y,c.z-a.z};
    const Vec3 cross=kich_r4_scene::Cross(direction,ac);const float determinant=kich_r4_scene::Dot(ab,cross);
    if(fabsf(determinant)<1.e-8f)return false;
    const Vec3 delta={origin.x-a.x,origin.y-a.y,origin.z-a.z};const float inverse=1.f/determinant;
    const float u=kich_r4_scene::Dot(delta,cross)*inverse;const Vec3 q=kich_r4_scene::Cross(delta,ab);
    const float v=kich_r4_scene::Dot(direction,q)*inverse;
    if(u<-.00001f || v<-.00001f || u+v>1.00001f)return false;
    const float hit=kich_r4_scene::Dot(ac,q)*inverse;
    if(hit<0 || hit>range)return false;
    range=hit;return true;
}
inline uint32_t PortalRaySector(const Topology& topology,Vec3 origin,Vec3 direction,float& distance){
    if(!topology.valid)return UINT32_MAX;
    uint32_t result=UINT32_MAX;
    for(size_t i=0;i<topology.portals.size();++i){const Portal& p=topology.portals[i];
        const float side=kich_r4_scene::Dot(p.normal,origin)+p.distance;
        if(fabsf(side)<.05f){Vec3 low=p.vertices[0],high=low;
            for(size_t j=1;j<p.vertices.size();++j)for(unsigned a=0;a<3;++a){Vec3 v=p.vertices[j];Axis(low,a)=(std::min)(Axis(low,a),Axis(v,a));Axis(high,a)=(std::max)(Axis(high,a),Axis(v,a));}
            if(origin.x>=low.x-.05f && origin.x<=high.x+.05f && origin.y>=low.y-.05f && origin.y<=high.y+.05f && origin.z>=low.z-.05f && origin.z<=high.z+.05f){
                // The origin straddles a doorway. Signal ambiguity explicitly;
                // the adapter must not replace this with a farther CFORM hit.
                distance=0;return UINT32_MAX;
            }
        }
        for(size_t j=2;j<p.vertices.size();++j)if(PortalTriangleRay(origin,direction,p.vertices[0],p.vertices[j-1],p.vertices[j],distance))result=side>0?p.front:p.back;
    }
    return result;
}
}
#endif
