#include "../../xr_3da/xrGame/stalker_combat_cover_policy.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <iostream>
namespace policy = stalker_combat_cover_policy;
int main()
{
    // Normalized directional exposure used to fit entirely below the old +1 threshold.
    assert(policy::exposure_worsened(.9f, .1f));
    assert(!policy::exposure_worsened(.21f, .1f));
    // A harmless fluctuation must not bounce the NPC between two covers.
    assert(!policy::accept_change(.6f, .5f, 5000u, true));
    assert(!policy::accept_change(.9f, .2f, 200u, true));
    assert(policy::accept_change(.9f, .2f, 1000u, true));
    assert(policy::accept_change(.1f, 1.f, 200u, false)); // invalid cover has no hold lock
    // Same protection: prefer the shorter move, while still accepting a real shelter.
    assert(policy::cover_score(.2f,false,1.f,2.f,22.f,6) < policy::cover_score(.2f,false,1.f,20.f,22.f,6));
    assert(policy::cover_score(.2f,false,1.f,20.f,22.f,6) < policy::cover_score(.2f,true,1.f,2.f,22.f,6));
    assert(policy::cover_score(.2f,false,0.f,1.f,22.f,6) > 1000.f);
    assert(policy::cover_score(.2f,false,.2f,2.f,22.f,6) > policy::cover_score(.2f,false,1.f,2.f,22.f,6));
    // At 7m, a shotgun position is useful while a sniper has a minimum-range penalty.
    assert(policy::cover_score(.2f,false,1.f,2.f,7.f,7) < policy::cover_score(.2f,false,1.f,2.f,7.f,8));
    assert(policy::ambush_distance(8) > policy::ambush_distance(7));
    assert(policy::ambush_distance(6) > 4.f);
    // Negative results retry with a bound, but neither a still scene nor moving threat spins every tick.
    assert(!policy::retry_empty(249u,100.f,100.f));
    assert(policy::retry_empty(250u,4.f,0.f));
    assert(!policy::retry_empty(999u,0.f,0.f));
    assert(policy::retry_empty(1000u,0.f,0.f));
    assert(!policy::failure_expired(4999u));
    assert(policy::failure_expired(5000u));
    assert(!policy::movement_stalled(249u,true));
    assert(policy::movement_stalled(250u,true));
    assert(!policy::movement_stalled(2999u,false));
    assert(policy::movement_stalled(3000u,false));
    const unsigned int start = std::numeric_limits<unsigned int>::max() - 500u;
    assert(policy::elapsed(499u,start)==1000u);
    assert(policy::retry_empty(policy::elapsed(499u,start),0.f,0.f));
    // Search cannot retain the old 120-second idle phase; grenade escape must increase separation.
    assert(!policy::search_expired(44999u));
    assert(policy::search_expired(45000u));
    assert(policy::search_step_expired(2500u));
    // Same-position observations refresh patience; repeated/older relays do not.
    assert(policy::newer_observation(105u, 100u));
    assert(!policy::newer_observation(100u, 100u));
    assert(!policy::newer_observation(90u, 100u));
    assert(policy::newer_observation(100u, start));
    assert(!policy::finish_search(60000u, true, true)); // another member still sees the enemy
    assert(!policy::finish_search(100u, true, false)); // fresh same-position report
    assert(!policy::finish_search(29999u, true, false));
    assert(policy::finish_search(30000u, true, false));
    assert(!policy::finish_search(30000u, false, false));
    assert(policy::finish_search(45000u, false, false));
    // A short fuse cannot justify a distant cover route; escape stays local even with long fuses.
    assert(policy::fuse_remaining(900u, 1100u) == 200u);
    assert(policy::fuse_remaining(1100u, 900u) == 0u);
    assert(policy::fuse_remaining(start, 499u) == 1000u);
    assert(policy::grenade_travel_budget(250u, 4.f) == 0.f);
    assert(policy::grenade_travel_budget(5000u, 0.f) == 0.f);
    assert(policy::grenade_travel_budget(1000u, 4.f) < 2.f);
    assert(policy::grenade_travel_budget(5000u, 4.f) > 10.f);
    assert(policy::grenade_escape_radius(20.f) == 4.f);
    assert(policy::grenade_escape_radius(0.f) == 1.25f);
    assert(policy::grenade_escape_radius(2.f) == 2.f);
    assert(!policy::escape_improves(9.f,9.5f));
    assert(!policy::escape_improves(9.f,4.f));
    assert(policy::escape_improves(9.f,25.f));
    std::cout << "cover tactical policy: all scenarios passed\n";
}
