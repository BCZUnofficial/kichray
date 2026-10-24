#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include "stalker_combat_cover_policy.h"
using u32=unsigned int; using u16=unsigned short;
struct {u32 dwTimeGlobal=10000;} Device;
template<class T,class U>T smart_cast(U* p){return dynamic_cast<T>(p);}
struct Fvector {
    float x=0,y=0,z=0;
    Fvector(){} Fvector(float a,float b,float c):x(a),y(b),z(c){}
    float distance_to_sqr(const Fvector& b)const{return (x-b.x)*(x-b.x)+(y-b.y)*(y-b.y)+(z-b.z)*(z-b.z);}
    float distance_to(const Fvector& b)const{return std::sqrt(distance_to_sqr(b));}
};
struct CObject {virtual ~CObject(){}u16 id=1;u16 ID()const{return id;}};
struct CMissile:CObject {u32 fuse=14000;u32 destroy_time()const{return fuse;}};
struct CDangerObject {
    enum Type{eDangerTypeGrenade};Fvector pos;CObject* explosive=nullptr;
    Type type()const{return eDangerTypeGrenade;}const Fvector& position()const{return pos;}
    const CObject* dependent_object()const{return explosive;}
};
struct CCoverPoint {
    Fvector pos;u32 vertex=1;const Fvector& position()const{return pos;}u32 level_vertex_id()const{return vertex;}
};
enum {eMovementTypeRun,eMovementTypeStand,eBodyStateStand,eBodyStateCrouch,eMentalStateDanger,eObjectActionIdle,
    eWorldPropertyCoverReached,eWorldPropertyLookedAround};
namespace MovementManager{enum {ePathTypeLevelPath};}
namespace DetailPathManager{enum {eDetailPathTypeSmooth};}
namespace StalkerMovement{enum {eVelocityRunDangerStandPositive};}
namespace SightManager{enum {eSightTypePathDirection};}
struct CSightAction{CSightAction(int,bool,bool){}};
struct Movement {
    Fvector dest;u32 vertex=0;bool complete=false;int type=-1,body=-1;
    struct Path {bool value=false;bool failed()const{return value;}} path;
    struct Detail {struct Velocity {float linear_velocity=5.f;} speed;const Velocity& velocity(int){return speed;}} detail_state;
    Path& level_path(){return path;}Detail& detail(){return detail_state;}
    bool path_completed()const{return complete;}
    void set_level_dest_vertex(u32 id){if(id!=vertex)complete=false;vertex=id;}
    void set_desired_position(const Fvector* p){dest=*p;}
    void set_nearest_accessible_position(){complete=true;dest=Fvector();vertex=0;}
    void set_movement_type(int v){type=v;}void set_body_state(int v){body=v;}
    void set_desired_direction(const Fvector*){}void set_path_type(int){}
    void set_detail_path_type(int){}void set_mental_state(int){}
};
struct CObjectHandler {void set_goal(int,void*){}};
struct CAI_Stalker:CObjectHandler {
    Fvector pos;Movement moving;const CCoverPoint* cover=nullptr;unsigned probes=0,invalidations=0;
    bool corridor=true;float radius=0;
    struct Memory {
        struct Danger {CDangerObject* value=nullptr;const CDangerObject* selected()const{return value;}} d;
        Danger& danger(){return d;}
    } mem;
    struct Agent {
        struct Members {struct Member {void cover(int){}} order;Member& member(CAI_Stalker*){return order;}} members;
        Members& member(){return members;}
    } agent;
    struct Sight {void setup(const CSightAction&){}} eyes;
    Memory& memory(){return mem;}Movement& movement(){return moving;}
    Agent& agent_manager(){return agent;}Sight& sight(){return eyes;}
    const Fvector& Position()const{return pos;}u32 ID()const{return 1;}
    void* best_weapon(){return nullptr;}
    void invalidate_best_cover(){++invalidations;}const CCoverPoint* best_cover(const Fvector&){return cover;}
};
// Geometry boundary: production local corridor probing is unchanged by this refactor.
bool stalker_grenade_escape(CAI_Stalker& npc,const Fvector&,float radius,Fvector& dest,u32& vertex)
{++npc.probes;npc.radius=radius;dest=Fvector(0,0,radius);vertex=2;return npc.corridor;}
namespace stalker_combat_diagnostics {
enum Event {grenade};bool enabled(u32){return false;}void log(u32,Event,const char*,const char*,...){}
}
#include "grenade_state.generated.h"
struct Base {
    CAI_Stalker& npc;std::map<int,bool> properties;
    explicit Base(CAI_Stalker& p):npc(p){}void initialize(){}void execute(){}
    CAI_Stalker& object(){return npc;}void set_property(int p,bool v){properties[p]=v;}
};
struct CStalkerActionDangerGrenadeTakeCover:Base {
    using inherited=Base;using Base::Base;CStalkerGrenadeEscape m_escape;
    void initialize();void execute();
};
#include "audit_grenade.generated.h"
int main(){
    CAI_Stalker npc;CMissile first,second;second.id=2;CDangerObject danger;danger.explosive=&first;
    npc.mem.d.value=&danger;CStalkerActionDangerGrenadeTakeCover action(npc);
    action.initialize();action.execute();
    assert(npc.probes==1&&npc.moving.type==eMovementTypeRun&&npc.moving.dest.z>0);
    assert(!action.properties[eWorldPropertyCoverReached]);
    Device.dwTimeGlobal+=100;action.execute();assert(npc.probes==1);
    danger.explosive=&second;action.execute();assert(npc.probes==2); // New grenade breaks the lease.
    npc.corridor=false;npc.moving.path.value=true;Device.dwTimeGlobal+=500;action.execute();
    assert(npc.moving.type==eMovementTypeStand&&npc.moving.body==eBodyStateCrouch);
    npc.corridor=true;npc.moving.path.value=false;
    CCoverPoint far;far.pos=Fvector(0,0,20);npc.cover=&far;second.fuse=Device.dwTimeGlobal+300;
    Device.dwTimeGlobal+=400;action.execute();
    assert(npc.moving.vertex==2&&npc.moving.type==eMovementTypeRun); // Far cover cannot beat fuse.
    CCoverPoint near;near.pos=Fvector(0,0,.5f);npc.cover=&near;
    Device.dwTimeGlobal+=2000;action.execute();npc.moving.complete=true;action.execute();
    assert(npc.moving.body==eBodyStateCrouch&&!action.properties[eWorldPropertyCoverReached]);
    danger.explosive=nullptr;action.execute();
    assert(action.properties[eWorldPropertyCoverReached]);
    std::puts("PASS: peaceful grenade reaction uses bounded escape, destination lease, fuse and explosion transitions");
}
