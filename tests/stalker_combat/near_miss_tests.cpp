#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include "stalker_combat_awareness_policy.h"
using u32 = unsigned int;
namespace MemorySpace { using squad_mask_type = u32; }
namespace ALife { enum ERelationType { eRelationTypeFriend, eRelationTypeNeutral, eRelationTypeEnemy }; }
unsigned int contact_casts=0;
template <typename T,typename U> T smart_cast(U* value) {++contact_casts;return dynamic_cast<T>(value);}
struct Fvector {
    float x=0,y=0,z=0;
    Fvector(){} Fvector(float a,float b,float c):x(a),y(b),z(c){}
    float square_magnitude() const {return x*x+y*y+z*z;}
    float distance_to_sqr(const Fvector& b) const {return (x-b.x)*(x-b.x)+(y-b.y)*(y-b.y)+(z-b.z)*(z-b.z);}
    void sub(const Fvector& a,const Fvector& b){x=a.x-b.x;y=a.y-b.y;z=a.z-b.z;}
    void invert(){x=-x;y=-y;z=-z;}
    float dotproduct(const Fvector& b) const{return x*b.x+y*b.y+z*b.z;}
    void normalize_safe(){float n=std::sqrt(square_magnitude());if(n>0){x/=n;y/=n;z/=n;}}
    void mad(const Fvector& a,const Fvector& b,float k){x=a.x+b.x*k;y=a.y+b.y*k;z=a.z+b.z*k;}
};
float _sqrt(float n){return std::sqrt(n);}
struct CEntityAlive {
    virtual ~CEntityAlive() {}
    u32 id=10; bool alive=true; ALife::ERelationType relation=ALife::eRelationTypeEnemy;
    u32 ID() const{return id;} bool g_Alive() const{return alive;}
    // Deliberately no Position()/Center(): sector decisions must compile using snapshots only.
};
struct MemoryMask { u32 value=1; bool test(u32 mask) const{return (value&mask)!=0;} };
struct CMemoryInfo {
    const CEntityAlive* m_object=nullptr;
    bool m_enabled=true; u32 m_level_time=0; MemoryMask m_squad_mask;
    struct Params {Fvector m_position;} m_object_params;
};
bool _valid(const Fvector& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
struct CVisualMemoryManager {using VISIBLES=std::vector<CMemoryInfo>;};
struct CSoundMemoryManager {using SOUNDS=std::vector<CMemoryInfo>;};
struct CHitMemoryManager {using HITS=std::vector<CMemoryInfo>;};
struct DeviceData { u32 dwTimeGlobal=10000; } Device;
enum { eBodyStateStand, eBodyStateCrouch, eMovementTypeStand, eMovementTypeWalk, eMovementTypeRun,
    eMentalStateDanger, eMentalStateFree };
struct CDangerObject {
    enum Type { eDangerTypeBulletRicochet, eDangerTypeGrenade };
    enum Perception { eDangerPerceiveTypeSound };
    const void* source; Fvector point; u32 time; Type kind;
    CDangerObject(const void* who, const Fvector& where, u32 when, Type what, Perception)
        : source(who), point(where), time(when), kind(what) {}
    Type type() const { return kind; }
};
struct Memory {
    struct Enemies { const CEntityAlive* target=nullptr; const CEntityAlive* selected() const { return target; } } enemies;
    struct Danger {
        const CDangerObject* chosen=nullptr; std::vector<CDangerObject> entries;
        const CDangerObject* selected() const { return chosen; }
        void add(const CDangerObject& event) { entries.push_back(event); }
    } dangers;
    struct Records {
        std::vector<CMemoryInfo> values;
        const std::vector<CMemoryInfo>& objects() const{return values;}
    } sounds, hits;
    struct Visual : Records {
        bool seen=false, lingering=false;
        bool visible_now(const CEntityAlive*) const{return seen||lingering;}
        bool visible_right_now(const CEntityAlive*) const{return seen;}
        u32 mask() const{return 1;}
    } visuals;
    Visual& visual(){return visuals;}
    Records& sound(){return sounds;} Records& hit(){return hits;}
    Enemies& enemy() { return enemies; } Danger& danger() { return dangers; }
};
struct Movement {
    int target_body=eBodyStateStand, current_body=eBodyStateStand;
    int type=eMovementTypeStand, mental=eMentalStateDanger;
    int target_body_state() const { return target_body; }
    int target_movement_type() const { return type; }
    int target_mental_state() const { return mental; }
    void set_body_state(int value) { target_body=value; }
    void update() { current_body=target_body; }
};
struct CAI_Stalker : CEntityAlive {
    stalker_combat_awareness::NearMiss m_combat_near_miss;
    Fvector m_combat_near_miss_point;
    stalker_combat_awareness::AlertLook m_combat_alert;
    Fvector m_combat_alert_point,position;
    const Fvector& Position() const{return position;}
    mutable Memory memories; Movement moving;
    struct Eyes {Fvector k=Fvector(0,0,1);} eye_matrix;
    struct Agent {
        struct Members {
            std::vector<u32> ids;
            const u32* get_member(u32 id) const {for(const auto& value:ids)if(value==id)return &value;return nullptr;}
        } people;
        Members& member(){return people;}
    } agent;
    Agent& agent_manager(){return agent;}
    bool is_relation_enemy(const CEntityAlive* other) const{return other->relation==ALife::eRelationTypeEnemy;}
    ALife::ERelationType tfGetRelationType(const CEntityAlive* other) const{return other->relation;}
    bool combat_known_friendly_fire(const Fvector&);
    bool alive=true, destroyed=false, remote=false, script=false, injured=false, critical=false, talking=false;
    struct Animation { std::vector<int> scripts; const std::vector<int>& script_animations() const { return scripts; } } animations;
    bool g_Alive() const { return alive; } bool getDestroy() const { return destroyed; }
    bool Remote() const { return remote; } bool GetScriptControl() const { return script; }
    bool wounded() const { return injured; } bool critically_wounded() { return critical; }
    bool IsTalking() { return talking; } Animation& animation() { return animations; }
    Memory& memory() const { assert(alive && !destroyed); return memories; }
    Movement& movement() { return moving; } u32 ID() const { return 7; }
    bool bullet_near_miss_ready() const; void on_bullet_near_miss(const Fvector&,const Fvector&);
    void on_combat_heard_shot(const Fvector&); bool combat_alert_look(Fvector&);
    u32 combat_near_miss_revision() const; bool combat_near_miss_recent(u32) const;
    void process_combat_near_miss(); void update_combat_near_miss();
};
namespace stalker_combat_diagnostics {
    enum Category { action }; std::vector<std::string> events;
    bool enabled(u32) { return true; }
    void log(u32, Category, const char* event, const char*, ...) { events.push_back(event); }
}
#include "near_miss.generated.h"

void idle_and_combat()
{
    Device.dwTimeGlobal=10000;
    CAI_Stalker npc; Fvector point; point.x=.8f; point.y=1.f;
    assert(npc.bullet_near_miss_ready());
    npc.on_bullet_near_miss(point,Fvector(1,0,0));
    assert(npc.combat_near_miss_revision()==1 && !npc.bullet_near_miss_ready());
    assert(npc.memories.dangers.entries.empty()); // Deferred: no invalidation of selected danger during delivery.
    Device.dwTimeGlobal+=250;
    npc.on_bullet_near_miss(point,Fvector(1,0,0)); assert(npc.combat_near_miss_revision()==1); // Pending events coalesce.
    npc.process_combat_near_miss();
    assert(npc.memories.dangers.entries.size()==1 && !npc.memories.dangers.entries[0].source);
    assert(npc.memories.dangers.entries[0].point.x==point.x && !npc.memories.enemies.target);
    npc.update_combat_near_miss(); assert(npc.moving.target_body==eBodyStateStand);

    CAI_Stalker combat; CEntityAlive known_enemy; combat.memories.enemies.target=&known_enemy;
    combat.on_bullet_near_miss(point,Fvector(1,0,0)); combat.process_combat_near_miss();
    combat.update_combat_near_miss(); combat.movement().update();
    assert(combat.moving.current_body==eBodyStateCrouch && combat.memories.dangers.entries.empty());
    assert(combat.memories.enemies.target==&known_enemy); // No shooter memory or targeting operation.
    Device.dwTimeGlobal+=300;
    combat.on_bullet_near_miss(point,Fvector(1,0,0)); combat.process_combat_near_miss();
    assert(combat.moving.target_body==eBodyStateStand); // Released before the planner's next decision.
    combat.update_combat_near_miss(); assert(combat.moving.target_body==eBodyStateCrouch);
    Device.dwTimeGlobal+=600;
    combat.process_combat_near_miss(); combat.update_combat_near_miss(); combat.movement().update();
    assert(combat.moving.current_body==eBodyStateStand); // Sustained fire did not restart the first 900ms duck.
    combat.on_bullet_near_miss(point,Fvector(1,0,0)); combat.process_combat_near_miss(); combat.update_combat_near_miss();
    assert(combat.moving.target_body==eBodyStateStand); // Cooldown cannot pin an NPC crouched.
    Device.dwTimeGlobal+=1600;
    combat.on_bullet_near_miss(point,Fvector(1,0,0)); combat.process_combat_near_miss(); combat.update_combat_near_miss();
    assert(combat.moving.target_body==eBodyStateCrouch);
}

void priorities_and_lifecycle()
{
    CEntityAlive enemy;
    for(int reason=0;reason<11;++reason) {
        Device.dwTimeGlobal=20000;
        CAI_Stalker npc; npc.memories.enemies.target=&enemy;
        Fvector point;
        CDangerObject grenade(nullptr,point,Device.dwTimeGlobal,CDangerObject::eDangerTypeGrenade,CDangerObject::eDangerPerceiveTypeSound);
        npc.on_bullet_near_miss(point,Fvector(1,0,0)); npc.process_combat_near_miss();
        switch(reason) {
        case 0:npc.alive=false;break; case 1:npc.destroyed=true;break;
        case 2:npc.remote=true;break; case 3:npc.script=true;break;
        case 4:npc.injured=true;break; case 5:npc.critical=true;break;
        case 6:npc.talking=true;break; case 7:npc.animations.scripts.push_back(1);break;
        case 8:npc.memories.dangers.chosen=&grenade;break;
        case 9:npc.moving.type=eMovementTypeRun;break;
        case 10:npc.moving.mental=eMentalStateFree;break;
        }
        npc.update_combat_near_miss(); assert(npc.moving.target_body==eBodyStateStand);
        assert(!npc.m_combat_near_miss.response_pending && !npc.m_combat_near_miss.duck_active);
    }
    // A script, injury or other high-priority owner can take over between ticks
    // and deliberately request Crouch. Ending our duck must preserve that request.
    for(int reason=0;reason<8;++reason) {
        Device.dwTimeGlobal=30000;
        CAI_Stalker npc; npc.memories.enemies.target=&enemy;
        Fvector point;
        npc.on_bullet_near_miss(point,Fvector(1,0,0)); npc.process_combat_near_miss(); npc.update_combat_near_miss();
        assert(npc.m_combat_near_miss.restore_body && npc.moving.target_body==eBodyStateCrouch);
        switch(reason) {
        case 0:npc.alive=false;break; case 1:npc.destroyed=true;break;
        case 2:npc.remote=true;break; case 3:npc.script=true;break;
        case 4:npc.injured=true;break; case 5:npc.critical=true;break;
        case 6:npc.talking=true;break; case 7:npc.animations.scripts.push_back(1);break;
        }
        npc.moving.set_body_state(eBodyStateCrouch);
        npc.m_combat_near_miss.pending=npc.m_combat_near_miss.response_pending=true;
        npc.process_combat_near_miss();
        assert(npc.moving.target_body==eBodyStateCrouch);
        assert(!npc.m_combat_near_miss.restore_body && !npc.m_combat_near_miss.pending &&
            !npc.m_combat_near_miss.response_pending && !npc.m_combat_near_miss.duck_active);
    }
    CAI_Stalker delayed; Fvector point;
    delayed.on_bullet_near_miss(point,Fvector(1,0,0)); Device.dwTimeGlobal+=1500; delayed.process_combat_near_miss();
    assert(delayed.memories.dangers.entries.empty()); // Offline/scheduler delay cannot inject a stale danger.
    delayed.m_combat_near_miss=stalker_combat_awareness::NearMiss();
    assert(!delayed.combat_near_miss_revision() && !delayed.combat_near_miss_recent(5000));
    point.x=std::numeric_limits<float>::quiet_NaN(); delayed.on_bullet_near_miss(point,Fvector(1,0,0));
    assert(!delayed.combat_near_miss_revision());
    CAI_Stalker crouched; crouched.memories.enemies.target=&enemy; crouched.moving.target_body=eBodyStateCrouch;
    point.x=0; crouched.on_bullet_near_miss(point,Fvector(1,0,0)); crouched.process_combat_near_miss(); crouched.update_combat_near_miss();
    Device.dwTimeGlobal+=1000; crouched.process_combat_near_miss(); crouched.update_combat_near_miss();
    assert(crouched.moving.target_body==eBodyStateCrouch); // An action's existing crouch is never owned/restored by us.
    stalker_combat_awareness::NearMiss wrap;
    assert(wrap.observe(0xffffff00u)); wrap.pending=false;
    assert(!wrap.ready(0xffffff01u)); assert(wrap.ready(10u) && wrap.recent(10u,500u));
    assert(wrap.start_duck(10u)); assert(wrap.ducking(100u) && !wrap.ducking(910u));
}
void audible_attention()
{
    Device.dwTimeGlobal=50000; CAI_Stalker npc; Fvector event(10,1,4),look;
    npc.on_combat_heard_shot(event);
    assert(npc.combat_alert_look(look) && look.x==10 && look.z==4);
    assert(!npc.memories.enemies.target && npc.memories.dangers.entries.empty());
    Device.dwTimeGlobal+=500; npc.on_combat_heard_shot(Fvector(-20,0,0));
    assert(npc.combat_alert_look(look) && look.x==10); // Burst cannot drag the gaze each frame.
    Device.dwTimeGlobal+=1000; assert(!npc.combat_alert_look(look));
    Device.dwTimeGlobal+=1000; npc.on_combat_heard_shot(Fvector(-20,0,0));
    assert(npc.combat_alert_look(look) && look.x==-20);
    for(int reason=0;reason<11;++reason){
        Device.dwTimeGlobal+=3000; CAI_Stalker guarded; guarded.on_combat_heard_shot(event);
        CEntityAlive enemy; CDangerObject grenade(nullptr,event,Device.dwTimeGlobal,CDangerObject::eDangerTypeGrenade,CDangerObject::eDangerPerceiveTypeSound);
        switch(reason){
        case 0:guarded.alive=false;break;case 1:guarded.destroyed=true;break;
        case 2:guarded.remote=true;break;case 3:guarded.script=true;break;
        case 4:guarded.injured=true;break;case 5:guarded.critical=true;break;
        case 6:guarded.talking=true;break;case 7:guarded.animations.scripts.push_back(1);break;
        case 8:guarded.memories.dangers.chosen=&grenade;break;case 9:guarded.moving.type=eMovementTypeRun;break;
        case 10:guarded.memories.enemies.target=&enemy;guarded.memories.visuals.seen=true;break;
        }
        assert(!guarded.combat_alert_look(look));
    }
    CAI_Stalker silent; silent.on_combat_heard_shot(Fvector(41,0,0));assert(!silent.combat_alert_look(look));
    silent.on_combat_heard_shot(Fvector());assert(!silent.combat_alert_look(look));
    // A true near miss looks toward a local incoming sector, independently of danger/Lua acceptance.
    CAI_Stalker flyby;flyby.on_bullet_near_miss(Fvector(0,1,1),Fvector(1,0,0));
    assert(flyby.combat_alert_look(look) && look.x==-3 && look.y==1 && look.z==1);
    assert(!flyby.memories.enemies.target);
}
CMemoryInfo report(const CEntityAlive& other, const Fvector& point, u32 age=0, u32 mask=1)
{
    CMemoryInfo value;value.m_object=&other;value.m_object_params.m_position=point;
    value.m_level_time=Device.dwTimeGlobal-age;value.m_squad_mask.value=mask;return value;
}
void visible_enemy_cancels_flyby()
{
    Device.dwTimeGlobal=60000; CAI_Stalker npc; CEntityAlive enemy;
    npc.memories.enemies.target=&enemy;npc.memories.visuals.seen=true;
    assert(!npc.bullet_near_miss_ready());
    npc.on_bullet_near_miss(Fvector(0,1,0),Fvector(0,0,1));
    npc.on_combat_heard_shot(Fvector(0,1,-10));
    assert(!npc.combat_near_miss_revision() && !npc.m_combat_alert.active);
    // The enemy can be acquired after main-thread delivery but before the scheduled response.
    npc.memories.visuals.seen=false;
    npc.on_bullet_near_miss(Fvector(0,1,0),Fvector(0,0,1));
    assert(npc.combat_near_miss_revision()==1);
    npc.memories.visuals.seen=true;
    npc.process_combat_near_miss();npc.update_combat_near_miss();
    assert(!npc.m_combat_near_miss.pending && !npc.m_combat_near_miss.response_pending &&
        !npc.m_combat_near_miss.duck_active && !npc.m_combat_alert.active);
    assert(npc.moving.target_body==eBodyStateStand && npc.memories.dangers.entries.empty());
    // Visibility acquired during the memory update must also cancel before applying posture.
    CAI_Stalker late;late.memories.enemies.target=&enemy;
    late.on_bullet_near_miss(Fvector(0,1,0),Fvector(0,0,1));late.process_combat_near_miss();
    late.memories.visuals.seen=true;late.update_combat_near_miss();
    assert(late.moving.target_body==eBodyStateStand && !late.m_combat_near_miss.duck_active);
    // A stale visible_now grace period is not personal sight.
    CAI_Stalker grace;grace.memories.enemies.target=&enemy;grace.memories.visuals.lingering=true;
    grace.on_bullet_near_miss(Fvector(0,1,0),Fvector(0,0,1));
    assert(grace.combat_near_miss_revision()==1);
}
void friendly_rear_sector_uses_known_positions()
{
    Device.dwTimeGlobal=70000;CEntityAlive enemy,ally,back_enemy;
    ally.id=11;ally.relation=ALife::eRelationTypeFriend;back_enemy.id=12;
    const Fvector incoming(0,0,-1),point(0,1,0),flight(0,0,1);
    for(int mode=0;mode<26;++mode) {
        CAI_Stalker npc;npc.memories.enemies.target=&enemy;
        npc.memories.visuals.values.push_back(report(ally,Fvector(0,0,-10)));
        npc.memories.visuals.values.push_back(report(enemy,Fvector(0,0,10)));
        bool expected=true;
        switch(mode) {
        case 0:break; // Own fresh visual snapshot.
        case 1:npc.memories.visuals.values[0].m_squad_mask.value=3;break; // Shared squad observation.
        case 2:npc.memories.visuals.values.erase(npc.memories.visuals.values.begin());expected=false;break;
        case 3:npc.memories.visuals.values[0].m_level_time=Device.dwTimeGlobal-5001;expected=false;break;
        case 4:npc.memories.visuals.values[0].m_squad_mask.value=2;expected=false;break;
        case 5:npc.memories.visuals.values[0].m_object_params.m_position=Fvector(10,0,0);expected=false;break;
        case 6:npc.memories.sounds.values.push_back(report(back_enemy,Fvector(0,0,-12)));expected=false;break;
        case 7:npc.memories.hits.values.push_back(report(back_enemy,Fvector(0,0,-12)));expected=false;break;
        case 8:npc.memories.visuals.values.push_back(report(back_enemy,Fvector(0,0,-12)));expected=false;break;
        case 9:npc.memories.visuals.values[1].m_object_params.m_position=Fvector(0,0,-10);expected=false;break;
        case 10:npc.memories.visuals.values[1].m_level_time=Device.dwTimeGlobal-15001;expected=false;break;
        case 11:npc.memories.visuals.values[0].m_enabled=false;expected=false;break;
        case 12:ally.relation=ALife::eRelationTypeNeutral;npc.agent.people.ids.push_back(ally.ID());break;
        case 13:ally.relation=ALife::eRelationTypeNeutral;expected=false;break;
        case 14:npc.memories.sounds.values.push_back(report(enemy,Fvector(0,0,-12),10000));break;
        case 15:npc.memories.hits.values.push_back(report(enemy,Fvector(0,0,-12),10000));break;
        case 16:npc.memories.visuals.values.push_back(report(enemy,Fvector(0,0,-12),10000));break;
        case 17:
            npc.memories.visuals.values[0].m_level_time-=1000;
            npc.memories.sounds.values.push_back(report(ally,Fvector(0,0,8)));expected=false;break;
        case 18:npc.memories.visuals.values[1].m_squad_mask.value=2;expected=false;break;
        case 19:case 20:
            // A newer inaccessible/disabled hit must not hide the intermediate usable sound.
            npc.memories.visuals.values[1]=report(enemy,Fvector(0,0,-12),10000);
            npc.memories.sounds.values.push_back(report(enemy,Fvector(0,0,10),1000));
            npc.memories.hits.values.push_back(report(enemy,Fvector(0,0,-12),0,mode==19?2:1));
            if(mode==20)npc.memories.hits.values.back().m_enabled=false;
            break;
        case 21:
            npc.memories.visuals.values[0].m_level_time-=1000;
            npc.memories.sounds.values.push_back(report(ally,Fvector(0,0,8),500));
            npc.memories.hits.values.push_back(report(ally,Fvector(0,0,-10),0,2));expected=false;break;
        case 22:
            npc.memories.visuals.values.push_back(report(back_enemy,Fvector(0,0,-12),1000));
            npc.memories.hits.values.push_back(report(back_enemy,Fvector(0,0,10),0,2));expected=false;break;
        case 23:npc.memories.sounds.values.push_back(report(enemy,Fvector(0,0,-12)));break;
        case 24:
            npc.memories.visuals.values[1].m_object_params.m_position=Fvector(0,0,-10);
            npc.memories.sounds.values.push_back(report(enemy,Fvector(0,0,10)));expected=false;break;
        case 25:
            npc.memories.hits.values.push_back(report(enemy,Fvector(0,0,-12)));
            npc.memories.hits.values.back().m_level_time+=1000;break;
        }
        assert(npc.combat_known_friendly_fire(incoming)==expected);
        npc.on_bullet_near_miss(point,flight);
        assert((npc.combat_near_miss_revision()==0)==expected);
        assert(npc.memories.enemies.target==&enemy);
        if(expected) {
            assert(!npc.combat_near_miss_recent(5000)); // Does not cancel cautious search.
            npc.on_combat_heard_shot(Fvector(0,1,-10));assert(!npc.m_combat_alert.active);
        }
        ally.relation=ALife::eRelationTypeFriend;
    }
    CAI_Stalker npc;npc.memories.enemies.target=&enemy;
    npc.memories.visuals.values.push_back(report(ally,Fvector(0,0,-10)));
    npc.memories.visuals.values.push_back(report(enemy,Fvector(0,0,10)));
    assert(!npc.combat_known_friendly_fire(Fvector(1,0,0)));
    assert(!npc.combat_known_friendly_fire(Fvector(0,0,1)));
    npc.memories.enemies.target=nullptr;assert(!npc.combat_known_friendly_fire(incoming));
    npc.memories.enemies.target=&enemy;
    npc.memories.visuals.values.resize(128,npc.memories.visuals.values.front());
    assert(npc.combat_known_friendly_fire(incoming)); // Exactly the raw cap still works.
    // The cap is checked before casting/collecting any entry across all three arrays.
    npc.memories.hits.values.push_back(report(back_enemy,Fvector(0,0,-12)));
    const unsigned int casts=contact_casts;
    assert(!npc.combat_known_friendly_fire(incoming) && contact_casts==casts);
    npc.memories.visuals.values.resize(1);npc.memories.hits.values.resize(1);
    npc.memories.sounds.values.resize(127);
    assert(!npc.combat_known_friendly_fire(incoming) && contact_casts==casts);
}
int main()
{
    idle_and_combat(); priorities_and_lifecycle(); audible_attention();
    visible_enemy_cancels_flyby(); friendly_rear_sector_uses_known_positions();
    std::cout << "PASS: production near-miss notification, visible-enemy priority, known friendly sectors, anonymous danger, bounded duck and lifecycle\n";
}
