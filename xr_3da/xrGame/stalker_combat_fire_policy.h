#pragma once

#include <float.h>

// Small, engine-independent rules used by stalker combat only.
// Keep perception, weapon simulation, and rendering outside this policy.
namespace stalker_combat_fire_policy
{
inline unsigned int invalid_id() { return ~0u; }
inline bool queue_parameters_provided(unsigned int minimum)
{
    return minimum != invalid_id();
}
inline bool deadline_reached(unsigned int now, unsigned int deadline)
{
    return (now - deadline) < 0x80000000u;
}
inline bool refresh_queue(unsigned int previous_weapon, unsigned int weapon,
    bool profile_changed, unsigned int now, unsigned int deadline)
{
    return previous_weapon != weapon || profile_changed || deadline_reached(now, deadline);
}
inline bool queue_in_progress(bool fire_state, bool working, bool completed,
    unsigned int fired, int queue_size)
{
    return fire_state && working && !completed &&
        (queue_size < 0 || fired < static_cast<unsigned int>(queue_size));
}
inline unsigned int suppression_queue_limit(unsigned int fired, unsigned int remaining)
{
    return fired > invalid_id() - remaining ? invalid_id() : fired + remaining;
}
inline int distance_band(float distance, int previous)
{
    if (previous < 0 || previous > 2)
        return distance > 30.f ? 2 : (distance > 15.f ? 1 : 0);
    if (previous == 0)
        return distance > 31.5f ? 2 : (distance > 16.5f ? 1 : 0);
    if (previous == 2)
        return distance < 13.5f ? 0 : (distance < 28.5f ? 1 : 2);
    return distance < 13.5f ? 0 : (distance > 31.5f ? 2 : 1);
}
inline bool aim_aligned(float direction_dot, bool already_aligned)
{
    // 10 degrees to start, 14 degrees to continue; both pitch and yaw participate.
    return direction_dot >= (already_aligned ? 0.970295726f : 0.984807753f);
}
inline bool line_reaches_target(float center_distance, float target_distance, bool center_hits_enemy)
{
    return center_hits_enemy || center_distance + 0.05f >= target_distance;
}
inline bool reset_suppression_budget(bool target_changed, bool currently_visible,
    unsigned int previous_direct_time, unsigned int direct_time)
{
    // Weapon changes are deliberately absent: swapping guns does not create
    // new information about an occluded target or replenish its burst budget.
    // The timestamps must come from this NPC's direct-sighting ledger, not relay.
    return target_changed || currently_visible || previous_direct_time != direct_time;
}
inline unsigned int suppression_shot_limit(unsigned int magazine_size)
{
    // A short covering burst, at most half a magazine and never over eight shots.
    unsigned int result = magazine_size / 2u;
    if (result < 1u) result = 1u;
    return result > 8u ? 8u : result;
}
inline bool suppression_fresh(unsigned int now, unsigned int observed_at)
{
    return observed_at != invalid_id() && (now - observed_at) <= 3000u;
}
inline unsigned int suppression_remaining(unsigned int shots, unsigned int ammo, unsigned int magazine)
{
    const unsigned int limit = suppression_shot_limit(magazine);
    if (ammo <= 1u || shots >= limit)
        return 0u;
    const unsigned int remaining = limit - shots;
    return remaining < ammo - 1u ? remaining : ammo - 1u;
}
inline bool suppression_has_ammo(unsigned int shots, unsigned int ammo, unsigned int magazine)
{
    return suppression_remaining(shots, ammo, magazine) != 0u;
}
inline bool same_grenade(unsigned int left_id, unsigned int right_id)
{
    return left_id == right_id;
}
inline float grenade_urgency(float distance, float radius, unsigned int fuse_ms,
    unsigned int observation_age_ms)
{
    if (radius < 1.f) radius = 1.f;
    // Old positions are less reliable; preserve the last observation, do not track
    // an invisible grenade through its live object coordinates.
    const float age_penalty = observation_age_ms > 2000u ? 1500.f : float(observation_age_ms) * .25f;
    if (distance > radius + 3.f)
        return FLT_MAX;
    const unsigned int bounded_fuse = fuse_ms > 5000u ? 5000u : fuse_ms;
    return 1000.f + float(bounded_fuse) + (distance / radius) * 1000.f + age_penalty;
}
}
