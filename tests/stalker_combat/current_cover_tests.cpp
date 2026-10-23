#include "current_cover_fixture.h"
#include "current_cover_consumer.generated.h"
#include <cassert>
#include <cstdio>

TestLevel& Level() { static TestLevel level = {}; return level; }

BOOL TestObjectSpace::RayPick(const Fvector& origin, const Fvector& direction,
                            float distance, collide::rq_target target,
                            collide::rq_result& result, void* ignored)
{
    ++queries;
    assert(origin.x == 11.f && origin.y == 22.f && origin.z == 33.f);
    assert(direction.x == .25f && direction.y == .5f && direction.z == .75f);
    assert(distance == 10.f && target == collide::rqtStatic && ignored == NULL);
    result.range = hit_range;
    return hit;
}

int main()
{
    CAI_Stalker stalker;
    stalker.eye_matrix.c = {11.f, 22.f, 33.f};
    stalker.eye_matrix.k = {.25f, .5f, .75f};
    TestObjectSpace& space = Level().ObjectSpace;
    space.hit = false;
    space.hit_range = .1f; // A miss must not expose stale result storage.
    assert(current_cover(&stalker) == 100.f);
    space.hit = true;
    const float ranges[] = {2.75f, 3.f, 3.25f};
    for (unsigned i = 0; i != 3; ++i)
    {
        space.hit_range = ranges[i];
        const float distance = current_cover(&stalker);
        assert(distance == ranges[i]);
        assert((distance >= 3.f) == (i != 0));
        assert((distance < 3.f) == (i == 0));
    }
    assert(space.queries == 4);
    std::puts("PASS: current_cover external linkage and danger ray behavior");
}
