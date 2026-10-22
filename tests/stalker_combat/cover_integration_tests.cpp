// The lifecycle methods below are extracted from the production source by the runner.
// Geometry queries are controlled here so cache, callbacks and path state can be tested.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include "../../xr_3da/xrGame/stalker_combat_cover_policy.h"

typedef unsigned int u32;
static const float flt_max = std::numeric_limits<float>::max();
static const float MIN_SUITABLE_ENEMY_DISTANCE = 3.f;
template <typename T> T _sqr(T value) { return value * value; }
struct { u32 dwTimeGlobal; } Device;

struct Fvector
{
    float x, y, z;
    Fvector(float px = 0.f, float py = 0.f, float pz = 0.f) : x(px), y(py), z(pz) {}
    float distance_to_sqr(const Fvector& other) const
    { return _sqr(x - other.x) + _sqr(y - other.y) + _sqr(z - other.z); }
};
struct CCoverPoint
{
    u32 vertex;
    Fvector point;
    float score, exposure, weight;
    bool suitable;
    CCoverPoint(u32 id, float x, float value)
        : vertex(id), point(x, 0.f, 0.f), score(value), exposure(.2f), weight(1.f), suitable(true) {}
    const Fvector& position() const { return point; }
    u32 level_vertex_id() const { return vertex; }
};
namespace MonsterSpace { enum { eMovementTypeStand, eMovementTypeWalk, eMovementTypeRun }; }
struct FakePath { bool is_failed = false; bool failed() const { return is_failed; } };
struct FakeMovement
{
    int intended = MonsterSpace::eMovementTypeStand;
    u32 destination = u32(-1);
    bool completed = false, available = true;
    FakePath path;
    int target_movement_type() const { return intended; }
    u32 level_dest_vertex_id() const { return destination; }
    bool path_completed() const { return completed; }
    bool accessible(const Fvector&) const { return available; }
    FakePath& level_path() { return path; }
};
class CAI_Stalker;
struct FakeMember
{
    const CCoverPoint* reserved = 0;
    void cover(const CCoverPoint* point) { reserved = point; }
};
struct FakeMembers
{
    FakeMember state;
    FakeMember& member(CAI_Stalker*) { return state; }
};
struct FakeLocation
{
    FakeMembers& members;
    unsigned int reservations = 0;
    explicit FakeLocation(FakeMembers& value) : members(value) {}
    void make_suitable(CAI_Stalker*, const CCoverPoint* point)
    { members.state.cover(point); ++reservations; }
};
struct FakeAgent
{
    FakeMembers members;
    FakeLocation locations;
    FakeAgent() : locations(members) {}
    FakeMembers& member() { return members; }
    FakeLocation& location() { return locations; }
};
class CStalkerMovementRestrictor
{
public:
    CStalkerMovementRestrictor(CAI_Stalker*, bool, bool = true) {}
    bool operator()(const CCoverPoint* cover) const { return cover->suitable; }
    float weight(const CCoverPoint* cover) const { return cover->weight; }
};
class CStalkerCombatCoverEvaluator
{
    float value = flt_max;
public:
    explicit CStalkerCombatCoverEvaluator(CAI_Stalker*) {}
    void setup(const Fvector&, float, float, float) {}
    void initialize(const Fvector&, bool) {}
    void evaluate(const CCoverPoint* cover, float weight) { if (weight > 0.f) value = cover->score; }
    float best_value() const { return value; }
};
float combat_cover_exposure(const CCoverPoint* cover, const Fvector&) { return cover->exposure; }
namespace stalker_combat_diagnostics
{
enum Event { movement, cover };
bool enabled(u32) { return false; }
void log(u32, Event, const char*, const char*, ...) {}
}

class CAI_Stalker
{
public:
    const CCoverPoint* m_best_cover;
    bool m_best_cover_actual;
    float m_best_cover_value;
    bool m_best_cover_can_try_advance;
    const CCoverPoint* m_best_cover_advance_cover;
    u32 m_best_cover_last_search_time, m_best_cover_last_change_time;
    Fvector m_best_cover_search_position, m_best_cover_enemy_position;
    float m_best_cover_exposure;
    struct SCombatCoverFailure { const CCoverPoint* cover; u32 time; } m_failed_combat_covers[4];
    u32 m_failed_combat_cover_next;
    const CCoverPoint* m_cover_progress_point;
    Fvector m_cover_progress_position;
    u32 m_cover_progress_time;

