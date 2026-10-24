#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <iostream>

typedef unsigned int u32;
typedef unsigned short u16;
typedef char string64[64];
// Linux CRT adapter for the existing MSVC sized formatter. Keep production
// function names intact: inventing an xr_sprintf stub hid a Windows build error.
#ifndef _MSC_VER
#define sprintf_s std::snprintf
#endif
template <typename T, typename U> T smart_cast(U* p) { return dynamic_cast<T>(p); }
bool _valid(float value) { return std::isfinite(value); }
float clampr(float value, float low, float high) { return value < low ? low : value > high ? high : value; }
enum { eMovementTypeStand, eMovementTypeWalk, eMovementTypeRun };
enum { eBodyStateStand, eBodyStateCrouch };
struct { u32 dwTimeGlobal = 1000; } Device;
struct MotionID {
    unsigned value = 0;
    MotionID() {}
    explicit MotionID(unsigned id) : value(id) {}
    explicit operator bool() const { return value != 0; }
    void invalidate() { value = 0; }
};
struct CBlend { float timeTotal = 1.f, speed = 1.f; };
struct CKinematicsAnimated {
    std::map<std::string, MotionID> motions;
    std::map<unsigned, float> durations;
    unsigned queries = 0, plays = 0;
    MotionID ID_Cycle_Safe(const char* name) { ++queries; return motions[name]; }
    void add(const std::string& name, float duration = 1.f) {
        unsigned id = unsigned(motions.size()) + 1;
        motions[name] = MotionID(id); durations[id] = duration;
    }
};
struct CStalkerAnimationPair {
    MotionID selected;
    CBlend current;
    bool played = false;
    void reset() { selected.invalidate(); played = false; }
    void animation(MotionID value) { selected = value; }
    CBlend* blend() { return played ? &current : nullptr; }
    void play(CKinematicsAnimated* skeleton, void (*)(CBlend*), void*, bool, bool = false, u32 = 0) {
        if (!played) { ++skeleton->plays; current.timeTotal = skeleton->durations[selected.value]; played = true; }
    }
    void synchronize(CKinematicsAnimated*, const CStalkerAnimationPair&) {}
};
struct CObject { virtual ~CObject() {} u16 id = 10; u16 ID() const { return id; } };
struct CEntityAlive : CObject {};
struct CInventoryItem : CObject { CObject& object() { return *this; } };
struct CWeapon : CInventoryItem {
    enum { eIdle, eFire, eReload, eHidden };
    u32 slot = 2, state = eIdle;
    bool strapped = false;
    u32 animation_slot() const { return slot; }
    u32 GetState() const { return state; }
};
struct CObjectHandler { bool weapon_strapped(CWeapon* w) const { return w->strapped; } };
struct Inventory { CInventoryItem* active = nullptr; CInventoryItem* ActiveItem() { return active; } };
struct Movement {
    u32 target_move = eMovementTypeStand, target_body = eBodyStateStand, body = eBodyStateStand;
    u32 target_movement_type() const { return target_move; }
    u32 target_body_state() const { return target_body; }
    u32 body_state() const { return body; }
};
struct CDangerObject { enum { eDangerTypeGrenade }; u32 type() const { return eDangerTypeGrenade; } };
struct Enemy { CEntityAlive* value = nullptr; CEntityAlive* selected() { return value; } };
struct Visual { bool visible = false; bool visible_right_now(const CEntityAlive*) { return visible; } };
struct Danger { CDangerObject* value = nullptr; CDangerObject* selected() { return value; } };
struct Hit { u32 revision = 0; u32 recent_hit_revision() const { return revision; } };
struct Memory {
    Enemy enemies; Visual visuals; Danger dangers; Hit hits;
    Enemy& enemy() { return enemies; } Visual& visual() { return visuals; }
    Danger& danger() { return dangers; } Hit& hit() { return hits; }
};
struct Conditions { float value = 1.f; float health() const { return value; } };
struct CAI_Stalker : CObjectHandler {
    Inventory items; Movement motion; Memory memories; Conditions condition;
    bool alive = true, destroyed = false, scripted = false, hurt = false, critical = false, talking = false;
    bool g_Alive() const { return alive; } bool getDestroy() const { return destroyed; }
    bool GetScriptControl() const { return scripted; } bool wounded() const { return hurt; }
    bool critically_wounded() const { return critical; } bool IsTalking() const { return talking; }
    Inventory& inventory() { return items; } Movement& movement() { return motion; }
    Memory& memory() { return memories; } Conditions& conditions() { return condition; }
};
class CStalkerAnimationManager {
public:
    CStalkerAnimationPair m_search_listen, m_global, m_torso, m_legs, m_head;
    MotionID m_search_listen_motions[3];
    bool m_search_listen_active = false, m_search_listen_phase_played = false;
    u32 m_search_listen_phase = 0, m_search_listen_started = 0, m_search_listen_phase_started = 0;
    u32 m_search_listen_phase_duration = 0, m_search_listen_slot = 0, m_search_listen_hit_revision = 0;
    u16 m_search_listen_item = u16(-1);
    float m_search_listen_health = 1.f;
    CKinematicsAnimated skeleton; CKinematicsAnimated* m_skeleton_animated = &skeleton;
    CAI_Stalker npc; std::vector<int> scripts;
    bool physically_standing = true;
    unsigned normal_passes = 0, script_passes = 0, track_updates = 0;
    CAI_Stalker& object() const { return const_cast<CAI_Stalker&>(npc); }
    const std::vector<int>& script_animations() const { return scripts; }
    bool standing() const { return physically_standing; }
    CStalkerAnimationPair& global() { return m_global; }
    CStalkerAnimationPair& torso() { return m_torso; }
    CStalkerAnimationPair& legs() { return m_legs; }
    CStalkerAnimationPair& head() { return m_head; }
    void play_delayed_callbacks() {} void update_tracks() { ++track_updates; }
    bool play_script() { if (scripts.empty()) return false; ++script_passes; return true; }
    bool play_global() { return false; } void play_head() {} void play_torso() {}
    void play_legs() { ++normal_passes; }
    MotionID assign_head_animation() { return MotionID(100); }
    static void head_play_callback(CBlend*) {}
    u32 m_script_bone_part_mask = 7;
    u32 search_listen_slot() const;
    bool search_listen_allowed() const;
    bool start_search_listen();
    bool search_listen_active() const;
    void stop_search_listen();
    bool play_search_listen();
    void update_impl();
};
#include "search_listen_animation.generated.h"

