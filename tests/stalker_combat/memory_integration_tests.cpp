#include "memory_fixture.h"
#include "memory_production.inc"
#include <limits>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(std::string(__func__)+": " #condition); } while (0)
static bool near(float a, float b) { return std::fabs(a-b)<.001f; }

static void helper_boundaries()
{
    using namespace stalker_combat_memory;
    CHECK(saved_age(10, 20)==0);
    CHECK(saved_age(20, 10)==10);
    CHECK(restored_timestamp(5, 100)==0);
    CHECK(restored_timestamp(100, 5)==95);
    CHECK(interval_elapsed(10, 100, 500));
    CHECK(!interval_elapsed(1000, 600, 500));
    CHECK(interval_elapsed(1100, 600, 500));
    CHECK(near(recent_hit_weight(3500, 1000, 5000), .5f));
    CHECK(recent_hit_weight(999, 1000, 5000)==0.f);
    CHECK(recent_hit_weight(6000, 1000, 5000)==0.f);
    CHECK(recent_hit_weight(1000, 1000, 0)==0.f);
    CHECK(meaningful_threat_improvement(9000, 8749, 250));
    CHECK(!meaningful_threat_improvement(9000, 8750, 250));
    CHECK(!meaningful_threat_improvement(9000, std::numeric_limits<float>::quiet_NaN(), 250));
    CHECK(!relay_is_newer(1000, 2000, true));
    CHECK(!relay_is_newer(2000, 1000, false));
    CHECK(relay_is_newer(1000, 2000, false));
}

static void timestamp_packets()
{
    typedef void (*Save)(Packet&, const Record&, bool);
    typedef void (*Load)(Packet&, Record&, bool);
    const Save saves[]={saveVisual, saveSound, saveHit};
    const Load loads[]={loadVisual, loadSound, loadHit};
    for (unsigned kind=0; kind<3; ++kind) {
        Device.dwTimeGlobal=100000;
        Record original;
        original.m_level_time=90000;
        original.m_last_level_time=80000;
        original.m_first_level_time=70000;
        Packet packet;
        saves[kind](packet, original, true);
        Packet fields=packet;
        CHECK(fields.r_u32()==10000);
        CHECK(fields.r_u32()==20000);
#ifdef USE_FIRST_LEVEL_TIME
        CHECK(fields.r_u32()==30000);
        CHECK(packet.bytes.size()==12);
#else
        CHECK(packet.bytes.size()==8);
#endif
        CHECK(fields.cursor==fields.bytes.size());

        Device.dwTimeGlobal=200000;
        Record restored;
        // Loading a stalker must not VERIFY the old destination fields before reading the packet.
        restored.m_level_time=restored.m_last_level_time=restored.m_first_level_time=u32(-1);
        loads[kind](packet, restored, true);
        CHECK(restored.m_level_time==190000);
        CHECK(restored.m_last_level_time==180000);
#ifdef USE_FIRST_LEVEL_TIME
        CHECK(restored.m_first_level_time==170000);
#endif
        CHECK(packet.cursor==packet.bytes.size());
        Packet resaved;
        saves[kind](resaved, restored, true);
        CHECK(resaved.bytes==packet.bytes);

        Device.dwTimeGlobal=5000;
        packet.cursor=0;
        loads[kind](packet, restored, true);
        CHECK(restored.m_level_time==0);
        CHECK(restored.m_last_level_time==0);
#ifdef USE_FIRST_LEVEL_TIME
        CHECK(restored.m_first_level_time==0);
#endif

        Device.dwTimeGlobal=200000;
        Record legacy;
        packet.cursor=0;
        loads[kind](packet, legacy, false);
        CHECK(legacy.m_level_time==210000);
        CHECK(legacy.m_last_level_time==220000);
#ifdef USE_FIRST_LEVEL_TIME
        CHECK(legacy.m_first_level_time==230000);
#endif

        Device.dwTimeGlobal=100000;
        original.m_last_level_time=original.m_first_level_time=120000;
        Packet future;
        saves[kind](future, original, true);
        CHECK(future.r_u32()==10000);
        CHECK(future.r_u32()==0);
#ifdef USE_FIRST_LEVEL_TIME
        CHECK(future.r_u32()==0);
#endif
    }
}