    Fvector position;
    FakeMovement moves;
    FakeAgent agent;
    const CCoverPoint* query_result = 0;
    unsigned int queries = 0, callbacks = 0;
    const CCoverPoint* callback_old = 0;
    const CCoverPoint* callback_new = 0;
    CAI_Stalker() { reset_best_cover_state(); }
    const Fvector& Position() const { return position; }
    u32 ID() const { return 7; }
    FakeMovement& movement() { return moves; }
    FakeAgent& agent_manager() { return agent; }
    void on_best_cover_changed(const CCoverPoint* point, const CCoverPoint* old)
    {
        assert(m_best_cover == old);
        ++callbacks;
        callback_old = old;
        callback_new = point;
    }
    const CCoverPoint* find_best_cover(const Fvector&) { ++queries; return query_result; }
    float best_cover_value(const Fvector&) { return m_best_cover ? m_best_cover->score : flt_max; }
    void reset_best_cover_state();
    void invalidate_best_cover();
    bool combat_cover_blocked(const CCoverPoint*) const;
    void abandon_best_cover(const char*);
    bool update_cover_progress();
    void accept_best_cover(const CCoverPoint*, const Fvector&);
    void best_cover_can_try_advance();
    void update_best_cover_actuality(const Fvector&);
    const CCoverPoint* best_cover(const Fvector&);
};

#include "cover_methods.generated.h"

const Fvector threat(30.f, 0.f, 0.f);

void test_accept_and_advance()
{
    Device.dwTimeGlobal = 10000;
    CAI_Stalker npc;
    CCoverPoint a(10, 5.f, .8f), b(11, 7.f, .1f);
    assert(!npc.m_best_cover && !npc.m_best_cover_can_try_advance && !npc.m_best_cover_advance_cover);
    npc.query_result = &a;
    assert(npc.best_cover(threat) == &a);
    assert(npc.queries == 1 && npc.callbacks == 1);
    assert(npc.callback_old == 0 && npc.callback_new == &a);
    assert(npc.agent.members.state.reserved == &a && npc.m_best_cover_value == .8f);

    Device.dwTimeGlobal += 1000;
    npc.best_cover_can_try_advance();
    npc.query_result = &b;
    assert(npc.best_cover(threat) == &b);
    assert(npc.queries == 2 && npc.callbacks == 2);
    assert(npc.callback_old == &a && npc.callback_new == &b);
    assert(npc.m_best_cover_value == .1f && npc.agent.members.state.reserved == &b);
    assert(!npc.m_best_cover_can_try_advance && !npc.m_best_cover_advance_cover);

    Device.dwTimeGlobal += 1000;
    npc.best_cover_can_try_advance();
    npc.query_result = 0;
    assert(npc.best_cover(threat) == &b); // An empty advance search preserves safe cover.
    assert(npc.queries == 3 && npc.callbacks == 2);
    assert(npc.m_best_cover_actual && npc.m_best_cover_value == .1f);
    npc.accept_best_cover(&b, threat);
    assert(npc.callbacks == 2);
}

void test_change_hysteresis()
{
    Device.dwTimeGlobal = 20000;
    CAI_Stalker npc;
    CCoverPoint a(10, 5.f, .8f), b(11, 7.f, .1f);
    npc.accept_best_cover(&a, threat);
    npc.query_result = &b;
    Device.dwTimeGlobal += 749;
    npc.invalidate_best_cover();
    assert(npc.best_cover(threat) == &a);
    Device.dwTimeGlobal += 1;
    npc.invalidate_best_cover();
    assert(npc.best_cover(threat) == &b);
    assert(npc.callbacks == 2);
    a.score = .05f; // Less than 0.15 improvement does not churn the selected point.
    npc.query_result = &a;
    Device.dwTimeGlobal += 1000;
    npc.best_cover_can_try_advance();
    assert(npc.best_cover(threat) == &b && npc.callbacks == 2);
    b.suitable = false; // Hard invalidation bypasses the improvement threshold.
    npc.invalidate_best_cover();
    assert(npc.best_cover(threat) == &a && npc.callbacks == 3);
}

void test_negative_cache()
{
    Device.dwTimeGlobal = 30000;
    CAI_Stalker npc;
    assert(!npc.best_cover(threat) && npc.queries == 1 && npc.callbacks == 0);
    Device.dwTimeGlobal += 999;
    assert(!npc.best_cover(threat) && npc.queries == 1);
    Device.dwTimeGlobal += 1;
    assert(!npc.best_cover(threat) && npc.queries == 2);
    npc.position.x = 2.f;
    Device.dwTimeGlobal += 249;
    assert(!npc.best_cover(threat) && npc.queries == 2);
    Device.dwTimeGlobal += 1;
    assert(!npc.best_cover(threat) && npc.queries == 3);
    Device.dwTimeGlobal += 250;
    const Fvector moved_threat(33.f, 0.f, 0.f);
    assert(!npc.best_cover(moved_threat) && npc.queries == 4);
}

