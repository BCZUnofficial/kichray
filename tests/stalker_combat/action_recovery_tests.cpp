// Environment fixture only: production initialize/execute are extracted by the runner.
#include <cassert>
#include <deque>
#include <iostream>
#include <map>
#include <vector>
typedef unsigned int u32;
struct DeviceState { u32 dwTimeGlobal; } Device;
struct Fvector
{
    float x, y, z;
    Fvector(float a = 0.f, float b = 0.f, float c = 0.f) : x(a), y(b), z(c) {}
    float distance_to_sqr(const Fvector& other) const
    { return (x-other.x)*(x-other.x)+(y-other.y)*(y-other.y)+(z-other.z)*(z-other.z); }
};
enum EMovementType { eMovementTypeStand, eMovementTypeWalk, eMovementTypeRun };
enum EBodyState { eBodyStateStand };
enum EMentalState { eMentalStateDanger };
enum Property { eWorldPropertyInCover, eWorldPropertyLookedOut, eWorldPropertyPositionHolded, eWorldPropertyEnemyDetoured };
namespace MovementManager { enum PathType { ePathTypeLevelPath }; }
namespace DetailPathManager { enum PathType { eDetailPathTypeSmooth }; }
namespace stalker_combat_diagnostics
{
enum Event { movement };
bool enabled(u32) { return false; }
void log(u32, Event, const char*, const char*, ...) {}
}
struct CEntityAlive {};
struct CMemoryInfo
{
    const CEntityAlive* m_object;
    struct Params { Fvector m_position; } m_object_params;
    CMemoryInfo() : m_object(0) {}
};
struct EnemyMemory
{
    const CEntityAlive* value;
    const CEntityAlive* selected() const { return value; }
};
struct Memory
{
    EnemyMemory enemies;
    EnemyMemory& enemy() { return enemies; }
    CMemoryInfo memory(const CEntityAlive* enemy) const
    { CMemoryInfo result; result.m_object = enemy; result.m_object_params.m_position = Fvector(0,0,20); return result; }
};
struct LevelPath
{
    bool is_failed;
    bool failed() const { return is_failed; }
};
struct Movement
{
    LevelPath level;
    bool completed;
    u32 destination;
    Fvector desired;
    EMovementType type;
    unsigned int destination_changes;
    Movement() : level{false}, completed(true), destination(0), type(eMovementTypeStand), destination_changes(0) {}
    void set_desired_direction(const Fvector*) {}
    void set_path_type(MovementManager::PathType) {}
    void set_detail_path_type(DetailPathManager::PathType) {}
    void set_mental_state(EMentalState) {}
    void set_body_state(EBodyState) {}
    void set_movement_type(EMovementType movement) { type = movement; }
    void set_nearest_accessible_position() { completed = true; }
    void set_level_dest_vertex(u32 vertex) { destination = vertex; completed = false; ++destination_changes; }
    void set_desired_position(const Fvector* point) { desired = *point; }
    bool path_completed() const { return completed; }
    LevelPath& level_path() { return level; }
};
struct CCoverPoint
{
    Fvector pos;
    u32 vertex;
    CCoverPoint(u32 id, float x) : pos(x,0,0), vertex(id) {}
    const Fvector& position() const { return pos; }
    u32 level_vertex_id() const { return vertex; }
};
struct Brain { void affect_cover(bool) {} };
struct Storage
{
    std::map<Property, bool> values;
    void set_property(Property property, bool value) { values[property] = value; }
};
struct CAI_Stalker
{
    CEntityAlive target;
    Memory memories;
    Movement moving;
    Brain brains;
    Fvector pos;
    const CCoverPoint* cover;
    bool needs_search;
    unsigned int cover_queries, fires;
    u32 blocked_time;
    std::deque<const CCoverPoint*> candidates;
    std::vector<u32> abandoned;
    CAI_Stalker() : cover(0), needs_search(false), cover_queries(0), fires(0), blocked_time(0)
    { memories.enemies.value = &target; }
    Memory& memory() { return memories; }
    Movement& movement() { return moving; }
    Brain& brain() { return brains; }
    const Fvector& Position() const { return pos; }
    u32 ID() const { return 10; }
    u32 combat_fire_blocked_time() const { return blocked_time; }
    const char* combat_fire_block_reason() const { return "world"; }
    const CCoverPoint* best_cover(const Fvector&)
    {
        ++cover_queries;
        if (needs_search)
        {
            cover = candidates.empty() ? 0 : candidates.front();
            if (!candidates.empty()) candidates.pop_front();
            needs_search = false;
        }
        return cover;
    }
    void abandon_best_cover(const char*)
    {
        if (!cover) return;
        abandoned.push_back(cover->vertex);
        needs_search = true;
    }
};
class CStalkerActionCombatBase
{
public:
    CAI_Stalker& npc;
    Storage* m_storage;
    CStalkerActionCombatBase(CAI_Stalker& owner, Storage& storage) : npc(owner), m_storage(&storage) {}
    CAI_Stalker& object() { return npc; }
    void initialize() {}
    void execute() {}
    void fire() { ++npc.fires; }
};
class CStalkerActionKillEnemy : public CStalkerActionCombatBase
{
public:
    typedef CStalkerActionCombatBase inherited;
    const CCoverPoint* m_fire_recovery_cover;
    u32 m_fire_recovery_started, m_fire_recovery_settled, m_fire_recovery_last_attempt;
    bool m_fire_recovery_retry;
    CStalkerActionKillEnemy(CAI_Stalker& owner, Storage& storage) : inherited(owner, storage) {}
    void initialize();
    void execute();
};
#define SILENT_COMBAT
#include "action_recovery.generated.h"