static void target_eligibility()
{
    CAI_Stalker owner(10);
    CMemoryManager memory(&owner);
    CEnemyManager enemies(&owner);
    CEntityAlive rejected(11), accepted(12);
    rejected.visible=accepted.visible=true;
    Device.dwTimeGlobal=10001;
    memory.visual().begin_test_update();
    memory.visual().add_visible_object(&rejected, .1f);
    memory.visual().add_visible_object(&accepted, .1f);
    enemies.m_selected=&rejected;
    enemies.m_objects.push_back(&accepted);
    enemies.m_last_enemy_change=10000;
    enemies.allow_change=false;
    enemies.try_change_enemy();
    CHECK(enemies.selected()==&accepted);
    CHECK(owner.changes==1);
    enemies.m_objects.clear();
    enemies.try_change_enemy();
    CHECK(enemies.selected()==0);
    CHECK(owner.changes==2);

    enemies.m_objects.push_back(&accepted);
    enemies.m_objects.push_back(&rejected);
    enemies.m_selected=&accepted;
    rejected.score=1000;
    const unsigned evaluated=enemies.evaluations;
    enemies.try_change_enemy();
    CHECK(enemies.selected()==&accepted);
    CHECK(enemies.evaluations==evaluated);

    CAI_Stalker wounded(13);
    wounded.visible=true;
    wounded.is_wounded=true;
    memory.visual().add_visible_object(&wounded, .1f);
    enemies.m_objects.clear();
    enemies.m_objects.push_back(&wounded);
    enemies.m_objects.push_back(&accepted);
    enemies.m_selected=&wounded;
    enemies.allow_change=true;
    enemies.try_change_enemy();
    CHECK(enemies.selected()==&accepted);
    CHECK(enemies.m_objects.size()==1);
}

static void threat_cadence_and_inertia()
{
    CAI_Stalker owner(20);
    CMemoryManager memory(&owner);
    CEnemyManager enemies(&owner);
    CEntityAlive current(21), candidate(22);
    current.visible=candidate.visible=true;
    current.position=Fvector(40,0,0);
    candidate.position=Fvector(30,0,0);
    Device.dwTimeGlobal=10000;
    memory.visual().begin_test_update();
    memory.visual().add_visible_object(&current, .1f);
    memory.visual().add_visible_object(&candidate, .1f);
    current.score=9000; candidate.score=8500;
    enemies.m_objects.push_back(&current);
    enemies.m_objects.push_back(&candidate);
    enemies.m_selected=&current;
    enemies.m_last_threat_check=10000;
    enemies.m_last_enemy_change=10000;
    Device.dwTimeGlobal=10499;
    enemies.try_change_enemy();
    CHECK(enemies.evaluations==0);
    Device.dwTimeGlobal=10500;
    enemies.try_change_enemy();
    CHECK(enemies.evaluations>0);
    CHECK(enemies.selected()==&current); // a better distant target still respects inertia

    Device.dwTimeGlobal=14000;
    candidate.score=8800;
    enemies.try_change_enemy();
    CHECK(enemies.selected()==&current); // margin prevents aim thrashing after inertia
    Device.dwTimeGlobal=14500;
    candidate.score=8500;
    enemies.try_change_enemy();
    CHECK(enemies.selected()==&candidate);

    enemies.m_selected=&current;
    enemies.m_last_enemy_change=enemies.m_last_threat_check=15000;
    candidate.position=Fvector(2,0,0);
    Device.dwTimeGlobal=15500;
    enemies.try_change_enemy();
    CHECK(enemies.selected()==&candidate); // directly visible close threat breaks inertia

    enemies.m_selected=&current;
    enemies.m_last_enemy_change=16000;
    current.visible=false;
    current.position_forbidden=true;
    candidate.position=Fvector(30,0,0);
    Device.dwTimeGlobal=16001;
    memory.visual().begin_test_update();
    memory.visual().add_visible_object(&candidate, .1f);
    enemies.try_change_enemy();
    CHECK(enemies.selected()==&candidate); // gained sight can react immediately

    // Non-stalker path retains its original visible-target lock even if not in candidates.
    enemies.m_stalker=0;
    memory.visual().m_stalker=false;
    enemies.m_selected=&current;
    current.visible=true;
    memory.visual().visible_object(&current)->m_visible.assign(1);
    enemies.m_objects.clear();
    enemies.m_objects.push_back(&candidate);
    enemies.try_change_enemy();
    CHECK(enemies.selected()==&current);
}