void begin_movement(CAI_Stalker& npc, const CCoverPoint& point)
{
    npc.accept_best_cover(&point, threat);
    npc.moves.intended = MonsterSpace::eMovementTypeRun;
    npc.moves.destination = point.level_vertex_id();
}

void test_path_failure_and_blacklist()
{
    Device.dwTimeGlobal = 40000;
    CAI_Stalker npc;
    CCoverPoint a(10, 5.f, .8f), b(11, 7.f, .1f);
    begin_movement(npc, a);
    npc.moves.path.is_failed = true;
    assert(!npc.update_cover_progress());
    Device.dwTimeGlobal += 249;
    assert(!npc.update_cover_progress());
    Device.dwTimeGlobal += 1;
    assert(npc.update_cover_progress());
    assert(!npc.m_best_cover_actual && npc.combat_cover_blocked(&a));
    assert(!npc.agent.members.state.reserved && !npc.m_cover_progress_point);
    npc.query_result = &b;
    assert(npc.best_cover(threat) == &b && npc.callbacks == 2);
    assert(npc.callback_old == &a && npc.callback_new == &b);
    Device.dwTimeGlobal += 4999;
    assert(npc.combat_cover_blocked(&a));
    Device.dwTimeGlobal += 1;
    assert(!npc.combat_cover_blocked(&a));
}

void test_no_progress_and_safe_standing()
{
    Device.dwTimeGlobal = 50000;
    CAI_Stalker npc;
    CCoverPoint a(10, 5.f, .8f);
    begin_movement(npc, a);
    assert(!npc.update_cover_progress());
    Device.dwTimeGlobal += 2999;
    assert(!npc.update_cover_progress());
    npc.position.x = .4f;
    assert(!npc.update_cover_progress()); // Actual progress restarts the window.
    Device.dwTimeGlobal += 2999;
    assert(!npc.update_cover_progress());
    Device.dwTimeGlobal += 1;
    assert(npc.update_cover_progress() && npc.combat_cover_blocked(&a));

    npc.reset_best_cover_state();
    begin_movement(npc, a);
    npc.moves.intended = MonsterSpace::eMovementTypeStand;
    npc.moves.path.is_failed = true;
    Device.dwTimeGlobal += 20000;
    assert(!npc.update_cover_progress() && !npc.combat_cover_blocked(&a));
    npc.moves.intended = MonsterSpace::eMovementTypeRun;
    npc.moves.completed = true;
    assert(!npc.update_cover_progress());
    npc.moves.completed = false;
    npc.moves.destination = 999;
    assert(!npc.update_cover_progress());
    npc.moves.destination = a.vertex;
    npc.position = a.point;
    assert(!npc.update_cover_progress() && !npc.combat_cover_blocked(&a));
}

void test_exposure_and_outer_invalidation()
{
    Device.dwTimeGlobal = 80000;
    CAI_Stalker npc;
    CCoverPoint a(10, 5.f, .8f);
    npc.accept_best_cover(&a, threat);
    a.exposure += .25f;
    Device.dwTimeGlobal += 749;
    npc.update_best_cover_actuality(threat);
    assert(npc.m_best_cover_actual);
    Device.dwTimeGlobal += 1;
    npc.update_best_cover_actuality(threat);
    assert(!npc.m_best_cover_actual);
    npc.accept_best_cover(&a, threat);
    npc.invalidate_best_cover();
    assert(!npc.m_best_cover_actual && npc.m_best_cover == &a && npc.callbacks == 1);
    npc.accept_best_cover(0, threat);
    assert(npc.callbacks == 2 && npc.callback_old == &a && !npc.callback_new);
    assert(npc.m_best_cover_value == flt_max && !npc.agent.members.state.reserved);
}

int main()
{
    test_accept_and_advance();
    test_change_hysteresis();
    test_negative_cache();
    test_path_failure_and_blacklist();
    test_no_progress_and_safe_standing();
    test_exposure_and_outer_invalidation();
    std::puts("PASS: 6 production cover lifecycle groups (cache, callbacks, progress, blacklist, hysteresis)");
}
