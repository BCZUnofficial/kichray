////////////////////////////////////////////////////////////////////////////
//	Module 		: stalker_animation_global.cpp
//	Created 	: 25.02.2003
//  Modified 	: 19.11.2004
//	Author		: Dmitriy Iassenev
//	Description : Stalker animation manager : global animations
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "pch_script.h" // Complete luabind types used by enemy_manager.h callbacks.
#include "stalker_animation_manager.h"
#include "ai/stalker/ai_stalker.h"
#include "inventory.h"
#include "fooditem.h"
#include "property_storage.h"
#include "stalker_movement_manager.h"
#include "ai/stalker/ai_stalker_space.h"
#include "stalker_animation_data.h"
#include "weapon.h"
#include "missile.h"
#include "stalker_animation_manager_impl.h"
#include "memory_manager.h"
#include "enemy_manager.h"
#include "visual_memory_manager.h"
#include "hit_memory_manager.h"
#include "danger_manager.h"
#include "entitycondition.h"

using namespace StalkerSpace;

void CStalkerAnimationManager::global_play_callback(CBlend* blend)
{
	CAI_Stalker* object = (CAI_Stalker*)blend->CallbackParam;
	VERIFY(object);

	CStalkerAnimationPair& pair = object->animation().global();
	pair.on_animation_end();
}

MotionID CStalkerAnimationManager::global_critical_hit()
{
	if (!object().critically_wounded())
		return (MotionID());

	if (global().animation())
		return (global().animation());

	CWeapon* weapon = smart_cast<CWeapon*>(object().inventory().ActiveItem());
	VERIFY2(weapon, make_string("current active item: %s", object().inventory().ActiveItem()
															   ? *object().inventory().ActiveItem()->object().cName()
															   : "no active item"));

	u32 animation_slot = weapon->animation_slot();
	VERIFY(animation_slot >= 1);
	VERIFY(animation_slot <= 3);

	return (global().select(m_data_storage->m_part_animations.A[eBodyStateStand]
								.m_global.A[object().critical_wound_type() + 6 * (animation_slot - 1)]
								.A,
							&object().critical_wound_weights()));
}

MotionID CStalkerAnimationManager::assign_global_animation()
{
	if (eMentalStatePanic != object().movement().mental_state())
		return (global_critical_hit());

	if (fis_zero(object().movement().speed(object().character_physics_support()->movement())))
		return (MotionID());

	return (global().select(m_data_storage->m_part_animations.A[body_state()].m_global.A[1].A));
}


u32 CStalkerAnimationManager::search_listen_slot() const
{
    CInventoryItem* item = object().inventory().ActiveItem();
    if (!item) return 0;
    CWeapon* weapon = smart_cast<CWeapon*>(item);
    if (!weapon) return u32(-1); // Do not disguise a missile/tool as empty hands.
    if (object().CObjectHandler::weapon_strapped(weapon)) return 0;
    const u32 slot = weapon->animation_slot();
    switch (slot)
    {
    case 1: case 2: case 3: case 4: case 8: case 9: case 10: return slot;
    default: return u32(-1);
    }
}

bool CStalkerAnimationManager::search_listen_allowed() const
{
    if (!object().g_Alive() || object().getDestroy() || object().GetScriptControl() ||
        object().wounded() || object().critically_wounded() || object().IsTalking() ||
        !script_animations().empty() || object().movement().target_movement_type() != eMovementTypeStand ||
        object().movement().target_body_state() != eBodyStateStand) return false;
    const CEntityAlive* enemy = object().memory().enemy().selected();
    if (enemy && object().memory().visual().visible_right_now(enemy)) return false;
    const CDangerObject* danger = object().memory().danger().selected();
    if (danger && danger->type() == CDangerObject::eDangerTypeGrenade) return false;
    CInventoryItem* item = object().inventory().ActiveItem();
    CWeapon* weapon = smart_cast<CWeapon*>(item);
    if (item && (!weapon || weapon->GetState() != CWeapon::eIdle)) return false;
    return true;
}