static void real_hit_priority_and_attribution()
{
    CAI_Stalker owner(30);
    CMemoryManager memory(&owner);
    CEnemyManager enemies(&owner);
    CEntityAlive current(31), attacker(32), heard(33);
    current.visible=attacker.visible=true;
    current.position=Fvector(40,0,0); attacker.position=Fvector(30,0,0);
    Device.dwTimeGlobal=20000;
    memory.visual().begin_test_update();
    memory.visual().add_visible_object(&current, .1f);
    memory.visual().add_visible_object(&attacker, .1f);
    current.score=9000; attacker.score=8500;
    enemies.m_objects.push_back(&current);
    enemies.m_objects.push_back(&attacker);
    enemies.m_selected=&current;
    enemies.m_last_enemy_change=enemies.m_last_threat_check=20000;
    Device.dwTimeGlobal=20001;
    memory.hit().record_test_hit(&attacker, 10.f);
    CHECK(memory.hit().m_last_hit_object_id==attacker.ID());
    CHECK(memory.hit().recent_hit_weight(&attacker)==1.f);
    enemies.try_change_enemy();
    CHECK(enemies.selected()==&attacker); // new actual damage bypasses cadence and inertia
    const u32 revision=memory.hit().recent_hit_revision();
    memory.hit().record_test_hit(&heard, 0.f);
    CHECK(memory.hit().m_last_hit_object_id==attacker.ID());
    CHECK(memory.hit().recent_hit_revision()==revision);
    CHECK(memory.hit().recent_hit_weight(&heard)==0.f);
    Device.dwTimeGlobal=22501;
    CHECK(near(memory.hit().recent_hit_weight(&attacker), .5f));
    Device.dwTimeGlobal=25001;
    CHECK(memory.hit().recent_hit_weight(&attacker)==0.f);
    CHECK(memory.hit().m_last_hit_object_id==attacker.ID());
    memory.hit().m_stalker=false;
    memory.hit().record_test_hit(&heard, 10.f);
    CHECK(memory.hit().m_last_hit_object_id==heard.ID());
    CHECK(memory.hit().recent_hit_revision()==revision);
}

static MemorySpace::CMemoryInfo report_for(const CEntityAlive& enemy, u32 time, float x)
{
    MemorySpace::CMemoryInfo report;
    report.m_object=&enemy;
    report.m_level_time=time;
    report.m_last_level_time=time-1;
    report.m_object_params.m_position=Fvector(x,4,5);
    report.m_object_params.m_level_vertex_id=123;
    report.m_visual_info=true;
    report.m_visible.assign(32);
    report.m_squad_mask.assign(32);
    return report;
}

