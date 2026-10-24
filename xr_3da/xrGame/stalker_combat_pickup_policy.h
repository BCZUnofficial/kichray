#pragma once

namespace stalker_combat_pickup
{
// Per-NPC, transient state. No object pointers and no save-format changes.
struct Recovery
{
    unsigned int target, started, advanced, cursor;
    float x, y, z;
    unsigned int rejected_ids[4], rejected_at[4];
    Recovery() : target(~0u), started(0), advanced(0), cursor(0), x(0), y(0), z(0)
    {
        for (unsigned int i = 0; i != 4; ++i) { rejected_ids[i] = ~0u; rejected_at[i] = 0; }
    }
    bool rejected(unsigned int id, unsigned int now) const
    {
        for (unsigned int i = 0; i != 4; ++i)
            if (rejected_ids[i] == id && now - rejected_at[i] < 10000u) return true;
        return false;
    }
    void forget(unsigned int id)
    {
        if (target == id) target = ~0u;
        for (unsigned int i = 0; i != 4; ++i)
            if (rejected_ids[i] == id) rejected_ids[i] = ~0u;
    }
    bool approach(unsigned int id, unsigned int now, float px, float py, float pz, bool failed)
    {
        if (rejected(id, now)) return false;
        if (target != id)
        {
            target = id; started = advanced = now; x = px; y = py; z = pz;
        }
        const float dx = px-x, dy = py-y, dz = pz-z;
        if (dx*dx + dy*dy + dz*dz >= .0225f)
        {
            advanced = now; x = px; y = py; z = pz;
        }
        if (now - started < 15000u && now - advanced < 3000u &&
            !(failed && now - started >= 250u)) return true;
        rejected_ids[cursor] = id; rejected_at[cursor] = now;
        cursor = (cursor + 1u) % 4u; target = ~0u;
        return false;
    }
};
}