bool CStalkerAnimationManager::start_search_listen(bool crouched)
{
    stop_search_listen();
    if (!search_listen_allowed()) return false;
    const u32 slot = search_listen_slot();
    if (slot == u32(-1)) return false;

    // The body-state remains standing while authored full-body transitions
    // kneel/bend/listen. Crouched describes the search posture before entry.
    const bool long_gun = slot == 2u || slot == 3u || slot == 8u || slot == 9u;
    struct Family { u32 slot; bool legacy; bool transitions_required; };
    struct Candidate { MotionID in, idle, out; };
    Candidate candidates[24];
    u32 count = 0;
    for (u32 pass = 0; pass != 2 && !count; ++pass)
    {
        Family families[5];
        u32 family_count = 0;
        if (long_gun && crouched && pass == 0)
        {
            // Only the legacy family 3 is the observed kneeling rifle gesture.
            // Modern slot 3 remains the weapon-specific shotgun animation.
            families[family_count++] = Family{3u, true, true};
        }
        else
        {
            families[family_count++] = Family{slot, false, false};
            if (long_gun)
            {
                families[family_count++] = Family{2u, true, true};
                if (slot != 8u) families[family_count++] = Family{8u, false, true};
                // A model may provide only the original weapon-slot family.
                if (slot == 3u) families[family_count++] = Family{3u, true, true};
            }
            else if (slot <= 4u)
                families[family_count++] = Family{slot, true, false};
        }
        for (u32 f = 0; f != family_count; ++f)
        {
            const Family& family = families[f];
            // Canonical SoC names, plus the literal custom-model names supplied
            // by the user. Resolve once at entry, never during the render loop.
            const char* prefixes[] = {"prisluh", "prushluh", "pprushluh"};
            for (u32 alias = 0; alias != 3; ++alias)
            {
                if (alias == 2 && (family.slot != 8u || family.legacy)) continue;
                string64 name;
                sprintf_s(name, sizeof(name), family.legacy ? "%s_%u_idle_0" : "%s_%u_in",
                    prefixes[alias], family.slot);
                const MotionID entry = m_skeleton_animated->ID_Cycle_Safe(name);
                if (family.legacy)
                    sprintf_s(name, sizeof(name), "%s_%u_idle_%u", prefixes[alias],
                        family.slot, family.slot == 2u ? 6u : 5u);
                else
                    sprintf_s(name, sizeof(name), "%s_%u_out", prefixes[alias], family.slot);
                const MotionID exit = m_skeleton_animated->ID_Cycle_Safe(name);
                if (family.transitions_required && (!entry || !exit)) continue;
                const u32 before = count;
                // xr_state: entry 0, idle 1..4 (family 2 also 5), exit 5/6.
                // state_mgr_animation_list: idle 0..2, entry/exit named in/out.
                const u32 first = family.legacy ? 1u : 0u;
                const u32 end = family.legacy ? (family.slot == 2u ? 6u : 5u) : 3u;
                for (u32 variant = first; variant != end; ++variant)
                {
                    sprintf_s(name, sizeof(name), family.legacy ? "%s_%u_idle_%u" : "%s_%u_%u",
                        prefixes[alias], family.slot, variant);
                    const MotionID idle = m_skeleton_animated->ID_Cycle_Safe(name);
                    if (!idle) continue;
                    bool duplicate = false;
                    for (u32 c = 0; c != count; ++c)
                        if (candidates[c].idle == idle) { duplicate = true; break; }
                    if (!duplicate && count < 24u)
                        candidates[count++] = Candidate{entry, idle, exit};
                }
                if (count != before) break; // Do not mix spellings within a family.
            }
        }
    }
    if (!count) return false;
    // Every existing idle/rnd clip is eligible, except the previous selection
    // when alternatives exist. Entry and exit always belong to its own family.
    if (count > 1u)
        for (u32 c = 0; c != count; ++c)
            if (candidates[c].idle == m_search_listen_last_motion)
            {
                candidates[c] = candidates[--count];
                break;
            }
    const Candidate& chosen = candidates[::Random.randI(count)];
    m_search_listen_motions[0] = chosen.in;
    m_search_listen_motions[1] = chosen.idle;
    m_search_listen_motions[2] = chosen.out;
    m_search_listen_last_motion = chosen.idle;

    m_search_listen_slot = slot;
    CInventoryItem* item = object().inventory().ActiveItem();
    m_search_listen_item = item ? item->object().ID() : u16(-1);
    m_search_listen_hit_revision = object().memory().hit().recent_hit_revision();
    m_search_listen_health = object().conditions().health();
    m_search_listen_started = Device.dwTimeGlobal;
    m_search_listen_phase = m_search_listen_motions[0] ? 0u : 1u;
    m_search_listen_phase_played = false;
    m_search_listen_active = true;
    return true;
}

