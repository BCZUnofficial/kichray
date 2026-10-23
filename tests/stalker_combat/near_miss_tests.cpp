#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include "stalker_combat_awareness_policy.h"
using u32 = unsigned int;
struct Fvector {
    float x=0,y=0,z=0;
    Fvector(){} Fvector(float a,float b,float c):x(a),y(b),z(c){}
    float square_magnitude() const {return x*x+y*y+z*z;}
    float distance_to_sqr(const Fvector& b) const {return (x-b.x)*(x-b.x)+(y-b.y)*(y-b.y)+(z-b.z)*(z-b.z);}
    void normalize_safe(){float n=std::sqrt(square_magnitude());if(n>0){x/=n;y/=n;z/=n;}}
    void mad(const Fvector& a,const Fvector& b,float k){x=a.x+b.x*k;y=a.y+b.y*k;z=a.z+b.z*k;}
};
float _sqrt(float n){return std::sqrt(n);}
struct CEntityAlive {};
bool _valid(const Fvector& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
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
    struct Visual {bool seen=false; bool visible_now(const CEntityAlive*) const{return seen;}} visuals;
    Visual& visual(){return visuals;}
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
struct CAI_Stalker {
    stalker_combat_awareness::NearMiss m_combat_near_miss;
    Fvector m_combat_near_miss_point;
    stalker_combat_awareness::AlertLook m_combat_alert;
    Fvector m_combat_alert_point,position;
    const Fvector& Position() const{return position;}
    Memory memories; Movement moving;
    bool alive=true, destroyed=false, remote=false, script=false, injured=false, critical=false, talking=false;
    struct Animation { std::vector<int> scripts; const std::vector<int>& script_animations() const { return scripts; } } animations;
    bool g_Alive() const { return alive; } bool getDestroy() const { return destroyed; }
    bool Remote() const { return remote; } bool GetScriptControl() const { return script; }
    bool wounded() const { return injured; } bool critically_wounded() { return critical; }
    bool IsTalking() { return talking; } Animation& animation() { return animations; }
    Memory& memory() { assert(alive && !destroyed); return memories; }
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
int main()
{
    idle_and_combat(); priorities_and_lifecycle(); audible_attention();
    std::cout << "PASS: production near-miss notification, anonymous danger, bounded duck, lifecycle and priorities\n";
}
