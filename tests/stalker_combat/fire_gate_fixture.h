#pragma once

#include "stalker_combat_fire_policy.h"
#include "stalker_combat_diagnostics.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <map>

typedef std::uint8_t u8;
typedef std::uint16_t u16;
typedef std::uint32_t u32;
typedef const char* LPCSTR;
#define IC inline

const float EPS_L = 0.0001f;
const float PI_DIV_8 = 3.14159265359f / 8.f;
template <class T> T _max(T a, T b) { return std::max(a, b); }
template <class T> T _min(T a, T b) { return std::min(a, b); }
template <class T, class U> T smart_cast(U* value) { return dynamic_cast<T>(value); }

inline float clampr(float x,float a,float b) {return std::max(a,std::min(x,b));}
struct Fvector
{
    float x, y, z;
    Fvector(float ax = 0.f, float ay = 0.f, float az = 0.f) : x(ax), y(ay), z(az) {}
    Fvector& set(float a,float b,float c) {x=a;y=b;z=c;return *this;}
    float distance_to_sqr(const Fvector& v) const {return (x-v.x)*(x-v.x)+(y-v.y)*(y-v.y)+(z-v.z)*(z-v.z);}
    Fvector& sub(const Fvector& a, const Fvector& b)
    { x = a.x - b.x; y = a.y - b.y; z = a.z - b.z; return *this; }
    Fvector& add(const Fvector& a) { x += a.x; y += a.y; z += a.z; return *this; }
    Fvector& div(float amount) { x /= amount; y /= amount; z /= amount; return *this; }
    float magnitude() const { return std::sqrt(x*x + y*y + z*z); }
    Fvector& normalize_safe() { const float size = magnitude(); if (size > EPS_L) div(size); return *this; }
    float dotproduct(const Fvector& other) const { return x*other.x + y*other.y + z*other.z; }
    float distance_to(const Fvector& other) const { Fvector delta; return delta.sub(*this, other).magnitude(); }
    void getHP(float& yaw, float& pitch) const { yaw = std::atan2(x, z); pitch = -std::atan2(y, std::sqrt(x*x + z*z)); }
    Fvector& setHP(float yaw, float pitch)
    { x = std::sin(yaw)*std::cos(pitch); y = -std::sin(pitch); z = std::cos(yaw)*std::cos(pitch); return *this; }
};

struct TestDevice { u32 dwFrame = 0; u32 dwTimeGlobal = 0; };
extern TestDevice Device;

struct CObject
{
    explicit CObject(u16 id) : object_id(id) {}
    virtual ~CObject() {}
    u16 ID() const { return object_id; }
    virtual int ef_weapon_type() const { return 0; }
    CObject& object() { return *this; }
    const CObject& object() const { return *this; }
    u16 object_id;
};

