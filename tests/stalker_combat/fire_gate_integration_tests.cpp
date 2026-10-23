#include "fire_gate_fixture.h"
#include <cstring>
#include <iostream>

TestDevice Device;
namespace stalker_combat_diagnostics
{
bool enabled(unsigned int) { return false; }
void log(unsigned int, Event, const char*, const char*, ...) {}
}

// This file is generated from ai_stalker_fire.cpp on every test run. No copy of
// the production gate or its state transitions is maintained in the fixture.
#include "fire_gate_production.inc"

namespace
{
void frame(u32 time) { ++Device.dwFrame; Device.dwTimeGlobal = time; }
void reason(const CAI_Stalker& npc, const char* value)
{ assert(std::strcmp(npc.combat_fire_block_reason(), value) == 0); }
bool equal(const Fvector& a, const Fvector& b) { return a.distance_to(b) < .0001f; }

void snapshot(CAI_Stalker& npc, CEntityAlive& enemy, const Fvector& position, u32 shared_time)
{
    auto& visual = npc.memories.visuals.records[enemy.ID()];
    visual.has_snapshot = true;
    visual.snapshot.m_enabled = true;
    visual.snapshot.m_object_params.m_position = position;
    visual.shared_time = shared_time;
    auto& known = npc.memories.known[enemy.ID()];
    known.m_object = &enemy;
    known.m_object_params.m_position = position;
}
void see(CAI_Stalker& npc, CEntityAlive& enemy, u32 time)
{
    enemy.live_reads_allowed = true;
    snapshot(npc, enemy, enemy.live_position, time);
    auto& record = npc.memories.visuals.records[enemy.ID()];
    record.visible = true;
    record.direct_time = time;
}
void hide(CAI_Stalker& npc, CEntityAlive& enemy)
{
    npc.memories.visuals.records[enemy.ID()].visible = false;
    enemy.live_reads_allowed = false;
    enemy.live_position = Fvector(99.f, 40.f, 200.f);
}
void equip_and_select(CAI_Stalker& npc, CEntityAlive& enemy, CWeapon& weapon)
{ npc.memories.enemies.value = &enemy; npc.items.active = &weapon; }
void clear_rays(CAI_Stalker& npc) { for (auto& ray : npc.rays) ray = RayResult(); }
void shoot(CAI_Stalker& npc, CWeapon& weapon)
{ assert(weapon.ammo > 0); ++weapon.fired; --weapon.ammo; npc.on_weapon_shot_start(&weapon); }

void test_known_position_never_reads_hidden_entity()
{
    CAI_Stalker npc;
    CEntityAlive enemy(10), second(11);
    CWeapon weapon(20);
    equip_and_select(npc, enemy, weapon);
    enemy.center_height = 1.2f;
    see(npc, enemy, 1000);
    Fvector point;
    assert(npc.combat_target_position(point, false));
    assert(equal(point, Fvector(0.f, 0.f, 10.f)));
    assert(npc.combat_target_position(point, true));
    assert(equal(point, Fvector(0.f, 1.2f, 10.f)));
    const unsigned reads = enemy.position_reads + enemy.center_reads;

    hide(npc, enemy);
    snapshot(npc, enemy, Fvector(2.f, 0.f, 12.f), 1100);
    assert(npc.combat_target_position(point, true));
    assert(equal(point, Fvector(2.f, 1.2f, 12.f)));
    assert(enemy.position_reads + enemy.center_reads == reads);
    npc.memories.visuals.records[enemy.ID()].snapshot.m_enabled = false;
    npc.memories.known[enemy.ID()].m_object_params.m_position = Fvector(3.f, 0.f, 13.f);
    assert(npc.combat_target_position(point, false));
    assert(equal(point, Fvector(3.f, 0.f, 13.f)));
    assert(npc.start_pick_distance() >= 50.f);
    npc.memories.known.erase(enemy.ID());
    assert(!npc.combat_target_position(point, true));

    npc.memories.enemies.value = &second;
    snapshot(npc, second, Fvector(0.f, 0.f, 20.f), 1200);
    assert(npc.combat_target_position(point, true));
    assert(equal(point, Fvector(0.f, .9f, 20.f))); // No height borrowed from another target.
    assert(second.position_reads == 0 && second.center_reads == 0);
}

void test_absent_inputs_and_same_frame_identity_changes()
{
    frame(1000);
    CAI_Stalker npc;
    CEntityAlive enemy(10), other(11);
    CWeapon first(20), second(21);
    CInventoryItem item(22);
    assert(!npc.fire_make_sense()); reason(npc, "no_target");
    assert(npc.fire_param_calls == 0 && npc.ray_calls == 0);
    npc.memories.enemies.value = &enemy;
    see(npc, enemy, 1000);
    assert(!npc.combat_fire_solution()); reason(npc, "no_weapon");
    npc.items.active = &item;
    assert(!npc.combat_fire_solution()); reason(npc, "no_weapon");
    npc.items.active = &first;
    assert(npc.fire_make_sense()); reason(npc, "none");
    assert(npc.fire_param_calls == 1 && npc.ray_calls == 5);
    assert(npc.combat_fire_solution());
    assert(npc.fire_param_calls == 1 && npc.ray_calls == 5);

    npc.items.active = &second; // Same frame, a different weapon must obtain its own result.
    npc.rays[0] = RayResult(2.f);
    assert(!npc.combat_fire_solution()); reason(npc, "world");
    assert(npc.fire_param_calls == 2 && npc.ray_calls == 10);
    npc.items.active = &first;
    clear_rays(npc);
    assert(npc.combat_fire_solution());
    assert(npc.fire_param_calls == 3 && npc.ray_calls == 15);
    assert(npc.combat_fire_blocked_time() == 0);

    // A shared-only enemy cannot inherit the previous enemy's allowed result.
    npc.memories.enemies.value = &other;
    snapshot(npc, other, Fvector(0.f, 0.f, 10.f), 1000);
    assert(!npc.combat_fire_solution()); reason(npc, "suppression_age");
    assert(npc.fire_param_calls == 3);
    assert(other.position_reads == 0 && other.center_reads == 0);
    npc.memories.enemies.value = nullptr;
    assert(!npc.combat_fire_solution()); reason(npc, "no_target");
    assert(npc.combat_fire_blocked_time() == 0);
}

void test_center_friend_sweep_and_blocked_timer()
{
    frame(2000);
    CAI_Stalker npc;
    CEntityAlive enemy(10), other(11);
    CWeapon first(20), second(21);
    equip_and_select(npc, enemy, first);
    see(npc, enemy, 2000);
    npc.rays[0] = RayResult(2.f);
    npc.rays[1] = RayResult(50.f, true);
    assert(!npc.combat_fire_solution()); reason(npc, "world");
    assert(npc.m_pick_distance == 50.f && npc.m_combat_center_pick_distance == 2.f);
    assert(npc.m_can_kill_enemy && !npc.m_combat_center_can_kill_enemy);
    assert(npc.fire_param_calls == 1 && npc.ray_calls == 5);
    assert(npc.combat_fire_blocked_time() == 0);
    frame(2250);
    assert(!npc.combat_fire_solution());
    assert(npc.combat_fire_blocked_time() == 250);

    clear_rays(npc);
    npc.rays[2] = RayResult(5.f, false, true);
    frame(2300);
    assert(!npc.combat_fire_solution()); reason(npc, "friendly");
    assert(npc.combat_fire_blocked_time() == 300); // Continuous obstruction, even if its cause changes.
    clear_rays(npc);
    frame(2400);
    assert(npc.combat_fire_solution()); reason(npc, "none");
    assert(npc.combat_fire_blocked_time() == 0);

    npc.rays[0] = RayResult(2.f);
    frame(2500);
    assert(!npc.combat_fire_solution());
    frame(2600);
    assert(!npc.combat_fire_solution());
    assert(npc.combat_fire_blocked_time() == 100);
    npc.items.active = &second;
    assert(!npc.combat_fire_solution()); reason(npc, "world");
    assert(npc.combat_fire_blocked_time() == 0); // Identity change starts a new obstruction episode.
    frame(2700);
    assert(!npc.combat_fire_solution());
    assert(npc.combat_fire_blocked_time() == 100);
    see(npc, other, 2700);
    npc.memories.enemies.value = &other;
    assert(!npc.combat_fire_solution()); reason(npc, "world");
    assert(npc.combat_fire_blocked_time() == 0);

    npc.fixture_direction = Fvector(0.f, 1.f, 0.f);
    frame(2800);
    assert(!npc.combat_fire_solution()); reason(npc, "aiming");
    assert(npc.combat_fire_blocked_time() == 0);
    npc.fixture_direction = Fvector(0.f, 0.f, 1.f);
    frame(2900);
    assert(!npc.combat_fire_solution()); reason(npc, "world");
    npc.items.active = nullptr;
    assert(!npc.combat_fire_solution()); reason(npc, "no_weapon");
    assert(npc.combat_fire_blocked_time() == 0);
}

void test_pitch_tolerance_and_hysteresis()
{
    CAI_Stalker npc;
    CEntityAlive enemy(10);
    CWeapon weapon(20);
    equip_and_select(npc, enemy, weapon);
    see(npc, enemy, 1000);
    const float degrees = 3.14159265359f / 180.f;
    auto pitch = [&](float angle, u32 time) {
        npc.fixture_direction = Fvector(0.f, std::sin(angle*degrees), std::cos(angle*degrees));
        frame(time);
        return npc.combat_fire_solution();
    };
    assert(!pitch(12.f, 1000)); reason(npc, "aiming");
    assert(pitch(9.f, 1100));
    assert(pitch(12.f, 1200)); // Holding a lock permits a small animation/recoil oscillation.
    assert(!pitch(15.f, 1300)); reason(npc, "aiming");
    assert(!pitch(12.f, 1400)); // A lost lock has to reacquire the tighter tolerance.
    assert(pitch(0.f, 1500));
}

void test_visibility_shots_relay_and_weapon_budget()
{
    frame(1000);
    CAI_Stalker npc;
    CEntityAlive enemy(10);
    CWeapon first(20), second(21);
    equip_and_select(npc, enemy, first);
    see(npc, enemy, 1000);
    assert(npc.combat_fire_solution());
    assert(npc.combat_suppression_remaining() == u32(-1));
    shoot(npc, first);
    assert(npc.m_combat_suppression_shots == 0);
    assert(npc.effector.calls == 1 && npc.effector.seed == npc.m_weapon_shot_random_seed);
    assert(std::fabs(npc.effector.last_dispersion - .11f) < .0001f);
    const unsigned reads = enemy.position_reads + enemy.center_reads;
    hide(npc, enemy);
    frame(1100);
    assert(npc.combat_fire_solution());
    assert(npc.combat_suppression_remaining() == 8);
    const unsigned samples = npc.fire_param_calls;
    for (u32 shot = 1; shot <= 8; ++shot)
    {
        shoot(npc, first);
        assert(npc.m_combat_suppression_shots == shot);
        assert(npc.combat_suppression_remaining() == 8 - shot);
        assert(npc.combat_fire_solution() == (shot < 8));
    }
    reason(npc, "suppression_ammo");
    assert(npc.fire_param_calls == samples); // Shot invalidates decision, not the frame's ray sample.
    assert(enemy.position_reads + enemy.center_reads == reads);

    npc.items.active = &second;
    assert(!npc.combat_fire_solution()); reason(npc, "suppression_ammo");
    assert(npc.m_combat_suppression_shots == 8);
    snapshot(npc, enemy, Fvector(0.f, 0.f, 12.f), 1200);
    frame(1200);
    assert(!npc.combat_fire_solution()); reason(npc, "suppression_ammo");
    assert(npc.m_combat_suppression_shots == 8); // Fresh relay did not provide a direct sighting.
    assert(npc.memories.visuals.shared_time_reads == 0);

    enemy.live_position = Fvector(0.f, 0.f, 12.f);
    see(npc, enemy, 2000);
    frame(2000);
    assert(npc.combat_fire_solution());
    assert(npc.m_combat_suppression_shots == 0);
    hide(npc, enemy);
    frame(2100);
    assert(npc.combat_fire_solution());
    shoot(npc, second);
    shoot(npc, second);
    snapshot(npc, enemy, Fvector(0.f, 0.f, 13.f), 2200);
    frame(2200);
    assert(npc.combat_fire_solution());
    assert(npc.m_combat_suppression_shots == 2 && npc.combat_suppression_remaining() == 6);
    frame(5000);
    assert(npc.combat_fire_solution()); // Exactly three seconds since the own sighting.
    snapshot(npc, enemy, Fvector(0.f, 0.f, 13.f), 5001);
    frame(5001);
    assert(!npc.combat_fire_solution()); reason(npc, "suppression_age");
    assert(npc.m_combat_suppression_shots == 2);
}

void test_final_shot_after_death_keeps_recoil_without_ai_memory()
{
    frame(1000);
    CAI_Stalker npc;
    CEntityAlive enemy(10);
    CWeapon weapon(20);
    equip_and_select(npc, enemy, weapon);
    see(npc, enemy, 1000);
    assert(npc.combat_fire_solution());
    hide(npc, enemy);
    frame(1100);
    assert(npc.combat_fire_solution());
    shoot(npc, weapon);
    assert(npc.m_combat_suppression_shots == 1);
    assert(npc.combat_fire_solution());
    const unsigned memory_accesses = npc.memory_accesses;
    const u32 gate_frame = npc.m_combat_fire_gate_frame;

    // Death detaches squad sense storage while the clutched-hammer shot remains
    // valid. Keep the old selected enemy, as death does not clear that manager.
    npc.fixture_alive = false;
    npc.fixture_memory_available = false;
    shoot(npc, weapon);
    assert(npc.memory_accesses == memory_accesses);
    assert(npc.m_combat_suppression_shots == 1 && npc.m_combat_fire_gate_frame == gate_frame);
    assert(npc.effector.calls == 2 && npc.effector.seed == npc.m_weapon_shot_random_seed);
    assert(std::fabs(npc.effector.last_dispersion - .12f) < .0001f);
}

void test_shot_callback_without_matching_target()
{
    CAI_Stalker npc;
    CEntityAlive other(11);
    CWeapon weapon(20);
    npc.items.active = &weapon;
    npc.m_combat_fire_enemy_id = 10;
    npc.m_combat_suppression_shots = 3;
    npc.m_combat_fire_gate_frame = 77;
    shoot(npc, weapon); // No selected enemy: recoil only.
    assert(npc.m_combat_suppression_shots == 3 && npc.m_combat_fire_gate_frame == 77);
    npc.memories.enemies.value = &other;
    shoot(npc, weapon); // A different enemy must not consume the previous budget.
    assert(npc.m_combat_suppression_shots == 3 && npc.m_combat_fire_gate_frame == 77);
    assert(npc.effector.calls == 2);
}

void test_shared_only_load_and_ammo_reserve()
{
    frame(1000);
    CAI_Stalker npc;
    CEntityAlive enemy(10);
    CWeapon weapon(20);
    equip_and_select(npc, enemy, weapon);
    snapshot(npc, enemy, Fvector(0.f, 0.f, 10.f), 1000);
    assert(!npc.combat_fire_solution()); reason(npc, "suppression_age");
    snapshot(npc, enemy, Fvector(0.f, 0.f, 10.f), 1100);
    frame(1100);
    assert(!npc.combat_fire_solution()); reason(npc, "suppression_age");
    assert(enemy.position_reads == 0 && npc.fire_param_calls == 0);

    see(npc, enemy, 1200);
    frame(1200);
    assert(npc.combat_fire_solution());
    hide(npc, enemy);
    frame(1300);
    assert(npc.combat_fire_solution());
    // Save/load restores snapshots but deliberately clears the per-NPC direct ledger.
    npc.memories.visuals.records[enemy.ID()].direct_time = u32(-1);
    snapshot(npc, enemy, Fvector(0.f, 0.f, 10.f), 1400);
    frame(1400);
    assert(!npc.combat_fire_solution()); reason(npc, "suppression_age");

    enemy.live_position = Fvector(0.f, 0.f, 10.f);
    see(npc, enemy, 1500);
    frame(1500);
    assert(npc.combat_fire_solution());
    hide(npc, enemy);
    weapon.ammo = 2;
    frame(1600);
    assert(npc.combat_fire_solution());
    assert(npc.combat_suppression_remaining() == 1);
    shoot(npc, weapon);
    assert(!npc.combat_fire_solution()); reason(npc, "suppression_ammo");
    assert(weapon.ammo == 1 && npc.combat_suppression_remaining() == 0);
    weapon.ammo = 10;
    weapon.weapon_type = 5;
    frame(1700);
    assert(!npc.combat_fire_solution()); reason(npc, "suppression_age");
    enemy.live_position = Fvector(0.f, 0.f, 10.f);
    see(npc, enemy, 1800);
    frame(1800);
    assert(npc.combat_fire_solution()); // Non-automatic weapons still fire at visible enemies.
    assert(npc.memories.visuals.shared_time_reads == 0);
}
}

int main()
{
    test_known_position_never_reads_hidden_entity();
    test_absent_inputs_and_same_frame_identity_changes();
    test_center_friend_sweep_and_blocked_timer();
    test_pitch_tolerance_and_hysteresis();
    test_visibility_shots_relay_and_weapon_budget();
    test_final_shot_after_death_keeps_recoil_without_ai_memory();
    test_shot_callback_without_matching_target();
    test_shared_only_load_and_ammo_reserve();
    std::cout << "PASS: extracted stalker fire gate, shot lifecycle, target coordinates, visibility, budget, rays and cache\n";
}
