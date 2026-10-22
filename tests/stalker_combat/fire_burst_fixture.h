#pragma once

#include "fire_gate_fixture.h"
#include <cstdlib>
#include <vector>

#define VERIFY(value) assert(value)
#define NODEFAULT std::abort()
typedef CObject CGameObject;

// The test invokes the actual weapon while-loop/equality block extracted from
// state_Fire. Rendering, ballistics and state-transition scheduling are stubs.
struct CWeaponMagazined : CWeapon
{
    explicit CWeaponMagazined(u16 id, CAI_Stalker& owner) : CWeapon(id), holder(owner), m_magazine(30, 1) {}
    CAI_Stalker& holder;
    std::vector<int> m_magazine;
    int m_iQueueSize = -1, m_iShotNum = 0, m_iShootEffectorStart = 0;
    bool m_bStopedAfterQueueFired = false, m_bFireSingleShot = false;
    float fTime = 0.f, fTimeToFire = 1.f;
    Fvector m_vStartPos, m_vStartDir;
    unsigned queue_writes = 0, stops = 0;
    int GetQueueSize() const { return m_iQueueSize; }
    void SetQueueSize(int value) { m_iQueueSize = value; ++queue_writes; }
    int ShotsFired() const override { return m_iShotNum; }
    bool StopedAfterQueueFired() const { return m_bStopedAfterQueueFired; }
    void StopedAfterQueueFired(bool value) { m_bStopedAfterQueueFired = value; }
    void FireEnd() { working = false; ++stops; }
    void start_burst()
    {
        state = eFire;
        working = true;
        m_bStopedAfterQueueFired = false;
        m_bFireSingleShot = true;
        m_iShotNum = 0;
    }
    void OnShot() { holder.on_weapon_shot_start(this); }
    void FireTrace(const Fvector&, const Fvector&) { m_magazine.pop_back(); --ammo; }
    void advance_for_test(int shots_available);
};

namespace ObjectHandlerSpace
{
enum EWorldProperties
{
    eWorldPropertySwitch1, eWorldPropertySwitch2, eWorldPropertyAimingReady1,
    eWorldPropertyAiming2, eWorldPropertyFiring1, eWorldPropertyFiring2,
    eWorldPropertyIdle, eWorldPropertyIdleStrap, eWorldPropertyDropped,
    eWorldPropertyNoItemsIdle, eWorldPropertyAimingReady2,
    eWorldPropertyAimForceFull1, eWorldPropertyAimForceFull2, eWorldPropertyUsed,
    eWorldPropertyDummy
};
enum { eWorldOperatorQueueWait1 = 30, eWorldOperatorQueueWait2 = 31 };
}
struct CWorldProperty
{
    CWorldProperty(u32 id, bool enabled) : condition(id), value(enabled) {}
    u32 condition;
    bool value;
};
struct CState
{
    u32 target = 0;
    void add_condition(const CWorldProperty& property) { assert(property.value); target = property.condition; }
};
struct FakeQueueAction
{
    u32 interval = 0;
    void set_inertia_time(u32 value) { interval = value; }
};
struct FixtureRandom
{
    int randI(u32 minimum, u32 maximum) { assert(maximum > minimum); return int(maximum - 1u); }
};
extern FixtureRandom Random;

class CObjectHandlerPlanner
{
public:
    typedef ObjectHandlerSpace::EWorldProperties EWorldProperties;
    explicit CObjectHandlerPlanner(CAI_Stalker& owner) : holder(owner) { owner.fixture_handler = this; }
    CAI_Stalker& holder;
    std::map<u32, FakeQueueAction> actions;
    u32 goal = 0;
    u32 m_min_queue_size = 0, m_max_queue_size = 0, m_min_queue_interval = 0, m_max_queue_interval = 0;
    u32 m_queue_size = 0, m_queue_interval = 0, m_next_time_change = 0;
    u16 m_queue_weapon_id = u16(-1);
    CAI_Stalker& object() { return holder; }
    u32 uid(u32 id, u32 property) const { return (id << 16u) | property; }
    void set_target_state(const CState& target) { goal = target.target; }
    FakeQueueAction& action(u32 id) { return actions[id]; }
    EWorldProperties object_property(MonsterSpace::EObjectAction) const;
    void set_goal(MonsterSpace::EObjectAction, CGameObject*, u32, u32, u32, u32);
};

inline void CObjectHandler::set_goal(MonsterSpace::EObjectAction action, CInventoryItem* item,
    u32 minimum, u32 maximum, u32 min_interval, u32 max_interval)
{
    assert(fixture_handler);
    fixture_handler->set_goal(action, item, minimum, maximum, min_interval, max_interval);
}

class CStalkerActionCombatBase
{
public:
    explicit CStalkerActionCombatBase(CAI_Stalker& owner) : holder(owner) {}
    CAI_Stalker& holder;
    mutable int m_queue_distance_band = -1;
    mutable u16 m_queue_target_id = u16(-1);
    CAI_Stalker& object() const { return holder; }
    void fire();
    void aim_ready();
    void aim_ready_force_full();
    void select_queue_params(const float&, u32&, u32&, u32&, u32&) const;
};