inline bool _valid(const Fvector& v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
const u16 BI_NONE=u16(-1);
struct BoneTransform {Fvector c;};
struct IRender_Visual {virtual ~IRender_Visual() {}};
struct CKinematics : IRender_Visual {
    virtual ~CKinematics() {}
    bool has_chest=true;
    BoneTransform chest;
    u16 LL_BoneID(const char*) {return has_chest?0:BI_NONE;}
    const BoneTransform& LL_GetTransform(u16) {return chest;}
};
struct Transform {
    Fvector c;
    void transform_tiny(Fvector& p,const Fvector& local) const {p=local;p.add(c);}
};
struct CEntityAlive : CObject
{
    explicit CEntityAlive(u16 id) : CObject(id) {}
    const Fvector& Position() const
    {
        // A hidden live-coordinate read must fail even if the test's final
        // fire/no-fire answer happened to look correct.
        assert(live_reads_allowed);
        ++position_reads;
        return live_position;
    }
    void Center(Fvector& result) const
    {
        assert(live_reads_allowed);
        ++center_reads;
        result = live_position;
        result.y += center_height;
    }
    Fvector live_position = Fvector(0.f, 0.f, 10.f);
    bool human=false;
    struct Renderable {IRender_Visual* visual=nullptr;} renderable;
    bool human_being() const {return human;}
    // Match CObject: Visual() has no const overload; the stored pointer remains readable.
    IRender_Visual* Visual() {assert(live_reads_allowed);return renderable.visual;}
    Transform XFORM() const {assert(live_reads_allowed);Transform t;t.c=live_position;return t;}
    float center_height = .9f;
    bool live_reads_allowed = false;
    mutable unsigned position_reads = 0;
    mutable unsigned center_reads = 0;
};

struct CInventoryItem : CObject { explicit CInventoryItem(u16 id) : CObject(id) {} };
struct CWeapon : CInventoryItem
{
    enum { eIdle, eFire };
    explicit CWeapon(u16 id) : CInventoryItem(id) {}
    int GetAmmoElapsed() const { return ammo; }
    int GetAmmoMagSize() const { return magazine; }
    int ef_weapon_type() const { return weapon_type; }
    virtual int ShotsFired() const { return fired; }
    int GetState() const { return state; }
    bool IsWorking() const { return working; }
    bool can_be_strapped() const { return true; }
    int ammo = 30, magazine = 30, weapon_type = 6, fired = 0;
    int state = eIdle;
    bool working = false;
    float camDispersion = .1f, camDispersionInc = .01f;
};
struct FakeInventory
{
    CInventoryItem* active = nullptr;
    CInventoryItem* ActiveItem() const { return active; }
};

struct ObjectParams { Fvector m_position; };
namespace MemorySpace
{
struct CVisibleObject { bool m_enabled = true; ObjectParams m_object_params; };
}
struct CMemoryInfo
{
    const CObject* m_object = nullptr;
    ObjectParams m_object_params;
};
struct FakeEnemyManager
{
    CEntityAlive* value = nullptr;
    const CEntityAlive* selected() const { return value; }
};
struct FakeVisualMemory
{
    struct Record
    {
        bool visible = false, has_snapshot = false;
        u32 direct_time = u32(-1), shared_time = u32(-1);
        MemorySpace::CVisibleObject snapshot;
    };
    std::map<u16, Record> records;
    mutable unsigned shared_time_reads = 0;
    const Record* find(const CEntityAlive* enemy) const
    {
        const auto i = records.find(enemy->ID());
        return i == records.end() ? nullptr : &i->second;
    }
    bool visible_right_now(const CEntityAlive* enemy) const
    { const Record* r = find(enemy); return r && r->visible; }
    const MemorySpace::CVisibleObject* visible_object(const CEntityAlive* enemy) const
    { const Record* r = find(enemy); return r && r->has_snapshot ? &r->snapshot : nullptr; }
    u32 direct_visible_time(const CEntityAlive* enemy) const
    { const Record* r = find(enemy); return r ? r->direct_time : u32(-1); }
    u32 visible_object_time_last_seen(const CEntityAlive* enemy) const
    {
        ++shared_time_reads;
        const Record* r = find(enemy);
        return r ? r->shared_time : u32(-1);
    }
};
struct FakeMemory
{
    FakeEnemyManager enemies;
    FakeVisualMemory visuals;
    std::map<u16, CMemoryInfo> known;
    FakeEnemyManager& enemy() { return enemies; }
    const FakeEnemyManager& enemy() const { return enemies; }
    FakeVisualMemory& visual() { return visuals; }
    const FakeVisualMemory& visual() const { return visuals; }
    CMemoryInfo memory(const CEntityAlive* enemy) const
    { const auto i = known.find(enemy->ID()); return i == known.end() ? CMemoryInfo() : i->second; }
};

struct FakeShotEffector
{
    u32 seed = 0;
    unsigned calls = 0;
    float last_dispersion = 0.f;
    void SetRndSeed(u32 value) { seed = value; }
    void Shot(float value) { ++calls; last_dispersion = value; }
};
namespace collide { struct rq_results { void r_clear() {} }; }
struct RayResult
{
    RayResult(float range = 50.f, bool foe = false, bool friend_hit = false)
        : distance(range), enemy(foe), friendly(friend_hit) {}
    float distance;
    bool enemy, friendly;
};

namespace MonsterSpace
{
enum EObjectAction
{
    eObjectActionSwitch1, eObjectActionSwitch2, eObjectActionAim1, eObjectActionAim2,
    eObjectActionFire1, eObjectActionFire2, eObjectActionIdle, eObjectActionStrapped,
    eObjectActionDrop, eObjectActionActivate, eObjectActionDeactivate, eObjectActionAimReady1,
    eObjectActionAimReady2, eObjectActionAimForceFull1, eObjectActionAimForceFull2, eObjectActionUse
};
}
class CObjectHandlerPlanner;
struct CObjectHandler
{
    CObjectHandlerPlanner* fixture_handler = nullptr;
    void set_goal(MonsterSpace::EObjectAction, CInventoryItem*, u32, u32, u32, u32);
};
namespace SightManager { enum ESightType { eSightTypePosition }; }
struct CSightAction
{
    CSightAction(SightManager::ESightType, const Fvector& value, bool) : point(value) {}
    Fvector point;
};
struct FakeSightManager
{
    Fvector point;
    void setup(const CSightAction& action) { point = action.point; }
};

class CAI_Stalker : public CObjectHandler
{
public:
    FakeMemory memories;
    FakeInventory items;
    FakeShotEffector effector;
    FakeSightManager sight_manager;
    u32 fixture_min_queue = 12, fixture_max_queue = 12;
    Fvector own_position;
    Fvector fixture_origin = Fvector(0.f, .9f, 0.f);
    Fvector fixture_direction = Fvector(0.f, 0.f, 1.f);
    RayResult rays[5];
    unsigned fire_param_calls = 0, ray_calls = 0, ray_index = 0;
    bool fixture_alive = true, fixture_memory_available = true;
    mutable unsigned memory_accesses = 0;
    bool g_Alive() const { return fixture_alive; }
    FakeMemory& memory()
    { assert(fixture_memory_available); ++memory_accesses; return memories; }
    const FakeMemory& memory() const
    { assert(fixture_memory_available); ++memory_accesses; return memories; }
    FakeInventory& inventory() { return items; }
    const FakeInventory& inventory() const { return items; }
    CInventoryItem* best_weapon() { return items.active; }
    FakeSightManager& sight() { return sight_manager; }
    u32 min_queue_size_close() const { return fixture_min_queue; }
    u32 min_queue_size_medium() const { return fixture_min_queue; }
    u32 min_queue_size_far() const { return fixture_min_queue; }
    u32 max_queue_size_close() const { return fixture_max_queue; }
    u32 max_queue_size_medium() const { return fixture_max_queue; }
    u32 max_queue_size_far() const { return fixture_max_queue; }
    u32 min_queue_interval_close() const { return 300; }
    u32 min_queue_interval_medium() const { return 300; }
    u32 min_queue_interval_far() const { return 300; }
    u32 max_queue_interval_close() const { return 300; }
    u32 max_queue_interval_medium() const { return 300; }
    u32 max_queue_interval_far() const { return 300; }
    FakeShotEffector& weapon_shot_effector() { return effector; }
    const Fvector& Position() const { return own_position; }
    u16 ID() const { return 7; }

    // Only engine/environment boundaries are stubbed. The production center
    // capture, five-ray sweep, frame cache and gate methods are extracted.
    void g_fireParams(int, Fvector& position, Fvector& direction)
    { ++fire_param_calls; ray_index = 0; position = fixture_origin; direction = fixture_direction; }
    void can_kill_entity(const Fvector&, const Fvector&, float, collide::rq_results&)
    {
        assert(ray_index < 5);
        ++ray_calls;
        const RayResult& result = rays[ray_index++];
        m_can_kill_enemy = m_can_kill_enemy || result.enemy;
        m_can_kill_member = m_can_kill_member || result.friendly;
        m_pick_distance = _max(m_pick_distance, result.distance);
    }

    u32 m_pick_frame_id = u32(-1);
    float m_pick_distance = 0.f;
    bool m_can_kill_enemy = false, m_can_kill_member = false;
    collide::rq_results rq_storage;
    Fvector m_combat_fire_position;
    Fvector m_combat_fire_direction = Fvector(0.f, 0.f, 1.f);
    Fvector m_combat_target_offset = Fvector(0.f, .9f, 0.f);
    u16 m_combat_target_offset_id = u16(-1), m_combat_fire_enemy_id = u16(-1), m_combat_fire_weapon_id = u16(-1);
    u32 m_combat_last_visual_time = u32(-1), m_combat_suppression_shots = 0;
    u32 m_combat_fire_blocked_since = u32(-1), m_combat_fire_gate_frame = u32(-1);
    float m_combat_center_pick_distance = 0.f;
    bool m_combat_center_can_kill_enemy = false, m_combat_aim_locked = false, m_combat_fire_allowed = false;
    u8 m_combat_fire_block_reason = 0;
    u32 m_weapon_shot_random_seed = 1357;

    void can_kill_entity_from(const Fvector&, Fvector, float);
    float start_pick_distance() const;
    void update_can_kill_info();
    bool combat_target_position(Fvector&, bool aim_point = false);
    LPCSTR combat_fire_block_reason() const;
    u32 combat_fire_blocked_time() const;
    u32 combat_suppression_remaining() const;
    bool combat_fire_solution();
    bool fire_make_sense();
    void on_weapon_shot_start(CWeapon*);
};
