#include "fire_burst_fixture.h"
#include <iostream>
#include <cstring>

TestDevice Device;
FixtureRandom Random;
namespace stalker_combat_diagnostics
{
bool enabled(unsigned int) { return false; }
void log(unsigned int, Event, const char*, const char*, ...) {}
}
using namespace MonsterSpace;
using namespace ObjectHandlerSpace;
#include "fire_gate_production.inc"
#include "fire_burst_production.inc"

namespace
{
void prepare(CAI_Stalker& npc, CEntityAlive& enemy, CWeaponMagazined& weapon, bool hidden)
{
    ++Device.dwFrame;
    Device.dwTimeGlobal = 1100;
    npc.memories.enemies.value = &enemy;
    npc.items.active = &weapon;
    auto& record = npc.memories.visuals.records[enemy.ID()];
    record.direct_time = 1000;
    record.shared_time = 1000;
    record.visible = !hidden;
    record.has_snapshot = true;
    record.snapshot.m_object_params.m_position = Fvector(0.f, 0.f, 10.f);
    auto& known = npc.memories.known[enemy.ID()];
    known.m_object = &enemy;
    known.m_object_params.m_position = record.snapshot.m_object_params.m_position;
    enemy.live_reads_allowed = !hidden;
}
void expect_goal(const CObjectHandlerPlanner& planner, const CWeapon& weapon, EWorldProperties value)
{ assert(planner.goal == planner.uid(weapon.ID(), value)); }

void test_remaining_is_not_absolute_queue_length()
{
    CAI_Stalker npc;
    CEntityAlive enemy(10);
    CWeaponMagazined weapon(20, npc);
    CObjectHandlerPlanner planner(npc);
    CStalkerActionCombatBase action(npc);
    prepare(npc, enemy, weapon, true);
    action.fire();
    expect_goal(planner, weapon, eWorldPropertyFiring1);
    assert(weapon.GetQueueSize() == 8);
    weapon.start_burst();
    weapon.advance_for_test(5);
    assert(weapon.ShotsFired() == 5 && npc.m_combat_suppression_shots == 5);
    assert(!weapon.StopedAfterQueueFired() && npc.combat_suppression_remaining() == 3);
    action.fire();
    assert(weapon.GetQueueSize() == 8); // Old code wrote 3 below the live shot count of 5.
    weapon.advance_for_test(20);
    assert(weapon.ShotsFired() == 8 && weapon.StopedAfterQueueFired());
    assert(npc.m_combat_suppression_shots == 8 && weapon.ammo == 22);
    action.fire();
    expect_goal(planner, weapon, eWorldPropertyAimingReady1);
    assert(!weapon.IsWorking());
    weapon.advance_for_test(20);
    assert(npc.m_combat_suppression_shots == 8 && weapon.ammo == 22);
}

void test_completed_queue_is_not_reopened_by_next_profile()
{
    CAI_Stalker npc;
    CEntityAlive enemy(10);
    CWeaponMagazined weapon(20, npc);
    CObjectHandlerPlanner planner(npc);
    CStalkerActionCombatBase action(npc);
    prepare(npc, enemy, weapon, true);
    npc.fixture_min_queue = npc.fixture_max_queue = 3;
    action.fire();
    weapon.start_burst();
    weapon.advance_for_test(10);
    assert(weapon.ShotsFired() == 3 && weapon.StopedAfterQueueFired());

    npc.fixture_min_queue = npc.fixture_max_queue = 12;
    action.fire();
    assert(weapon.GetQueueSize() == 5); // Fresh burst gets the remaining allowance, without old +3.
    assert(weapon.StopedAfterQueueFired() && !weapon.IsWorking());
    weapon.advance_for_test(10); // Before the ordinary queue wait finishes, no extension is possible.
    assert(weapon.ShotsFired() == 3 && npc.m_combat_suppression_shots == 3);
    weapon.start_burst(); // Model the object planner starting the next burst after its wait.
    weapon.advance_for_test(10);
    assert(weapon.ShotsFired() == 5 && weapon.StopedAfterQueueFired());
    assert(npc.m_combat_suppression_shots == 8);
    action.fire();
    expect_goal(planner, weapon, eWorldPropertyAimingReady1);
    assert(!weapon.IsWorking());
}

void test_completion_boundary_and_live_profile_shrink()
{
    CAI_Stalker npc;
    CEntityAlive enemy(10);
    CWeaponMagazined weapon(20, npc);
    CObjectHandlerPlanner planner(npc);
    CStalkerActionCombatBase action(npc);
    prepare(npc, enemy, weapon, true);
    npc.fixture_min_queue = npc.fixture_max_queue = 6;
    action.fire();
    weapon.start_burst();
    weapon.advance_for_test(6);
    // Exercise the exact counter boundary before a caller observes the stop flag.
    weapon.StopedAfterQueueFired(false);
    action.fire();
    assert(weapon.GetQueueSize() == 2);
    assert(weapon.StopedAfterQueueFired() && !weapon.IsWorking());
    weapon.advance_for_test(10);
    assert(npc.m_combat_suppression_shots == 6);
    weapon.start_burst();
    weapon.advance_for_test(10);
    assert(weapon.ShotsFired() == 2 && weapon.StopedAfterQueueFired());
    assert(npc.m_combat_suppression_shots == 8);

    CAI_Stalker visible;
    CEntityAlive visible_enemy(11);
    CWeaponMagazined visible_weapon(21, visible);
    CObjectHandlerPlanner visible_planner(visible);
    CStalkerActionCombatBase visible_action(visible);
    prepare(visible, visible_enemy, visible_weapon, false);
    visible.fixture_min_queue = visible.fixture_max_queue = 8;
    visible_action.fire();
    visible_weapon.start_burst();
    visible_weapon.advance_for_test(5);
    visible.fixture_min_queue = visible.fixture_max_queue = 2;
    visible_action.fire();
    assert(visible_weapon.GetQueueSize() == 5); // Never write a live threshold below ShotsFired.
    visible_weapon.advance_for_test(10);
    assert(visible_weapon.ShotsFired() == 5 && visible_weapon.StopedAfterQueueFired());
}

void test_zero_allowance_and_lost_target_stop_fire_goal()
{
    CAI_Stalker npc;
    CEntityAlive enemy(10);
    CWeaponMagazined weapon(20, npc);
    CObjectHandlerPlanner planner(npc);
    CStalkerActionCombatBase action(npc);
    prepare(npc, enemy, weapon, true);
    weapon.ammo = 2;
    weapon.m_magazine.resize(2);
    action.fire();
    assert(weapon.GetQueueSize() == 1);
    weapon.start_burst();
    weapon.advance_for_test(10);
    assert(weapon.ammo == 1 && npc.combat_suppression_remaining() == 0);
    action.fire();
    expect_goal(planner, weapon, eWorldPropertyAimingReady1);
    assert(!weapon.IsWorking());
    weapon.advance_for_test(10);
    assert(weapon.ammo == 1);

    CAI_Stalker other;
    CEntityAlive other_enemy(11);
    CWeaponMagazined other_weapon(21, other);
    CObjectHandlerPlanner other_planner(other);
    CStalkerActionCombatBase other_action(other);
    prepare(other, other_enemy, other_weapon, false);
    other_action.fire();
    other_weapon.start_burst();
    other_weapon.advance_for_test(1);
    other.memories.enemies.value = nullptr;
    other_action.fire();
    expect_goal(other_planner, other_weapon, eWorldPropertyAimingReady1);
    assert(!other_weapon.IsWorking());
}

void test_ally_entering_live_queue_stops_before_shot_and_can_resume()
{
    CAI_Stalker npc;
    CEntityAlive enemy(10), ally(11);
    CWeaponMagazined weapon(20, npc);
    CObjectHandlerPlanner planner(npc);
    CStalkerActionCombatBase action(npc);
    prepare(npc, enemy, weapon, false);
    auto& world = Level().ObjectSpace;
    world = FixtureObjectSpace();
    assert(npc.combat_fire_solution());
    action.fire();
    weapon.start_burst();
    weapon.advance_for_test(1);
    assert(weapon.ammo == 29 && weapon.ShotsFired() == 1 && npc.effector.calls == 1);
    const unsigned samples = npc.fire_param_calls;
    ally.enemy_relation = false;
    ally.live_reads_allowed = true;
    ally.live_position = Fvector(.5f, 0.f, 5.f);
    world.objects = {&ally}; // Ally moves after scheduling and first shot, in the same frame.
    weapon.advance_for_test(20);
    assert(weapon.ammo == 29 && weapon.m_magazine.size() == 29);
    assert(weapon.ShotsFired() == 1 && npc.effector.calls == 1);
    assert(!weapon.IsWorking() && !weapon.m_bFireSingleShot && weapon.stops == 1);
    assert(weapon.fTime >= weapon.fTimeToFire && !weapon.StopedAfterQueueFired());
    assert(npc.fire_param_calls == samples); // No second stochastic aim sample for the guard.
    assert(std::strcmp(npc.combat_fire_block_reason(), "friendly") == 0);

    // Run the actual UpdateCL eFire branch: no stale shot, ammo use or stuck state.
    weapon.update_fire_for_test(.5f);
    assert(weapon.GetState() == CWeapon::eFire && weapon.scheduled_stops == 0);
    weapon.update_fire_for_test(.5f);
    weapon.update_fire_for_test(.5f);
    assert(weapon.GetState() == CWeapon::eIdle && weapon.scheduled_stops == 1);
    assert(weapon.ammo == 29 && weapon.ShotsFired() == 1 && npc.effector.calls == 1);
    // The object planner can now issue its ordinary fire start after the ally clears.
    world.objects.clear();
    ++Device.dwFrame;
    assert(npc.combat_fire_solution());
    action.fire();
    weapon.start_burst();
    weapon.advance_for_test(1);
    assert(weapon.ammo == 28 && weapon.ShotsFired() == 1 && npc.effector.calls == 2);
    assert(weapon.IsWorking());
    world = FixtureObjectSpace();
}

void test_guard_matches_initial_burst_direction_and_actor_bypass()
{
    CAI_Stalker npc;
    CEntityAlive enemy(10), ally(11);
    CWeaponMagazined weapon(20, npc);
    CObjectHandlerPlanner planner(npc);
    CStalkerActionCombatBase action(npc);
    prepare(npc, enemy, weapon, false);
    auto& world = Level().ObjectSpace;
    world = FixtureObjectSpace();
    action.fire();
    weapon.m_iShootEffectorStart = 2;
    weapon.start_burst();
    weapon.advance_for_test(1);
    npc.fixture_direction = Fvector(1.f, 0.f, 0.f);
    ally.enemy_relation = false;
    ally.live_reads_allowed = true;
    ally.live_position = Fvector(.5f, 0.f, 5.f);
    world.objects = {&ally};
    weapon.advance_for_test(1); // Shot 2 would still use the saved initial direction.
    assert(weapon.ShotsFired() == 1 && weapon.ammo == 29);

    weapon.working = true;
    weapon.m_iShotNum = 2; // Above the startup threshold FireTrace uses this frame's direction.
    weapon.advance_for_test(1);
    assert(weapon.ShotsFired() == 3 && weapon.ammo == 28);
    ally.live_position = Fvector(5.f, 0.f, .5f);
    weapon.advance_for_test(1);
    assert(weapon.ShotsFired() == 3 && weapon.ammo == 28);

    CObject actor(99);
    weapon.non_stalker_parent = &actor;
    weapon.start_burst();
    const unsigned queries = world.nearby_calls;
    weapon.advance_for_test(1); // The new hook has no effect on a non-stalker parent.
    assert(weapon.ShotsFired() == 1 && weapon.ammo == 27);
    assert(world.nearby_calls == queries);
    world = FixtureObjectSpace();
}

}

int main()
{
    test_remaining_is_not_absolute_queue_length();
    test_completed_queue_is_not_reopened_by_next_profile();
    test_completion_boundary_and_live_profile_shrink();
    test_zero_allowance_and_lost_target_stop_fire_goal();
    test_ally_entering_live_queue_stops_before_shot_and_can_resume();
    test_guard_matches_initial_burst_direction_and_actor_bypass();
    std::cout << "PASS: extracted combat base/queue planner with production shot-loop completion semantics\n";
}
