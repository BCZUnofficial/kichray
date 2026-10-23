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
    assert(npc.moving.type==eMovementTypeStand && npc.moving.body==eBodyStateCrouch);
    Device.dwTimeGlobal+=1000;action.execute();assert(npc.moving.body==eBodyStateStand);
    assert(!group.m_scout.assigned);action.finalize();
    assert(stalker_known_enemy_distance(npc,&enemy)==4.f);
    npc.memories.info.m_object=nullptr;assert(stalker_known_enemy_distance(npc,&enemy)==flt_max);
    // A failed, unsafe or old observation never enters crouch approach.
    for(int mode=0;mode<4;++mode){configure(npc,enemy,group);action.initialize();
        if(mode==0)route=false;
        if(mode==1)npc.memories.info.m_level_time=1000;
        if(mode==2)npc.memories.visuals.record.squad=true;
        if(mode==3)space=false;
        action.execute();assert(npc.moving.body==eBodyStateStand);action.finalize();
        npc.memories.visuals.record.squad=false;}
}
void search_cancellation()
{
    for(int cause=0;cause<9;++cause){
        CAgentMemberManager group;group.grouped=true;CAI_Stalker npc;CEntityAlive enemy;configure(npc,enemy,group);
        CDangerObject grenade;CStalkerActionSearchEnemy action(&npc);action.initialize();action.execute();
        assert(npc.moving.body==eBodyStateCrouch && group.m_scout.assigned);
        Device.dwTimeGlobal+=300;
        switch(cause){case 0:++npc.memories.hits.revision;break;case 1:npc.condition.hp=.8f;break;
        case 2:npc.memories.dangers.danger=&grenade;break;case 3:npc.moving.level.fail=true;break;
        case 4:npc.memories.visuals.seen=true;break;case 5:npc.memories.enemies.target=nullptr;break;
        case 6:npc.memories.info.m_object=nullptr;break;case 7:Device.dwTimeGlobal+=6000;break;
        case 8:npc.memories.info.m_object_params.m_position.x=3;++npc.memories.info.m_level_time;break;}
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
int main(){search_lifecycle();search_cancellation();group_scout();peripheral();close_pitch();
 std::cout<<"PASS: production cautious search, squad lease, cancellation, peripheral occlusion/delay and close pitch\n";}