static void shared_snapshot_and_provenance()
{
    CAI_Stalker owner(40);
    owner.position=Fvector(2,0,0);
    CMemoryManager memory(&owner);
    CEntityAlive hidden(41);
    hidden.position=Fvector(1000,0,0);
    hidden.position_forbidden=true;
    Device.dwTimeGlobal=10000;
    MemorySpace::CMemoryInfo report=report_for(hidden, 5000, 10);
    CHECK(memory.share_enemy_memory(report, 42));
    CVisibleObject* stored=memory.visual().visible_object(&hidden);
    CHECK(stored);
    CHECK(stored->m_level_time==5000);
    CHECK(stored->m_last_level_time==4999);
    CHECK(stored->m_object_params.m_position.x==10);
    CHECK(stored->m_object_params.m_level_vertex_id==123);
    CHECK(stored->m_self_params.m_position.x==2);
    CHECK(stored->m_visible.get()==0);
    CHECK(stored->m_squad_mask.get()==1);
    CHECK(memory.visual().direct_visible_time(&hidden)==u32(-1));

    report.m_visual_info=false; report.m_sound_info=true;
    report.m_level_time=6000; report.m_object_params.m_position.x=20;
    CHECK(memory.share_enemy_memory(report, 42));
    CHECK(stored->m_level_time==6000);
    CHECK(stored->m_object_params.m_position.x==20);
    CHECK(memory.visual().direct_visible_time(&hidden)==u32(-1));
    stored->m_enabled=false;
    CHECK(!memory.share_enemy_memory(report, 42));
    CHECK(!stored->m_enabled); // repeating the exhausted clue cannot revive it
    report.m_level_time=7000;
    CHECK(memory.share_enemy_memory(report, 42));
    CHECK(stored->m_enabled);

    stored->m_level_time=8000;
    stored->m_object_params.m_position.x=30;
    CHECK(memory.share_enemy_memory(report, 42));
    CHECK(stored->m_level_time==8000 && stored->m_object_params.m_position.x==30);
    stored->m_visible.assign(8); // another member of our shared squad sees it now
    report.m_level_time=9000;
    CHECK(memory.share_enemy_memory(report, 42));
    CHECK(stored->m_level_time==8000 && stored->m_visible.get()==8);

    report.m_level_time=10001;
    CHECK(!memory.share_enemy_memory(report, 42));
    report.m_level_time=9000;
    hidden.alive=false;
    CHECK(!memory.share_enemy_memory(report, 42));
    hidden.alive=true; hidden.enemy=false;
    CHECK(!memory.share_enemy_memory(report, 42));
    hidden.enemy=true; memory.m_stalker=false;
    CHECK(!memory.share_enemy_memory(report, 42));
}

static void direct_sight_ledger()
{
    CAI_Stalker owner(50);
    CMemoryManager memory(&owner);
    CVisualMemoryManager& visual=memory.visual();
    CEntityAlive seen(51), heard(52), second(53), third(54);
    Device.dwTimeGlobal=1000;
    CHECK(visual.direct_visible_time(&seen)==u32(-1));
    visual.add_visible_object(&seen, .1f);
    CHECK(visual.direct_visible_time(&seen)==u32(-1));
    seen.visible=true;
    visual.add_visible_object(&seen, .1f);
    CHECK(visual.direct_visible_time(&seen)==1000);
    Device.dwTimeGlobal=1100;
    visual.add_visible_object(&heard, .1f, true);
    CHECK(visual.direct_visible_time(&heard)==u32(-1));
    seen.visible=false;
    visual.add_visible_object(&seen, .1f, true);
    CHECK(visual.direct_visible_time(&seen)==1000);
    MemorySpace::CMemoryInfo report=report_for(seen, 1100, 20);
    CHECK(memory.share_enemy_memory(report, 55));
    CHECK(visual.direct_visible_time(&seen)==1000);
    visual.forget_direct_sight(&seen);
    CHECK(visual.direct_visible_time(&seen)==u32(-1));

    visual.m_max_object_count=2;
    Device.dwTimeGlobal=2000; visual.remember_direct_sight(&seen);
    Device.dwTimeGlobal=2100; visual.remember_direct_sight(&second);
    Device.dwTimeGlobal=2200; visual.remember_direct_sight(&seen);
    Device.dwTimeGlobal=2300; visual.remember_direct_sight(&third);
    CHECK(visual.m_direct_sights.size()==2);
    CHECK(visual.direct_visible_time(&seen)==2200);
    CHECK(visual.direct_visible_time(&second)==u32(-1));
    CHECK(visual.direct_visible_time(&third)==2300);
    visual.m_direct_sights.clear();
    CHECK(visual.direct_visible_time(&third)==u32(-1));
    visual.m_stalker=false;
    visual.remember_direct_sight(&third);
    CHECK(visual.m_direct_sights.empty());
    visual.m_stalker=true;
    visual.m_max_object_count=1000;
    std::vector<CEntityAlive> many(80);
    for (unsigned i=0; i<many.size(); ++i) {
        Device.dwTimeGlobal=3000+i;
        visual.remember_direct_sight(&many[i]);
    }
    CHECK(visual.m_direct_sights.size()==64);
}