void modern(CStalkerAnimationManager& manager, u32 slot)
{
    const std::string prefix = "prisluh_" + std::to_string(slot);
    manager.skeleton.add(prefix + "_in", .3f);
    manager.skeleton.add(prefix + "_1", .8f);
    manager.skeleton.add(prefix + "_out", .4f);
}

void test_slots_and_safe_fallbacks()
{
    for (u32 slot : {1u, 2u, 3u, 4u, 8u, 9u, 10u}) {
        CStalkerAnimationManager m; CWeapon weapon; weapon.slot = slot;
        m.npc.items.active = &weapon; modern(m, slot);
        assert(m.start_search_listen()); assert(m.m_search_listen_slot == slot);
        assert(m.m_search_listen_motions[1].value == m.skeleton.motions["prisluh_" + std::to_string(slot) + "_1"].value);
    }
    CStalkerAnimationManager unarmed; modern(unarmed, 0); assert(unarmed.start_search_listen());
    CStalkerAnimationManager legacy; CWeapon weapon; legacy.npc.items.active = &weapon;
    legacy.skeleton.add("prisluh_2_idle_0"); legacy.skeleton.add("prisluh_2_idle_1");
    legacy.skeleton.add("prisluh_2_idle_6"); assert(legacy.start_search_listen());
    assert(legacy.m_search_listen_motions[2].value == legacy.skeleton.motions["prisluh_2_idle_6"].value);
    CStalkerAnimationManager missing; modern(missing, 0); missing.npc.items.active = &weapon;
    assert(!missing.start_search_listen()); assert(!missing.search_listen_active());
    CStalkerAnimationManager tool; CInventoryItem item; tool.npc.items.active = &item; modern(tool, 0);
    assert(!tool.start_search_listen());
    CStalkerAnimationManager unsupported; weapon.slot = 13; unsupported.npc.items.active = &weapon; modern(unsupported, 0);
    assert(!unsupported.start_search_listen());
    CStalkerAnimationManager partial; partial.skeleton.add("prisluh_0_1");
    assert(partial.start_search_listen()); assert(partial.m_search_listen_phase == 1);
}
void test_completion_and_posture()
{
    Device.dwTimeGlobal = 1000;
    CStalkerAnimationManager m; modern(m, 0);
    m.npc.motion.body = eBodyStateCrouch;
    assert(m.start_search_listen()); m.update_impl();
    assert(m.search_listen_active() && m.skeleton.plays == 0 && m.normal_passes == 1);
    m.npc.motion.body = eBodyStateStand; m.update_impl();
    assert(m.m_search_listen_phase == 0 && m.m_search_listen_phase_duration == 300);
    Device.dwTimeGlobal = 1300; m.update_impl(); assert(m.m_search_listen_phase == 1);
    Device.dwTimeGlobal = 2100; m.update_impl(); assert(m.m_search_listen_phase == 2);
    Device.dwTimeGlobal = 2500; m.update_impl(); assert(!m.search_listen_active());
    assert(m.normal_passes == 2);
    m.npc.motion.target_body = eBodyStateCrouch; assert(!m.start_search_listen());
}
void test_interruptions_and_ownership()
{
    for (unsigned reason = 0; reason != 12; ++reason) {
        Device.dwTimeGlobal = 1000; CStalkerAnimationManager m; CWeapon weapon, second;
        CEntityAlive enemy; CDangerObject grenade;
        m.npc.items.active = &weapon; modern(m, 2); assert(m.start_search_listen()); m.update_impl();
        switch (reason) {
        case 0: m.scripts.push_back(42); break;
        case 1: m.npc.memories.enemies.value = &enemy; m.npc.memories.visuals.visible = true; break;
        case 2: m.npc.memories.dangers.value = &grenade; break;
        case 3: ++m.npc.memories.hits.revision; break;
        case 4: m.npc.condition.value = .9f; break;
        case 5: m.npc.motion.target_move = eMovementTypeRun; break;
        case 6: weapon.state = CWeapon::eReload; break;
        case 7: second.id = 11; m.npc.items.active = &second; break;
        case 8: m.npc.scripted = true; break;
        case 9: m.npc.alive = false; break;
        case 10: weapon.slot = 1; break;
        case 11: m.npc.talking = true; break;
        }
        m.update_impl(); assert(!m.search_listen_active());
        if (reason == 0) assert(m.scripts.size() == 1 && m.scripts[0] == 42 && m.script_passes == 1);
    }
    CStalkerAnimationManager m; modern(m, 0); assert(m.start_search_listen());
    m.physically_standing = false; Device.dwTimeGlobal += 6000; m.update_impl();
    assert(!m.search_listen_active());
    Device.dwTimeGlobal = 1000; assert(m.start_search_listen()); m.stop_search_listen();
    assert(!m.search_listen_active());
}
int main()
{
    test_slots_and_safe_fallbacks(); test_completion_and_posture(); test_interruptions_and_ownership();
    std::cout << "PASS: production search-listen slot selection, real-duration phases, posture wait, safe missing-clip fallback and interruption ownership\n";
}
