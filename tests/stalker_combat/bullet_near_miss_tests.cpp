#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
using u16 = unsigned short;
using u32 = unsigned int;
using BOOL = bool;
const bool FALSE=false;
const float EPS_L=.0001f;
#define VERIFY(x) assert(x)
inline bool fis_zero(float f){return std::fabs(f)<.00001f;}
inline bool _valid(float f){return std::isfinite(f);}
template<class T> T _max(T a,T b){return std::max(a,b);}
template<class T> T clampr(T n,T a,T b){return std::max(a,std::min(n,b));}
template<class T> struct xr_vector : std::vector<T> {
    using std::vector<T>::operator=;
    void clear_and_reserve(){this->clear();}
};
struct Fvector {
    float x=0,y=0,z=0;
    Fvector(){} Fvector(float a,float b,float c):x(a),y(b),z(c){}
    Fvector& mad(const Fvector& a,const Fvector& b,float k){x=a.x+b.x*k;y=a.y+b.y*k;z=a.z+b.z*k;return *this;}
    Fvector& sub(const Fvector& a,const Fvector& b){x=a.x-b.x;y=a.y-b.y;z=a.z-b.z;return *this;}
    Fvector& mul(float k){x*=k;y*=k;z*=k;return *this;}
    Fvector& div(float k){return mul(1.f/k);}
    Fvector& add(const Fvector& a){x+=a.x;y+=a.y;z+=a.z;return *this;}
    float dotproduct(const Fvector& a)const{return x*a.x+y*a.y+z*a.z;}
    float square_magnitude()const{return dotproduct(*this);}
    float magnitude()const{return std::sqrt(square_magnitude());}
};
struct CObject {virtual ~CObject(){};};
struct CAI_Stalker: CObject {
    u16 id=1; bool ready=true; unsigned events=0; Fvector center,point,incoming;
    u16 ID()const{return id;}
    bool bullet_near_miss_ready()const{return ready;}
    void Center(Fvector& v)const{v=center;}
    void on_bullet_near_miss(const Fvector& v,const Fvector& direction){++events;point=v;incoming=direction;ready=false;}
};
template<class T> T smart_cast(CObject* o){return dynamic_cast<T>(o);}
namespace ALife {enum EHitType {eHitTypeFireWound, eHitTypeStrike};}
struct SBullet {
    Fvector pos,dir=Fvector(1,0,0);
    float speed=1000,max_dist=1000,fly_dist=0,air_resistance=0,max_speed=1000;
    u32 frame_num=0;u16 parent_id=99;
    ALife::EHitType hit_type=ALife::eHitTypeFireWound;
    struct {bool ricochet_was=false,skipped_frame=false;}flags;
};
struct bullet_test_callback_data {SBullet* pBullet;bool bStopTracing;};
struct ISpatial{};
namespace CDB {const u32 OPT_CULL=1;}
namespace collide {
    enum rq_target{rqtBoth};
    struct ray_defs {
        Fvector start,dir;float range;
        ray_defs(const Fvector& a,const Fvector& b,float r,u32,rq_target):start(a),dir(b),range(r){}
    };
    struct rq_result {CObject* O=nullptr;float range=0;};
    struct rq_results {
        rq_result result;
        rq_result* r_begin(){return &result;}
        unsigned r_count(){return 1;}
    };
}
struct Fbox{float x1=-10000,x2=10000,y1=-10000,z1=-10000,z2=10000;};
struct ObjectSpaceFixture {
    xr_vector<CObject*> objects;
    bool blocked=false,collision=false,ricochet=false,stop=true;
    float collision_range=0;
    unsigned rays=0,queries=0;
    void GetNearest(xr_vector<CObject*>& out,const Fvector&,float,CObject*){out=objects;++queries;}
    bool RayPick(const Fvector&,const Fvector&,float,collide::rq_target,collide::rq_result&,CObject*){
        ++rays;return blocked;
    }
    bool RayQuery(collide::rq_results& out,const collide::ray_defs& ray,int,bullet_test_callback_data* data,int,void*){
        out.result.range=collision_range;data->bStopTracing=stop;
        if(collision&&ricochet){data->pBullet->flags.ricochet_was=true;data->pBullet->pos.mad(ray.start,ray.dir,collision_range);data->pBullet->dir=Fvector(0,0,1);}
        else if(collision&&stop)data->pBullet->speed=0;
        return collision;
    }
    Fbox GetBoundingVolume(){return Fbox();}
};
struct LevelFixture {ObjectSpaceFixture ObjectSpace;} level;
LevelFixture& Level(){return level;}
const int GAME_SINGLE=1;int game=GAME_SINGLE;int GameID(){return game;}
struct {u32 dwFrame=1;} Device;
struct CBulletManager {
    struct NearMissSegment {Fvector start,direction;float range;u16 shooter;};
    xr_vector<NearMissSegment> m_near_miss_segments;
    float m_fAirResistanceK=0,m_fGravityConst=9.8f,m_fMinBulletSpeed=2;
    static const int firetrace_callback=0,test_callback=0;
    void QueueNearMissSegment(const SBullet&,const collide::ray_defs&,float);
    void CommitNearMissSegments();
    bool CalcBullet(collide::rq_results&,xr_vector<ISpatial*>&,SBullet*,u32);
};
#include "bullet_near_miss.generated.h"