static void gradual_suspicion()
{
    CAI_Stalker owner(60);
    CMemoryManager memory(&owner);
    CVisualMemoryManager& visual=memory.visual();
    Device.dwTimeGlobal=5000;
    CNotYetVisibleObject partial={60.f, 4990};
    CNotYetVisibleObject current={60.f, 5000};
    visual.m_not_yet_visible_objects.push_back(partial);
    visual.m_not_yet_visible_objects.push_back(current);
    visual.decay_test_update(.25f);
    CHECK(near(visual.m_not_yet_visible_objects[0].m_value, 40.f));
    CHECK(near(visual.m_not_yet_visible_objects[1].m_value, 60.f));
    visual.decay_test_update(.5f);
    CHECK(visual.m_not_yet_visible_objects[0].m_value==0.f);
    visual.m_not_yet_visible_objects[0].m_value=120.f;
    visual.decay_test_update(1.5f);
    CHECK(visual.m_not_yet_visible_objects[0].m_value==0.f);
    visual.m_not_yet_visible_objects[0].m_value=60.f;
    visual.m_stalker=false;
    visual.decay_test_update(.001f);
    CHECK(visual.m_not_yet_visible_objects[0].m_value==0.f);
    const float once=stalker_combat_memory::decay_suspicion(120.f, 120.f, .5f, 1.5f);
    float split=120.f;
    for (unsigned i=0;i<5;++i) split=stalker_combat_memory::decay_suspicion(split, 120.f, .1f, 1.5f);
    CHECK(near(once,split));
}

