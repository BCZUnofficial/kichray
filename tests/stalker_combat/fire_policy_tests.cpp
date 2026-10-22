#include "stalker_combat_fire_policy.h"
#include <cassert>
#include <cmath>
#include <iostream>

namespace policy = stalker_combat_fire_policy;

int main()
{
    // Omitted queue arguments preserve the current profile; zero is an explicit
    // legacy request and must not be confused with the unsigned -1 sentinel.
    assert(!policy::queue_parameters_provided(~0u));
    assert(policy::queue_parameters_provided(0u));
    assert(policy::queue_parameters_provided(3u));
    assert(!policy::refresh_queue(10, 10, false, 200, 300));
    assert(policy::refresh_queue(10, 11, false, 200, 300));
    assert(policy::refresh_queue(10, 10, true, 200, 300));
    assert(policy::refresh_queue(10, 10, false, 300, 300));
    assert(!policy::deadline_reached(0xfffffff0u, 0x10u));
    assert(policy::deadline_reached(0x20u, 0x10u));

    // A target hovering around 15/30 m does not redraw the burst profile each
    // update, but a genuine distance-band change eventually takes effect.
    int band = policy::distance_band(14.9f, -1);
    assert(band == 0);
    band = policy::distance_band(15.1f, band); assert(band == 0);
    band = policy::distance_band(16.6f, band); assert(band == 1);
    band = policy::distance_band(14.9f, band); assert(band == 1);
    band = policy::distance_band(13.4f, band); assert(band == 0);
    band = policy::distance_band(40.f, band); assert(band == 2);
    band = policy::distance_band(29.9f, band); assert(band == 2);
    band = policy::distance_band(28.4f, band); assert(band == 1);

    const float degrees = 3.14159265359f / 180.f;
    assert(policy::aim_aligned(std::cos(9.f * degrees), false));
    assert(!policy::aim_aligned(std::cos(12.f * degrees), false));
    assert(policy::aim_aligned(std::cos(12.f * degrees), true));
    assert(!policy::aim_aligned(std::cos(15.f * degrees), true));
    // Dot product treats equal pitch and yaw errors identically.
    const float pitch_dot = std::cos(20.f * degrees);
    const float yaw_dot = std::cos(20.f * degrees);
    assert(!policy::aim_aligned(pitch_dot, true));
    assert(policy::aim_aligned(pitch_dot, true) == policy::aim_aligned(yaw_dot, true));

    // A free lateral ray cannot approve a shot whose center hits a wall.
    const float blocked_center = 2.f, free_lateral = 50.f, target = 10.f;
    assert(free_lateral > target);
    assert(!policy::line_reaches_target(blocked_center, target, false));
    assert(policy::line_reaches_target(50.f, target, false));
    assert(policy::line_reaches_target(9.5f, target, true));

    // Switching weapons refreshes that weapon's queue, but does not replenish
    // the suppression budget for the same remembered enemy observation.
    assert(policy::refresh_queue(10, 11, false, 200, 300));
    assert(!policy::reset_suppression_budget(false, false, 100, 100));
    assert(policy::reset_suppression_budget(true, false, 100, 100));
    assert(policy::reset_suppression_budget(false, false, 100, 200));
    assert(policy::reset_suppression_budget(false, true, 100, 100));
    assert(!policy::suppression_fresh(10000, ~0u));
    assert(policy::suppression_fresh(4000, 1000));
    assert(!policy::suppression_fresh(4001, 1000));
    assert(policy::suppression_fresh(0x20u, 0xfffffff0u));
    assert(policy::suppression_shot_limit(30) == 8);
    assert(policy::suppression_shot_limit(10) == 5);
    assert(policy::suppression_has_ammo(7, 20, 30));
    assert(!policy::suppression_has_ammo(8, 20, 30));
    assert(!policy::suppression_has_ammo(1, 1, 30));
    assert(policy::suppression_remaining(0, 2, 30) == 1);
    assert(policy::suppression_remaining(0, 5, 30) == 4);
    assert(policy::suppression_remaining(7, 30, 30) == 1);
    assert(policy::suppression_remaining(8, 30, 30) == 0);

    // Two missiles by the same thrower retain separate identities. Only the
    // already admitted stalker grenade events call this specialised rule.
    assert(policy::same_grenade(101, 101));
    assert(!policy::same_grenade(101, 102));
    assert(!policy::same_grenade(101, ~0u));
    const float near_short = policy::grenade_urgency(3.f, 10.f, 300, 100);
    const float near_long = policy::grenade_urgency(3.f, 10.f, 2200, 100);
    const float farther_short = policy::grenade_urgency(9.f, 10.f, 300, 100);
    assert(near_short < near_long);
    assert(near_short < farther_short);
    assert(near_long < 10000.f); // Ordinary enemy sound cannot outrank a live near grenade.
    assert(policy::grenade_urgency(100.f, 10.f, 300, 100) == FLT_MAX);
    // Future/unknown timers are bounded instead of overflowing a danger score.
    assert(policy::grenade_urgency(3.f, 10.f, ~0u, 100) < 10000.f);
    std::cout << "stalker fire/queue/grenade policy tests passed\n";
}
