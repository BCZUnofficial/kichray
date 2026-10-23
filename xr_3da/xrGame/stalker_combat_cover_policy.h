#pragma once

// Pure decisions shared by stalker tactical code and its focused regression tests.
// No engine, renderer, world queries or global state belong in this file.
namespace stalker_combat_cover_policy
{
inline unsigned int elapsed(unsigned int now, unsigned int then) { return now - then; }
inline float capped(float value, float maximum) { return value < maximum ? value : maximum; }
inline float preferred_distance(int weapon_type)
{
    switch (weapon_type)
    {
    case 5: return 12.f; // pistol
    case 7: return 8.f;  // shotgun
    case 8: return 35.f; // sniper
    default: return 22.f;
    }
}
inline float cover_score(float exposure, bool open_neighbour, float danger_weight,
                         float travel_distance, float enemy_distance, int weapon_type)
{
    if (danger_weight <= .0001f) return 100000.f;
    const float preferred = preferred_distance(weapon_type);
    const float range_error = enemy_distance < preferred * .5f ? preferred * .5f - enemy_distance :
                              (enemy_distance > preferred * 2.f ? enemy_distance - preferred * 2.f : 0.f);
    return (exposure + (open_neighbour ? 10.f : 0.f) +
            capped(travel_distance * .012f, .4f) + capped(range_error * .015f, .35f)) / danger_weight;
}
inline bool retry_empty(unsigned int age, float moved_sqr, float threat_moved_sqr)
{
    return age >= 1000u || (age >= 250u && (moved_sqr >= 4.f || threat_moved_sqr >= 9.f));
}
inline bool exposure_worsened(float current, float accepted)
{
    return current >= accepted + .25f;
}
inline bool accept_change(float old_score, float new_score, unsigned int held, bool old_safe)
{
    return !old_safe || (held >= 750u && new_score + .15f < old_score);
}
inline bool failure_expired(unsigned int age) { return age >= 5000u; }
inline bool movement_stalled(unsigned int age, bool failed) { return failed ? age >= 250u : age >= 3000u; }
inline float ambush_distance(int weapon_type)
{
    switch (weapon_type)
    {
    case 5: return 12.f;
    case 7: return 7.f;
    case 8: return 35.f;
    default: return 22.f;
    }
}
// Modular ordering prevents an unchanged/older relayed report from extending the search.
inline bool newer_observation(unsigned int candidate, unsigned int previous)
{
    const unsigned int difference = candidate - previous;
    return difference != 0u && difference < 0x80000000u;
}
inline bool search_expired(unsigned int age) { return age >= 45000u; }
inline bool finish_search(unsigned int age, bool sectors_done, bool squad_visible)
{
    return !squad_visible && (search_expired(age) || (sectors_done && age >= 30000u));
}
inline unsigned int fuse_remaining(unsigned int now, unsigned int deadline)
{
    const unsigned int remaining = deadline - now;
    return remaining < 0x80000000u ? remaining : 0u;
}
inline float grenade_travel_budget(unsigned int fuse_ms, float run_speed)
{
    // Reserve reaction time and allow for turning/acceleration; not a path-time prediction.
    return fuse_ms > 250u && run_speed > 0.f ? float(fuse_ms - 250u) * .001f * run_speed * .65f : 0.f;
}
inline float grenade_escape_radius(float travel_budget)
{
    // Even an imminent explosion warrants a short escape attempt; movement improves
    // separation before reaching the endpoint. Do not ask for a distant route then.
    return travel_budget < 1.25f ? 1.25f : capped(travel_budget, 4.f);
}
inline bool search_step_expired(unsigned int age) { return age >= 2500u; }
inline bool escape_improves(float old_distance_sqr, float new_distance_sqr)
{
    return new_distance_sqr > old_distance_sqr + 1.f;
}
}
