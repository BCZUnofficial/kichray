#pragma once
#include <cstddef>

typedef int BOOL;
struct Fvector { float x, y, z; };
// Deliberately has no enemy or memory interface: danger reactions need neither.
class CAI_Stalker
{
public:
    struct { Fvector c, k; } eye_matrix;
};
namespace collide
{
    enum rq_target { rqtStatic, rqtBoth };
    struct rq_result { float range; };
}
struct TestObjectSpace
{
    bool hit;
    float hit_range;
    unsigned queries;
    BOOL RayPick(const Fvector&, const Fvector&, float, collide::rq_target,
                 collide::rq_result&, void*);
};
struct TestLevel { TestObjectSpace ObjectSpace; };
TestLevel& Level();
