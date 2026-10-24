#ifndef KICH_R4_LEVEL_VISIBILITY_H
#define KICH_R4_LEVEL_VISIBILITY_H
#include "KichR4LevelCamera.h"
#include "KichR4Visibility.h"
#include <array>
#include <map>
#include <cstring>
namespace kich_r4_level {
// The same result is used for texture residency and draw submission. A caller
// must query each light from its own origin; a camera mask is not a caster mask.
struct StaticVisibility {
    std::vector<unsigned char> draws;
    std::vector<uint32_t> selected;
    uint32_t sectors,portal_tests,sector_rejected,frustum_rejected,bounds_tests,node_tests;bool portal_used;
    StaticVisibility():sectors(0),portal_tests(0),sector_rejected(0),frustum_rejected(0),bounds_tests(0),node_tests(0),portal_used(false){}
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
inline void SelectStaticSectors(const Scene& scene,const Matrix& vp,uint32_t seed_sector,StaticVisibility& result){
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
}
inline void SelectStatic(const Scene& scene,const Matrix& vp,uint32_t seed_sector,float wind_x,float wind_z,StaticVisibility& result){
    SelectStaticSectors(scene,vp,seed_sector,result);
    for(size_t i=0;i<scene.draws.size();++i){if(!result.draws[i]){++result.sector_rejected;continue;}
        ++result.bounds_tests;
        if(!Visible(WindBounds(scene,scene.draws[i],wind_x,wind_z),vp)){result.draws[i]=0;++result.frustum_rejected;}
        else result.selected.push_back(static_cast<uint32_t>(i));}
}

// Level topology is immutable within a level revision. Build its spatial index
// once; refit conservative wind bounds once per frame. Texture residency and
// submission then share exactly the same per-view result, including portals.
struct StaticFramePlan {
    struct Node {Draw bounds;uint32_t first,count,left,right;bool finite;};
    typedef std::array<uint32_t,18> Key;
    const Scene* scene=NULL;const Draw* storage=NULL;uint32_t revision=UINT32_MAX;
    std::vector<Draw> bounds;
    std::vector<uint32_t> order;
    std::vector<Node> nodes;
    std::map<Key,StaticVisibility> views;
    StaticVisibility uncached;
    kich_r4_visibility::SunCasterVolume sun_casters;
    float wind_x=0,wind_z=0;
    uint32_t queries=0,cache_hits=0;
    uint64_t bounds_tests=0,node_tests=0;
    static bool ValidBounds(const Draw& d){
        return Finite(d.low.x) && Finite(d.low.y) && Finite(d.low.z) && Finite(d.high.x) && Finite(d.high.y) && Finite(d.high.z) &&
            d.low.x<=d.high.x && d.low.y<=d.high.y && d.low.z<=d.high.z;
    }
    static void Include(Draw& a,const Draw& b){
        a.low.x=(std::min)(a.low.x,b.low.x);a.low.y=(std::min)(a.low.y,b.low.y);a.low.z=(std::min)(a.low.z,b.low.z);
        a.high.x=(std::max)(a.high.x,b.high.x);a.high.y=(std::max)(a.high.y,b.high.y);a.high.z=(std::max)(a.high.z,b.high.z);
    }
    uint32_t Build(uint32_t first,uint32_t count){
        const uint32_t id=static_cast<uint32_t>(nodes.size());Node node={};node.first=first;node.count=count;node.left=node.right=UINT32_MAX;
        nodes.push_back(node);
        if(count>8){
            Draw extent=bounds[order[first]];for(uint32_t i=1;i<count;++i)Include(extent,bounds[order[first+i]]);
            const float span[3]={extent.high.x-extent.low.x,extent.high.y-extent.low.y,extent.high.z-extent.low.z};
            unsigned axis=span[1]>span[0]?1:0;if(span[2]>span[axis])axis=2;
            const uint32_t middle=first+count/2;
            std::nth_element(order.begin()+first,order.begin()+middle,order.begin()+first+count,[&](uint32_t a,uint32_t b){
                // Invalid bounds remain conservative and must not violate the
                // strict weak ordering required by nth_element.
                const float x=Axis(bounds[a].low,axis)+Axis(bounds[a].high,axis),y=Axis(bounds[b].low,axis)+Axis(bounds[b].high,axis);
                const float cx=Finite(x)?x:0,cy=Finite(y)?y:0;return cx==cy?a<b:cx<cy;});
            const uint32_t left=Build(first,count/2),right=Build(middle,count-count/2);
            nodes[id].left=left;nodes[id].right=right;
        }
        return id;
    }
    void Begin(const Scene& value,uint32_t generation,float x,float z){
        const Draw* data=value.draws.empty()?NULL:&value.draws[0];
        const bool rebuild=scene!=&value || storage!=data || revision!=generation || bounds.size()!=value.draws.size();
        scene=&value;storage=data;revision=generation;wind_x=x;wind_z=z;views.clear();queries=cache_hits=0;bounds_tests=node_tests=0;
        bounds.resize(value.draws.size());for(size_t i=0;i<bounds.size();++i)bounds[i]=WindBounds(value,value.draws[i],x,z);
        if(rebuild){order.resize(bounds.size());for(size_t i=0;i<order.size();++i)order[i]=static_cast<uint32_t>(i);nodes.clear();nodes.reserve(order.size()*2);
            if(!order.empty())Build(0,static_cast<uint32_t>(order.size()));}
        for(size_t at=nodes.size();at>0;--at){Node& n=nodes[at-1];n.bounds=bounds[order[n.first]];n.finite=true;
            if(n.left!=UINT32_MAX){n.bounds=nodes[n.left].bounds;Include(n.bounds,nodes[n.right].bounds);n.finite=nodes[n.left].finite && nodes[n.right].finite;}
            else for(uint32_t i=0;i<n.count;++i){const Draw& d=bounds[order[n.first+i]];Include(n.bounds,d);n.finite=n.finite && ValidBounds(d);}}
    }
    bool NodeVisible(const Node& n,const Matrix& vp)const{
        if(!n.finite)return true;
        // Pad only the hierarchy test. Individual leaves keep the existing
        // test, so floating-point cancellation cannot drop a boundary leaf.
        Draw d=n.bounds;const float size=(std::max)(1.f,(std::max)((std::max)(fabsf(d.low.x),fabsf(d.high.x)),
            (std::max)((std::max)(fabsf(d.low.y),fabsf(d.high.y)),(std::max)(fabsf(d.low.z),fabsf(d.high.z)))));
        const float pad=size*.00001f+.001f;d.low.x-=pad;d.low.y-=pad;d.low.z-=pad;d.high.x+=pad;d.high.y+=pad;d.high.z+=pad;
        return Visible(d,vp);
    }
    void Visit(uint32_t id,const Matrix& vp,const std::vector<unsigned char>& allowed,StaticVisibility& out,bool sun){
        const Node& n=nodes[id];++out.node_tests;if(!NodeVisible(n,vp) || (sun && n.finite && !sun_casters.Visible(&n.bounds.low.x,&n.bounds.high.x)))return;
        if(n.left!=UINT32_MAX){Visit(n.left,vp,allowed,out,sun);Visit(n.right,vp,allowed,out,sun);return;}
        for(uint32_t j=0;j<n.count;++j){const uint32_t i=order[n.first+j];if(!allowed[i])continue;++out.bounds_tests;
            if(!ValidBounds(bounds[i]) || (Visible(bounds[i],vp) && (!sun || sun_casters.Visible(&bounds[i].low.x,&bounds[i].high.x)))){out.draws[i]=1;out.selected.push_back(i);}}
    }
    const StaticVisibility& Select(const Matrix& vp,uint32_t sector,bool sun=false){
        ++queries;Key key;memcpy(key.data(),vp.m,64);key[16]=sector;key[17]=sun?1u:0u;
        const auto found=views.find(key);if(found!=views.end()){++cache_hits;return found->second;}
        // Bound cache memory without imposing a draw/light limit. Extremely
        // large working sets fall back to correct uncached selection.
        const size_t per_view=sizeof(StaticVisibility)+sizeof(Key)+bounds.size()*5;
        const size_t limit=(std::max)(size_t(1),size_t(16u*1024u*1024u)/per_view);
        StaticVisibility& result=views.size()<limit?views[key]:uncached;SelectStaticSectors(*scene,vp,sector,result);
        std::vector<unsigned char> allowed;allowed.swap(result.draws);result.draws.assign(bounds.size(),0);
        for(size_t i=0;i<allowed.size();++i)if(!allowed[i])++result.sector_rejected;
        if(!nodes.empty())Visit(0,vp,allowed,result,sun);
        std::sort(result.selected.begin(),result.selected.end());
        result.frustum_rejected=static_cast<uint32_t>(bounds.size()-result.sector_rejected-result.selected.size());
        bounds_tests+=result.bounds_tests;node_tests+=result.node_tests;return result;
    }
};
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
