#include <cassert>
#include <cstdio>
#include <map>
using u32=unsigned int;using LPCSTR=const char*;
struct {u32 dwTimeGlobal=10000;} Device;
struct Fvector {float x=0,y=0,z=0;};
enum {eMentalStateDanger,eBodyStateStand,eBodyStateCrouch,eMovementTypeStand,eMovementTypeRun};
enum {eWorldPropertyInCover,eWorldPropertyLookedOut,eWorldPropertyPositionHolded,eWorldPropertyEnemyDetoured,eWorldPropertyUseCrouchToLookOut};
enum {eStalkerSoundNeedBackup,eStalkerSoundDetour};
namespace MovementManager {enum {ePathTypeLevelPath};}
namespace DetailPathManager {enum {eDetailPathTypeSmooth};}
namespace SightManager {enum {eSightTypePosition};}
struct CSightAction {CSightAction(int,const Fvector&,bool){}};
struct CEntityAlive {bool human_being()const{return true;}};
struct CCoverPoint {
    Fvector pos;u32 vertex=3;
    const Fvector& position()const{return pos;}u32 level_vertex_id()const{return vertex;}
};
struct CMemoryInfo {
    const CEntityAlive* m_object=nullptr;
    struct {Fvector m_position;u32 m_level_vertex_id=7;}m_object_params;
};
struct Memory {
    struct Enemies {const CEntityAlive* selected()const{static CEntityAlive enemy;return &enemy;}} enemies;
    CMemoryInfo known;
    Memory(){static CEntityAlive enemy;known.m_object=&enemy;known.m_object_params.m_position.z=20;}
    Enemies& enemy(){return enemies;}
    CMemoryInfo memory(const CEntityAlive*)const{return known;}
};
struct Movement {
    struct Level {bool fail=false;bool failed()const{return fail;}} level;
    bool complete=true;int type=eMovementTypeStand;u32 routes=0;
    void set_desired_direction(const Fvector*){}void set_path_type(int){}void set_detail_path_type(int){}
    void set_mental_state(int){}void set_body_state(int){}void set_movement_type(int v){type=v;}
    void set_nearest_accessible_position(){complete=true;}
    void set_level_dest_vertex(u32){complete=false;++routes;}
    void set_desired_position(const Fvector*){}
    bool path_completed()const{return complete;}Level& level_path(){return level;}
};
struct Storage {std::map<int,bool> flags;void set_property(int k,bool v){flags[k]=v;}bool property(int k){return flags[k];}};
struct CAI_Stalker;
struct Agent {
    struct Member {
        bool permitted=true,covering=false;
        struct Order {bool detouring=false;void detour(bool v){detouring=v;}void cover(int){}} order;
        Order& member(CAI_Stalker*){return order;}
        bool can_detour()const{return permitted;}bool cover_detouring()const{return covering;}
        bool group_behaviour()const{return false;}
    } members;
    Member& member(){return members;}
};
struct CoverEvaluator {void setup(const Fvector&,float,float,u32){}};
struct CAI_Stalker {
    Memory memories;Movement moving;Agent agent;Fvector pos;CoverEvaluator evaluator,*m_ce_angle=&evaluator;
    u32 blocked=20000,cover_queries=0;bool useful_fire=false,alive=true;
    struct Brain {void affect_cover(bool){}} brains;
    struct Sound {void play(int,int=0,int=0,int=0,int=0){}} sounds;
    struct Sight {void setup(const CSightAction&){}} sights;
    Memory& memory(){return memories;}Movement& movement(){return moving;}Agent& agent_manager(){return agent;}
    Brain& brain(){return brains;}Sound& sound(){return sounds;}Sight& sight(){return sights;}
    bool combat_target_position(Fvector& p,bool){p=memories.known.m_object_params.m_position;return true;}
    u32 combat_fire_blocked_time()const{return blocked;}
    float ffGetRange()const{return 30;}const Fvector& Position()const{return pos;}
    void best_cover(const Fvector&){++cover_queries;}bool g_Alive()const{return alive;}
};
struct CStalkerMovementRestrictor {CStalkerMovementRestrictor(CAI_Stalker*,bool){}};
struct AI {
    struct CoverManager {
        CCoverPoint point;bool available=true;u32 queries=0;
        const CCoverPoint* best_cover(const Fvector&,float,CoverEvaluator&,const CStalkerMovementRestrictor&){++queries;return available?&point:nullptr;}
    } covers;
    CoverManager& cover_manager(){return covers;}
} world;
AI& ai(){return world;}
struct Random {u32 random(u32){return 0;}} Random32;
class CStalkerActionCombatBase {
protected:
    CAI_Stalker* m_object;Storage storage;Storage* m_storage=&storage;
    u32 started=0,inertia=0;
public:
    CStalkerActionCombatBase(CAI_Stalker* npc,LPCSTR):m_object(npc){}
    virtual ~CStalkerActionCombatBase(){}
    virtual void initialize(){started=Device.dwTimeGlobal;}virtual void execute(){}virtual void finalize(){}
    CAI_Stalker& object(){return *m_object;}void aim_ready(){}void fire(){}
    bool fire_make_sense(){return object().useful_fire;}
    void set_inertia_time(u32 value){inertia=value;}bool completed()const{return Device.dwTimeGlobal-started>=inertia;}
    u32 start_level_time()const{return started;}
    bool flag(int k){return storage.property(k);}void flag(int k,bool v){storage.set_property(k,v);}
};
#include "search_progress.generated.h"
void hold_reaches_detour()
{
    CAI_Stalker npc;CStalkerActionHoldPosition action(&npc);
    action.initialize();action.flag(eWorldPropertyInCover,true);action.flag(eWorldPropertyLookedOut,true);
    action.execute();assert(action.flag(eWorldPropertyLookedOut));
    Device.dwTimeGlobal+=1000;action.execute();
    assert(action.flag(eWorldPropertyLookedOut)&&action.flag(eWorldPropertyPositionHolded)&&!action.flag(eWorldPropertyInCover));
    // Group cover fire can wait while another detour owns the role, then proceeds.
    action.initialize();action.flag(eWorldPropertyPositionHolded,false);
    npc.agent.members.permitted=false;npc.agent.members.covering=true;npc.useful_fire=true;
    Device.dwTimeGlobal+=1000;action.execute();assert(!action.flag(eWorldPropertyPositionHolded));
    npc.useful_fire=false;action.execute();assert(action.flag(eWorldPropertyPositionHolded));
}
void detour_finishes_one_leg()
{
    CAI_Stalker npc;CStalkerActionDetourEnemy action(&npc);Device.dwTimeGlobal=20000;world=AI();
    action.initialize();action.execute();assert(npc.moving.routes==1&&!action.flag(eWorldPropertyEnemyDetoured));
    npc.moving.complete=true;Device.dwTimeGlobal+=100;action.execute();
    assert(!action.flag(eWorldPropertyEnemyDetoured)&&npc.moving.routes==1); // Rebuild grace.
    Device.dwTimeGlobal+=200;action.execute();
    assert(action.flag(eWorldPropertyEnemyDetoured)&&npc.moving.routes==1&&world.covers.queries==1);
    action.finalize();assert(!npc.agent.members.order.detouring);
    for(int mode=0;mode<3;++mode){
        CAI_Stalker pending;CStalkerActionDetourEnemy blocked(&pending);Device.dwTimeGlobal=30000;world=AI();
        if(mode==0)world.covers.available=false;
        blocked.initialize();blocked.execute();
        if(mode==0)assert(blocked.flag(eWorldPropertyEnemyDetoured));
        else if(mode==1){pending.moving.level.fail=true;Device.dwTimeGlobal+=300;blocked.execute();assert(blocked.flag(eWorldPropertyEnemyDetoured));}
        else {Device.dwTimeGlobal+=5999;blocked.execute();assert(!blocked.flag(eWorldPropertyEnemyDetoured));
            ++Device.dwTimeGlobal;blocked.execute();assert(blocked.flag(eWorldPropertyEnemyDetoured));}
        assert(pending.moving.routes<=1);blocked.finalize();
    }
}
int main(){hold_reaches_detour();detour_finishes_one_leg();std::puts("PASS: production hidden-target hold/detour progression, one-leg routing, timeout and path failure");}
