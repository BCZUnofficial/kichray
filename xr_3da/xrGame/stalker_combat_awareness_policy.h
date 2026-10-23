#pragma once

// Stalker-local decisions. No world queries, target pointers or save-game state.
namespace stalker_combat_awareness
{
inline bool peripheral_motion(float distance_sqr, float facing, float speed_sqr, unsigned int sample_age)
{
    // A narrow extension of peripheral vision, not rear or all-round detection.
    return distance_sqr >= .04f && distance_sqr <= 9.f && facing >= -.34202015f &&
        facing < .57357644f && speed_sqr >= .0625f && sample_age <= 500u;
}

struct Peripheral
{
    unsigned int candidate, since, looked, last_scan;
    bool pending, active, scanned, reacted;
    Peripheral() : candidate(0), since(0), looked(0), last_scan(0),
        pending(false), active(false), scanned(false), reacted(false) {}
    bool due(unsigned int now)
    {
        if (scanned && now - last_scan < 250u) return false;
        scanned = true; last_scan = now; return true;
    }
    bool looking(unsigned int now) const { return active && now - looked < 1000u; }
    bool observe(unsigned int now, unsigned int id, bool present)
    {
        active = looking(now);
        if (reacted && now - looked < 2500u) { pending = false; return false; }
        if (!present) { pending = false; return false; }
        if (!pending || candidate != id) { pending = true; candidate = id; since = now; return false; }
        if (now - since < 300u) return false;
        looked = now; active = reacted = true; pending = false; return true;
    }
};

struct ScoutLease
{
    unsigned int owner, enemy, started;
    bool assigned;
    ScoutLease() : owner(0), enemy(0), started(0), assigned(false) {}
    bool active(unsigned int now) const { return assigned && now - started < 6500u; }
    bool claim(unsigned int who, unsigned int target, unsigned int now)
    {
        if (active(now)) return owner == who && enemy == target;
        assigned = true; owner = who; enemy = target; started = now; return true;
    }
    void release(unsigned int who) { if (assigned && owner == who) assigned = false; }
    void remove(unsigned int id) { if (assigned && (owner == id || enemy == id)) assigned = false; }
};

inline bool sneak_candidate(unsigned int age, float distance_sqr, bool squad_visible,
    bool unsafe, unsigned int npc, unsigned int observation, bool grouped)
{
    // At most six metres of validated final approach, ending four metres short.
    // Stable per episode: no frame-by-frame random reroll.
    return !unsafe && !squad_visible && age <= 7000u && distance_sqr >= 25.f &&
        distance_sqr <= 100.f && (npc + observation / 1000u) % 3u < (grouped ? 2u : 1u);
}
}