bool CStalkerAnimationManager::search_listen_active() const
{
    return m_search_listen_active;
}

void CStalkerAnimationManager::stop_search_listen()
{
    // No callbacks are registered and no Lua queue entries are touched. The
    // normal animation pass blends its own body tracks over this owned gesture.
    m_search_listen_active = false;
    m_search_listen_phase_played = false;
    m_search_listen.reset();
}

bool CStalkerAnimationManager::play_search_listen()
{
    if (!m_search_listen_active) return false;
    const u32 now = Device.dwTimeGlobal;
    CInventoryItem* item = object().inventory().ActiveItem();
    if (!search_listen_allowed() || now - m_search_listen_started >= 12000u ||
        search_listen_slot() != m_search_listen_slot ||
        (item ? item->object().ID() : u16(-1)) != m_search_listen_item ||
        object().memory().hit().recent_hit_revision() != m_search_listen_hit_revision ||
        object().conditions().health() < m_search_listen_health)
    {
        stop_search_listen();
        return false;
    }
    // Let the normal movement/pose update finish a requested crouch-to-stand
    // transition before starting a full-body standing animation.
    if (object().movement().body_state() != eBodyStateStand || !standing()) return false;

    if (m_search_listen_phase_played &&
        now - m_search_listen_phase_started >= m_search_listen_phase_duration)
    {
        ++m_search_listen_phase;
        m_search_listen_phase_played = false;
        m_search_listen.reset();
    }
    while (m_search_listen_phase < 3u && !m_search_listen_motions[m_search_listen_phase])
        ++m_search_listen_phase;
    if (m_search_listen_phase >= 3u)
    {
        stop_search_listen();
        return false;
    }

    global().reset();
    torso().reset();
    legs().reset();
    m_search_listen.animation(m_search_listen_motions[m_search_listen_phase]);
#ifdef USE_HEAD_BONE_PART_FAKE
    m_search_listen.play(m_skeleton_animated, 0, &object(), false, false, m_script_bone_part_mask);
    head().animation(assign_head_animation());
    head().play(m_skeleton_animated, head_play_callback, &object(), false);
#else
    head().reset();
    m_search_listen.play(m_skeleton_animated, 0, &object(), false, false);
#endif
    if (!m_search_listen_phase_played)
    {
        CBlend* blend = m_search_listen.blend();
        if (!blend || !_valid(blend->timeTotal) || !_valid(blend->speed) ||
            blend->timeTotal <= 0.f || blend->speed <= 0.f)
        {
            stop_search_listen();
            return false;
        }
        const float milliseconds = 1000.f * blend->timeTotal / blend->speed;
        // Play one clip/cycle, bounding malformed or unusually long custom clips.
        m_search_listen_phase_duration = u32(clampr(milliseconds, 200.f, 4000.f));
        m_search_listen_phase_started = now;
        m_search_listen_phase_played = true;
    }
    return true;
}