int main(){
    CBulletManager manager;SBullet bullet;
    collide::rq_results storage;xr_vector<ISpatial*> spatial;
    CAI_Stalker near,far,self,unready;CObject other;
    near.center=Fvector(5,1,0);far.center=Fvector(5,1.51f,0);self.id=bullet.parent_id;self.center=near.center;
    unready.center=near.center;unready.ready=false;
    level.ObjectSpace.objects={&near,&far,&self,&unready,&other};
    assert(manager.CalcBullet(storage,spatial,&bullet,10));
    assert(near.events==0); // worker never invokes AI
    assert(manager.m_near_miss_segments[0].range==10.f);
    assert(bullet.pos.x==10.f&&std::fabs(bullet.speed-1000.f)<.01f);
    manager.CommitNearMissSegments();
    assert(near.events==1&&near.point.x==5.f&&near.point.y==0.f&&near.incoming.x==1.f);
    assert(far.events==0&&self.events==0&&unready.events==0&&level.ObjectSpace.rays==1);
    assert(manager.m_near_miss_segments.empty());
    // Clipping at a stopping wall excludes a target farther down the original ray.
    bullet=SBullet();near.ready=true;near.center=Fvector(7,1,0);
    level.ObjectSpace.collision=true;level.ObjectSpace.collision_range=3.f;
    assert(!manager.CalcBullet(storage,spatial,&bullet,10));
    assert(manager.m_near_miss_segments[0].range==3.f);
    manager.CommitNearMissSegments();assert(near.events==1);
    // An NPC very close to the impact, but behind cover, still requires clear local LOS.
    bullet=SBullet();near.center=Fvector(3.5f,0,0);level.ObjectSpace.blocked=true;
    manager.CalcBullet(storage,spatial,&bullet,10);manager.CommitNearMissSegments();assert(near.events==1);
    // Ricochet records incoming path, never the changed outgoing direction / impact as its origin.
    bullet=SBullet();level.ObjectSpace.ricochet=true;level.ObjectSpace.blocked=false;near.center=Fvector(1,1,0);
    manager.CalcBullet(storage,spatial,&bullet,10);
    assert(manager.m_near_miss_segments[0].start.x==0.f&&manager.m_near_miss_segments[0].direction.x==1.f);
    assert(manager.m_near_miss_segments[0].range==3.f);
    manager.CommitNearMissSegments();assert(near.events==2);
    // Fully transparent collision does not truncate the actual flight.
    bullet=SBullet();level.ObjectSpace.ricochet=false;level.ObjectSpace.stop=false;near.ready=true;near.center=Fvector(8,1,0);
    assert(manager.CalcBullet(storage,spatial,&bullet,10));
    assert(manager.m_near_miss_segments[0].range==10.f);
    manager.CommitNearMissSegments();assert(near.events==3);
    // Queue bounds, no zero-length / non-bullet / multiplayer notifications.
    collide::ray_defs ray(Fvector(),Fvector(1,0,0),10,0,collide::rqtBoth);
    manager.QueueNearMissSegment(bullet,ray,0);assert(manager.m_near_miss_segments.empty());
    bullet.hit_type=ALife::eHitTypeStrike;manager.QueueNearMissSegment(bullet,ray,10);assert(manager.m_near_miss_segments.empty());
    bullet.hit_type=ALife::eHitTypeFireWound;game=2;manager.QueueNearMissSegment(bullet,ray,10);assert(manager.m_near_miss_segments.empty());game=1;
    for(unsigned i=0;i<500;++i)manager.QueueNearMissSegment(bullet,ray,10);
    assert(manager.m_near_miss_segments.size()==256u);
    // Hard LOS budget even in a crowded, fully occluded scene; discarded queue does not persist.
    std::vector<CAI_Stalker> crowd(100);level.ObjectSpace.objects.clear();level.ObjectSpace.rays=0;level.ObjectSpace.blocked=true;
    for(auto& npc:crowd){npc.center=Fvector(5,1,0);level.ObjectSpace.objects.push_back(&npc);}
    manager.CommitNearMissSegments();assert(level.ObjectSpace.rays==64&&manager.m_near_miss_segments.empty());
    for(auto& npc:crowd)assert(npc.events==0);
    std::puts("PASS: actual bullet near-miss clipping, worker deferral, ricochet, occlusion, self/cooldown, SP/firewound and work budgets");
}