struct Scene
{
    CCoverPoint a, b, c;
    CAI_Stalker npc;
    Storage storage;
    CStalkerActionKillEnemy action;
    Scene() : a(1,0), b(2,6), c(3,10), action(npc,storage)
    {
        Device.dwTimeGlobal = 10000;
        npc.cover = &a;
        npc.candidates.push_back(&b);
        npc.candidates.push_back(&c);
        action.initialize();
    }
    void tick(u32 elapsed)
    {
        Device.dwTimeGlobal = 10000 + elapsed;
        npc.blocked_time = elapsed;
        action.execute();
    }
};

void test_special_action_keeps_running()
{
    Scene s;
    s.tick(1700);
    assert(s.npc.abandoned.empty());
    s.tick(1800);
    assert((s.npc.abandoned == std::vector<u32>{1}));
    assert(s.npc.moving.destination == 2 && s.npc.moving.type == eMovementTypeRun);
    // The same action continues: no planner transition through TakeCover is simulated.
    for (u32 time = 1900; time < 4500; time += 100) s.tick(time);
    assert((s.npc.abandoned == std::vector<u32>{1}));
    assert(s.npc.moving.destination_changes == 1);
    s.npc.pos = s.b.position();
    s.tick(4500);
    assert(s.npc.moving.type == eMovementTypeStand);
    assert(s.storage.values[eWorldPropertyInCover]);
    s.tick(6300); // Old world-block time must not blacklist this newly reached position.
    assert((s.npc.abandoned == std::vector<u32>{1}));
    s.tick(7000);
    assert((s.npc.abandoned == std::vector<u32>{1,2}));
    assert(s.npc.moving.destination == 3 && s.npc.moving.type == eMovementTypeRun);
}

void test_failed_path_retries_without_blacklisting_unvisited_cover()
{
    Scene s;
    s.tick(1800);
    s.npc.moving.level.is_failed = true;
    s.tick(1900); // A previous path's failure flag has a short rebuild grace.
    assert((s.npc.abandoned == std::vector<u32>{1}));
    s.tick(2100);
    assert((s.npc.abandoned == std::vector<u32>{1,2}));
    assert(s.npc.moving.type == eMovementTypeStand && s.action.m_fire_recovery_retry);
    const unsigned int queries = s.npc.cover_queries;
    for (u32 time = 2200; time < 4600; time += 100) s.tick(time);
    assert(s.npc.cover_queries == queries);
    s.npc.moving.level.is_failed = false;
    s.tick(4600);
    assert((s.npc.abandoned == std::vector<u32>{1,2}));
    assert(s.npc.moving.destination == 3 && s.action.m_fire_recovery_cover == &s.c);
}

void test_external_watchdog_candidate_has_own_grace()
{
    Scene s;
    s.tick(1800);
    // Cover lifecycle independently rejects B and supplies C while the old path failed.
    s.npc.cover = &s.c;
    s.npc.moving.level.is_failed = true;
    s.tick(2200);
    assert(s.action.m_fire_recovery_cover == &s.c && s.npc.moving.destination == 3);
    assert((s.npc.abandoned == std::vector<u32>{1}));
    s.npc.moving.level.is_failed = false;
    s.tick(2500);
    assert((s.npc.abandoned == std::vector<u32>{1}));
}

void test_absent_cover_timeout_and_lost_target()
{
    Scene absent;
    absent.npc.candidates.clear();
    absent.tick(1800);
    assert(absent.npc.moving.type == eMovementTypeStand && absent.action.m_fire_recovery_retry);
    const unsigned int queries = absent.npc.cover_queries;
    absent.tick(4000);
    assert(absent.npc.cover_queries == queries && absent.npc.abandoned.size() == 1);
    absent.npc.cover = &absent.c; // The bounded outer cache can now supply a new candidate.
    absent.tick(4300);
    assert(absent.npc.abandoned.size() == 1 && absent.npc.moving.destination == 3);

    Scene timeout;
    timeout.tick(1800);
    timeout.tick(11800);
    assert((timeout.npc.abandoned == std::vector<u32>{1,2}));
    assert(timeout.npc.moving.type == eMovementTypeStand);

    Scene lost;
    lost.tick(1800);
    lost.npc.memories.enemies.value = 0;
    lost.tick(1900);
    assert(lost.npc.moving.type == eMovementTypeStand && !lost.action.m_fire_recovery_cover);
}

int main()
{
    test_special_action_keeps_running();
    test_failed_path_retries_without_blacklisting_unvisited_cover();
    test_external_watchdog_candidate_has_own_grace();
    test_absent_cover_timeout_and_lost_target();
    std::cout << "PASS: extracted KillEnemy movement, special branches, cooldown and path recovery\n";
}
