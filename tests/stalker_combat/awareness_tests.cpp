#include "awareness_fixture.h"
void configure(CAI_Stalker& npc,CEntityAlive& enemy,CAgentMemberManager& group,u16 id=2)
{
    npc.id=id;npc.agent.manager=&group;enemy.id=50;enemy.hidden=true;
    npc.memories.enemies.target=&enemy;npc.memories.info.m_object=&enemy;
    npc.memories.info.m_level_time=10000;npc.memories.info.m_object_params.m_position=Fvector(0,0,8);
    Device.dwTimeGlobal=10000;route=space=true;
}
void search_lifecycle()
{
    CAgentMemberManager group;CAI_Stalker npc;CEntityAlive enemy;configure(npc,enemy,group);
    CStalkerActionSearchEnemy action(&npc);action.initialize();action.execute();
    assert(npc.moving.body==eBodyStateCrouch && npc.moving.type==eMovementTypeWalk);
    assert(npc.moving.desired.distance_to(Fvector(0,0,4))<.001f);
    npc.pos=npc.moving.desired;Device.dwTimeGlobal+=1500;action.execute();
    assert(npc.moving.type==eMovementTypeStand && npc.moving.body==eBodyStateStand);
    assert(npc.animations.starts==1); // Stand-only prisluh/fallback starts on arrival.
    Device.dwTimeGlobal+=1500;action.execute();assert(npc.moving.body==eBodyStateCrouch);
    assert(!group.m_scout.assigned);action.finalize();
    assert(stalker_known_enemy_distance(npc,&enemy)==4.f);
    npc.memories.info.m_object=nullptr;assert(stalker_known_enemy_distance(npc,&enemy)==flt_max);
    // A failed, unsafe or old observation never enters crouch approach.
    for(int mode=0;mode<5;++mode){configure(npc,enemy,group);action.initialize();
        if(mode==0)route=false;
        if(mode==1){npc.memories.info.m_level_time=1000;Device.dwTimeGlobal=20000;}
        if(mode==2)npc.memories.visuals.record.squad=true;
        if(mode==3)space=false;
        if(mode==4)npc.near_miss_recent=true;
        action.execute();assert(npc.moving.body==eBodyStateStand);action.finalize();
        npc.memories.visuals.record.squad=false;npc.near_miss_recent=false;}
}
void search_cancellation()
{
    for(int cause=0;cause<10;++cause){
        CAgentMemberManager group;group.grouped=true;CAI_Stalker npc;CEntityAlive enemy;configure(npc,enemy,group);
        CDangerObject grenade;CStalkerActionSearchEnemy action(&npc);action.initialize();action.execute();
        assert(npc.moving.body==eBodyStateCrouch && group.m_scout.assigned);
        Device.dwTimeGlobal+=300;
        switch(cause){case 0:++npc.memories.hits.revision;break;case 1:npc.condition.hp=.8f;break;
        case 2:npc.memories.dangers.danger=&grenade;break;case 3:npc.moving.level.fail=true;break;
        case 4:npc.memories.visuals.seen=true;break;case 5:npc.memories.enemies.target=nullptr;break;
        case 6:npc.memories.info.m_object=nullptr;break;case 7:Device.dwTimeGlobal+=6000;break;
        case 8:npc.memories.info.m_object_params.m_position.x=3;++npc.memories.info.m_level_time;break;
        case 9:++npc.near_miss_revision;break;}
        action.execute();assert(npc.moving.body==eBodyStateStand && !group.m_scout.assigned);
        action.finalize();
    }
    CAgentMemberManager group;group.grouped=true;CAI_Stalker npc;CEntityAlive enemy;configure(npc,enemy,group);
    CStalkerActionSearchEnemy action(&npc);action.initialize();action.execute();
    npc.alive=false;group.m_scout.remove(npc.ID());action.finalize(); // No detached agent access on death.
}
void group_scout()
{
    CAgentMemberManager group;group.grouped=true;CAI_Stalker a,b;CEntityAlive enemy;
    configure(a,enemy,group,2);configure(b,enemy,group,3);
    CStalkerActionSearchEnemy first(&a),second(&b);first.initialize();second.initialize();
    first.execute();second.execute();
    assert(a.moving.body==eBodyStateCrouch && b.moving.body==eBodyStateStand);
    assert(group.m_scout.owner==a.ID());assert(b.moving.desired.distance_to(Fvector(0,0,8))>1.f);
    first.finalize();assert(!group.m_scout.assigned);
    // A new search episode can take the role; ending another NPC's search cannot release it.
    second.finalize();second.initialize();second.execute();assert(group.m_scout.owner==b.ID());
    first.finalize();assert(group.m_scout.assigned);second.finalize();
    stalker_combat_awareness::ScoutLease lease;
    assert(lease.claim(1,50,0xfffffff0u));assert(!lease.claim(2,50,100));
    assert(lease.claim(1,50,100));assert(!lease.claim(1,51,101));
    assert(lease.claim(2,51,7000));lease.remove(51);assert(!lease.assigned);
    lease.claim(2,50,8000);lease.remove(2);assert(!lease.assigned);
}
void delayed_sneak_decision()
{
    // Entry outside the final segment must not consume the choice before walking closer.
    CAgentMemberManager group;CAI_Stalker npc;CEntityAlive enemy;configure(npc,enemy,group);
    npc.memories.info.m_object_params.m_position=Fvector(0,0,12);
    CStalkerActionSearchEnemy action(&npc);action.initialize();action.execute();
    assert(npc.moving.body==eBodyStateStand);
    npc.pos=Fvector(0,0,4);Device.dwTimeGlobal+=1000;action.execute();
    assert(npc.moving.body==eBodyStateCrouch && npc.moving.type==eMovementTypeWalk);
    assert(npc.moving.desired.distance_to(Fvector(0,0,8))<.001f);
    assert(npc.memories.info.m_level_time==10000); // Walking and local events never refresh knowledge.
    action.finalize();

    // A failed route is still one decision per episode, not repeated geometry probes.
    configure(npc,enemy,group);npc.pos=Fvector();route=false;route_queries=0;
    action.initialize();action.execute();assert(route_queries==1);
    route=true;Device.dwTimeGlobal+=200;action.execute();
    assert(route_queries==1 && npc.moving.body==eBodyStateStand);action.finalize();

    // A rejected stable choice cannot reroll on a subsequent same-position report.
    configure(npc,enemy,group,1);npc.pos=Fvector();action.initialize();action.execute();
    assert(npc.moving.body==eBodyStateStand);
    npc.memories.info.m_level_time=11000;Device.dwTimeGlobal=11000;action.execute();
    assert(npc.moving.body==eBodyStateStand);action.finalize();

    // The previous seven-second window often expired during lookout/hold/detour.
    for(u32 age:{15000u,15001u}){
        configure(npc,enemy,group);npc.pos=Fvector();Device.dwTimeGlobal=10000+age;
        action.initialize();action.execute();
        assert((npc.moving.body==eBodyStateCrouch)==(age==15000u));action.finalize();
    }
}
void finish_sectors(CAI_Stalker& npc,CStalkerActionSearchEnemy& action)
{
    npc.pos=npc.memories.info.m_object_params.m_position;
    npc.moving.complete=true;action.execute();
    // Arrival at the remembered node and at each of the three local sectors.
    for(int point=0;point<4;++point){
        Device.dwTimeGlobal+=1500;action.execute(); // Finish bounded missing-motion pause.
        action.execute(); // Only now can the next leg begin.
        if(point<3){npc.pos=npc.moving.desired;npc.moving.complete=true;
            Device.dwTimeGlobal+=250;action.execute();}
    }
}
void search_patience()
{
    for(int scenario=0;scenario<4;++scenario){
        CAgentMemberManager group;CAI_Stalker npc;CEntityAlive enemy;configure(npc,enemy,group,1);
        CStalkerActionSearchEnemy action(&npc);action.initialize();action.execute();finish_sectors(npc,action);
        assert(!npc.memories.disabled); // Completing three sectors after 7.5 s is not forgetting.
        Device.dwTimeGlobal=39999;action.execute();assert(!npc.memories.disabled);
        const u32 routes=npc.moving.changes;
        if(scenario==0){
            // Repeated old reports and a local bullet event do not refresh enemy observation time.
            ++npc.near_miss_revision;npc.near_miss_recent=true;
            Device.dwTimeGlobal=40000;action.execute();assert(npc.memories.disabled);
        }else if(scenario==1){
            // A genuinely newer report extends patience from observation time, not receipt time.
            npc.memories.info.m_level_time=20000;action.execute();assert(!npc.memories.disabled);
            Device.dwTimeGlobal=49999;action.execute();assert(!npc.memories.disabled);
            Device.dwTimeGlobal=50000;action.execute();assert(npc.memories.disabled);
        }else if(scenario==2){
            npc.memories.info.m_level_time=9000;Device.dwTimeGlobal=40000;
            action.execute();assert(npc.memories.disabled); // Older relay is ignored.
        }else{
            npc.memories.visuals.record.squad=true;Device.dwTimeGlobal=90000;
            action.execute();assert(!npc.memories.disabled); // Live squad contact is retained.
        }
        assert(npc.moving.changes==routes); // Patience does not rebuild the finished route.
        action.finalize();
    }
    // Entering search late cannot restart the hard budget from the receipt/action time.
    CAgentMemberManager group;CAI_Stalker npc;CEntityAlive enemy;configure(npc,enemy,group,1);
    Device.dwTimeGlobal=54999;CStalkerActionSearchEnemy action(&npc);action.initialize();action.execute();
    assert(!npc.memories.disabled);
    Device.dwTimeGlobal=55000;action.execute();assert(npc.memories.disabled);action.finalize();
}
void search_doorway_route()
{
    CAgentMemberManager group;CAI_Stalker npc;CEntityAlive enemy;configure(npc,enemy,group,1);
    npc.memories.info.m_object_params.m_position=Fvector(0,0,16);
    CStalkerActionSearchEnemy action(&npc);action.initialize();action.execute();
    const u32 planned=npc.moving.changes;
    // A queued path can still report the previous path complete for the first tick.
    npc.moving.complete=true;Device.dwTimeGlobal+=100;action.execute();
    assert(npc.moving.changes==planned && npc.moving.desired.z==16.f);
    npc.moving.complete=false;
    // Progress around the wall, away from the target, must count too. No direct ray
    // to the hidden position is needed by this existing level navigation route.
    for(u32 seconds=1;seconds<=12;++seconds){
        Device.dwTimeGlobal=10000+seconds*1000;npc.pos.x=float(seconds);
        action.execute();assert(npc.moving.changes==planned && npc.moving.desired.z==16.f);
    }
    // Continued movement still has an absolute approach limit.
    Device.dwTimeGlobal=28000;npc.pos.x=18;action.execute();assert(npc.moving.changes>planned);
    action.finalize();
    configure(npc,enemy,group,1);npc.pos=Fvector();npc.memories.info.m_object_params.m_position.z=16;
    action.initialize();action.execute();const u32 stalled=npc.moving.changes;
    Device.dwTimeGlobal=14499;action.execute();assert(npc.moving.changes==stalled);
    Device.dwTimeGlobal=14500;action.execute();assert(npc.moving.changes>stalled);
    assert(npc.memories.info.m_level_time==10000);action.finalize();
}
void moving(CEntityAlive& other,u32 now,float x=1.f,float z=0.f)
{
    other.hidden=false;other.pos=Fvector(x,0,z);
    other.samples={{now-100,Fvector(x-.1f,0,z)},{now,other.pos}};
}
void peripheral()
{
    using stalker_combat_awareness::peripheral_motion;
    assert(peripheral_motion(1,0,1,0));assert(!peripheral_motion(1,-1,1,0));
    assert(!peripheral_motion(1,1,1,0));assert(!peripheral_motion(1,0,0,0));
    assert(!peripheral_motion(10,0,1,0));assert(!peripheral_motion(1,0,1,501));
    for(int scenario=0;scenario<7;++scenario){
        CAI_Stalker npc;CEntityAlive other;level.ObjectSpace=ObjectSpace();level.ObjectSpace.nearby={&other};
        Fvector look;CDangerObject grenade;
        for(u32 tick=0;tick<=750;tick+=250){Device.dwTimeGlobal=10000+tick;moving(other,Device.dwTimeGlobal);
            if(scenario==1)level.ObjectSpace.blocked=true;
            if(scenario==2)other.samples[0].vPosition=other.pos;
            if(scenario==3){other.pos=Fvector(0,0,-1);other.samples.back().vPosition=other.pos;}
            if(scenario==4)npc.script=true;
            if(scenario==5)npc.memories.dangers.danger=&grenade;
            if(scenario==6)npc.talking=true;
            npc.update_combat_peripheral();}
        assert(npc.combat_peripheral_look(look)==(scenario==0));
        assert(!npc.memories.enemies.target && !npc.memories.info.m_object); // No synthetic detection.
        if(scenario==0){const Fvector fixed=look;other.pos=Fvector(20,0,20);Device.dwTimeGlobal+=10;
            npc.update_combat_peripheral();assert(npc.combat_peripheral_look(look));assert(look.distance_to(fixed)==0);
            npc.memories.enemies.target=&other;npc.memories.visuals.seen=true;
            assert(!npc.combat_peripheral_look(look));}
        assert(level.ObjectSpace.rays<=4);
    }
    CAI_Stalker dead;dead.alive=false;dead.m_combat_peripheral.active=true;
    dead.m_combat_peripheral.looked=Device.dwTimeGlobal;Fvector unused;
    assert(!dead.combat_peripheral_look(unused)); // No memory access after death.
    CAI_Stalker crowded;CEntityAlive nearby[8];level.ObjectSpace=ObjectSpace();level.ObjectSpace.blocked=true;
    for(auto& other:nearby){moving(other,Device.dwTimeGlobal);level.ObjectSpace.nearby.push_back(&other);}
    crowded.update_combat_peripheral();assert(level.ObjectSpace.rays==4);
    // Two uninterrupted samples are needed; wall/absence resets suspicion.
    stalker_combat_awareness::Peripheral state;
    assert(!state.observe(0,2,true));assert(!state.observe(250,2,true));
    assert(!state.observe(500,2,false));assert(!state.observe(750,2,true));assert(state.observe(1100,2,true));
    assert(!state.observe(2000,3,true));assert(!state.looking(2100));
}
void close_pitch()
{
    CAI_Stalker npc;CSightManager sight{&npc};
    for(float d:{.01f,.3f,.5f,1.f,2.f,5.f}) for(float h:{.8f,1.4f,3.f}){
        Fvector target(0,h,d),direction;direction.sub(target,npc.eye_matrix.c);
        float yaw,pitch;sight.SetFirePointLookAngles(target,yaw,pitch);
        float expected_yaw,expected_pitch;direction.getHP(expected_yaw,expected_pitch);
        assert(std::fabs(yaw+expected_yaw)<.0001f && std::fabs(pitch+expected_pitch)<.0001f);
    }
    float yaw,pitch;sight.SetFirePointLookAngles(npc.eye_matrix.c,yaw,pitch);
    assert(std::isfinite(yaw)&&std::isfinite(pitch));
}
void search_listening_and_horizontal_sight()
{
    for(float eye_height:{.8f,1.5f,3.f}){
        CAgentMemberManager group;CAI_Stalker npc;CEntityAlive enemy;configure(npc,enemy,group,1);
        npc.memories.info.m_object_params.m_position=Fvector(0,.9f,8); // Real memory Center Y.
        npc.pos=Fvector(0,0,8);npc.eye_matrix.c=Fvector(0,eye_height,8);
        npc.animations.available=true;
        CStalkerActionSearchEnemy action(&npc);action.initialize();action.execute();
        assert(npc.animations.active && npc.animations.starts==1 && npc.moving.type==eMovementTypeStand);
        assert(npc.sights.last.y==eye_height && npc.sights.last.z>npc.pos.z+1.f);
        const u32 legs=npc.moving.changes;
        npc.eye_matrix.c.y=eye_height+.5f; // Real crouch-to-stand raises the eyes during the phase.
        Device.dwTimeGlobal+=3000;action.execute();
        assert(npc.sights.last.y==npc.eye_matrix.c.y);
        assert(npc.animations.starts==1 && npc.moving.changes==legs); // No timed sector advance during listen.
        ++npc.memories.info.m_level_time;action.execute();
        assert(npc.animations.starts==1); // Same-position relay cannot replay the motion.
        npc.animations.active=false;action.execute();action.execute();
        assert(npc.moving.changes==legs+1 && npc.moving.type==eMovementTypeWalk);
        Device.dwTimeGlobal+=3000;npc.pos.x=-1;action.execute();
        assert(npc.moving.changes==legs+1 && npc.animations.starts==1); // Travelling is not arrival.
        npc.pos=npc.moving.desired;npc.moving.complete=true;Device.dwTimeGlobal+=250;action.execute();
        assert(npc.animations.starts==2 && npc.animations.active);
        assert(npc.sights.last.y==npc.eye_matrix.c.y && npc.sights.last.distance_to_xz_sqr(npc.eye_matrix.c)>1.f);
        action.finalize();assert(!npc.animations.active);
    }
}
void listening_interruptions()
{
    for(int cause=0;cause<11;++cause){
        CAgentMemberManager group;CAI_Stalker npc;CEntityAlive enemy;configure(npc,enemy,group,1);
        npc.pos=Fvector(0,0,8);npc.animations.available=true;
        CStalkerActionSearchEnemy action(&npc);action.initialize();action.execute();
        assert(npc.animations.active);CDangerObject grenade;
        Device.dwTimeGlobal+=300;
        switch(cause){
        case 0:npc.memories.visuals.seen=true;break;
        case 1:npc.memories.dangers.danger=&grenade;break;
        case 2:++npc.memories.hits.revision;break;
        case 3:++npc.near_miss_revision;break;
        case 4:npc.memories.info.m_object_params.m_position.z+=4;++npc.memories.info.m_level_time;break;
        case 5:npc.memories.enemies.target=nullptr;break;
        case 6:npc.alive=false;break;
        case 7:npc.animations.scripts.push_back(7);break;
        case 8:npc.condition.hp=.5f;break;
        case 9:Device.dwTimeGlobal+=6500;break;
        case 10:npc.script=true;break;
        }
        action.execute();assert(!npc.animations.active);
        if(cause==7)assert(npc.animations.scripts.size()==1); // Do not consume another controller's motion.
        action.finalize();
    }
    // A failed path never plays an arrival motion. Missing clips have a bounded pause.
    CAgentMemberManager group;CAI_Stalker npc;CEntityAlive enemy;configure(npc,enemy,group,1);
    CStalkerActionSearchEnemy action(&npc);action.initialize();action.execute();
    npc.moving.level.fail=true;route=false;Device.dwTimeGlobal+=300;action.execute();
    assert(npc.animations.starts==0);action.finalize();
    configure(npc,enemy,group,1);npc.moving.level.fail=false;npc.pos=Fvector(0,0,8);
    action.initialize();action.execute();assert(npc.animations.starts==1 && !npc.animations.active);
    const u32 legs=npc.moving.changes;
    Device.dwTimeGlobal+=1499;action.execute();assert(npc.moving.changes==legs);
    ++Device.dwTimeGlobal;action.execute();action.execute();assert(npc.moving.changes==legs+1);
    action.finalize();
}
int main(){search_lifecycle();search_cancellation();group_scout();delayed_sneak_decision();search_patience();search_doorway_route();search_listening_and_horizontal_sight();listening_interruptions();peripheral();close_pitch();
 std::cout<<"PASS: production search travel/listen progression, horizontal sight, animation cancellation/fallback, memory patience, scout lease and peripheral perception\n";}