static void squad_visibility_is_personal()
{
    CAI_Stalker observer_a(70), observer_b(71);
    CMemoryManager memory_a(&observer_a), memory_b(&observer_b);
    CVisualMemoryManager& a=memory_a.visual();
    CVisualMemoryManager& b=memory_b.visual();
    b.m_objects=a.m_objects; // the real squad architecture shares this record storage
    b.recipient_mask=2;
    a.params.m_still_visible_time=2000;
    CEntityAlive enemy(72), close_enemy(73);
    enemy.visible=true;
    enemy.position=Fvector(40,0,0);
    Device.dwTimeGlobal=1000;
    a.begin_test_update(); a.add_visible_object(&enemy, .1f);
    b.begin_test_update(); b.add_visible_object(&enemy, .1f);
    CHECK(a.visible_right_now(&enemy));
    CHECK(a.visible_now(&enemy));
    CHECK(b.visible_right_now(&enemy));
    CHECK(a.visible_object(&enemy)->m_visible.get()==3);

    // A loses sight in a second update at the very same global timestamp.
    a.begin_test_update();
    enemy.position_forbidden=true;
    CHECK(!a.visible_right_now(&enemy));
    CHECK(a.visible_now(&enemy)); // personal grace is retained, but is not direct visibility
    CEnemyManager enemies(&observer_a);
    CHECK(near(enemies.evaluate(&enemy), 10016.f)); // 40m last-known position, no live query

    // A close visible target can break inertia while the previous target only has grace.
    close_enemy.visible=true; close_enemy.position=Fvector(2,0,0);
    a.add_visible_object(&close_enemy, .1f);
    enemy.score=9000; close_enemy.score=8000;
    enemies.m_objects.push_back(&enemy); enemies.m_objects.push_back(&close_enemy);
    enemies.m_selected=&enemy;
    enemies.m_last_enemy_change=1000;
    enemies.try_change_enemy();
    CHECK(enemies.selected()==&close_enemy);

    // B continues observing for ten seconds. Shared timestamps cannot refresh A's sight.
    for (unsigned second=1; second<=10; ++second) {
        Device.dwTimeGlobal=1000+1000*second;
        enemy.position_forbidden=false;
        enemy.position.x=40.f+second;
        b.begin_test_update(); b.add_visible_object(&enemy, .1f);
        a.begin_test_update();
        enemy.position_forbidden=true;
        CHECK(!a.visible_right_now(&enemy));
        CHECK(a.visible_now(&enemy)==(second<=2));
        CHECK(a.direct_visible_time(&enemy)==1000);
        CHECK(b.direct_visible_time(&enemy)==Device.dwTimeGlobal);
        const float expected=10000.f+_sqr(40.f+second)/100.f;
        CHECK(near(enemies.evaluate(&enemy), expected));
        CHECK(memory_a.share_enemy_memory(memory_b.memory(&enemy), observer_b.ID()));
        CHECK(!a.visible_right_now(&enemy));
        CHECK(a.direct_visible_time(&enemy)==1000);
    }
    CHECK((a.visible_object(&enemy)->m_visible.get()&a.mask())==0);
    CHECK(Device.dwTimeGlobal-a.direct_visible_time(&enemy)>3000);

    // Disabling vision invalidates the current confirmation immediately, including before update.
    Device.dwTimeGlobal=12000;
    enemy.position_forbidden=false;
    a.begin_test_update(); a.add_visible_object(&enemy, .1f);
    CHECK(a.visible_right_now(&enemy));
    a.enable(false);
    CHECK(!a.visible_right_now(&enemy));
    CHECK(!a.visible_now(&enemy));
    a.enable(true);
    CHECK(!a.visible_right_now(&enemy)); // a quick toggle cannot resurrect direct sight
    CHECK(a.direct_visible_time(&enemy)==12000); // nor refresh the suppression timestamp
    a.enable(false);
    Device.dwTimeGlobal=16000;
    enemy.position_forbidden=true;
    a.begin_test_update(); // production early return when disabled
    CHECK(!a.visible_right_now(&enemy));
    CHECK(!a.visible_now(&enemy));
    a.enable(true);
    CHECK(!a.visible_right_now(&enemy));
    CHECK(!a.visible_now(&enemy)); // personal grace has also expired
    CHECK(a.direct_visible_time(&enemy)==12000);
    enemy.position_forbidden=false;
    a.begin_test_update(); a.add_visible_object(&enemy, .1f);
    CHECK(a.visible_right_now(&enemy)); // a new real sensor pass restores direct sight
    a.enable(true);
    CHECK(a.visible_right_now(&enemy)); // assigning the existing state is harmless

    // Loading leaves saved visible masks but deliberately clears all personal confirmations.
    a.visible_object(&enemy)->m_visible.set(a.mask(),true);
    a.m_direct_sights.clear();
    CHECK(!a.visible_right_now(&enemy));
    CHECK(!a.visible_now(&enemy));

    // Shared managers for non-stalkers preserve their old timestamp/mask interpretation.
    a.m_stalker=false;
    a.enable(false);
    a.begin_test_update();
    CHECK(a.visible_right_now(&enemy));
    CHECK(a.visible_now(&enemy));
}

int main()
{
    try {
        helper_boundaries();
        timestamp_packets();
        target_eligibility();
        threat_cadence_and_inertia();
        real_hit_priority_and_attribution();
        shared_snapshot_and_provenance();
        direct_sight_ledger();
        gradual_suspicion();
        squad_visibility_is_personal();
        std::puts("PASS: 9 production memory regression groups");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what());
        return 1;
    }
}
