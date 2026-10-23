////////////////////////////////////////////////////////////////////////////
//	Module 		: stalker_combat_actions.cpp
//	Created 	: 25.03.2004
//  Modified 	: 26.03.2004
//	Author		: Dmitriy Iassenev
//	Description : Stalker combat action classes
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "stalker_combat_actions.h"
#include "ai/stalker/ai_stalker.h"
#include "script_game_object.h"
#include "stalker_decision_space.h"
#include "inventory.h"
#include "cover_evaluators.h"
#include "cover_point.h"
#include "cover_manager.h"
#include "missile.h"
#include "stalker_movement_restriction.h"
#include "movement_manager_space.h"
#include "detail_path_manager_space.h"
#include "memory_manager.h"
#include "enemy_manager.h"
#include "visual_memory_manager.h"
#include "sight_manager.h"
#include "ai_object_location.h"
#include "stalker_movement_manager.h"
#include "stalker_movement_manager_space.h"
#include "sound_player.h"
#include "stalker_planner.h"
#include "agent_member_manager.h"
#include "agent_location_manager.h"
#include "danger_cover_location.h"
#include "ai/stalker/ai_stalker_space.h"
#include "weapon.h"
#include "danger_manager.h"
#include "detail_path_manager.h"
#include "weaponmagazined.h"
#include "stalker_animation_manager.h"
#include "level_path_manager.h"
#include "level_graph.h"
#include "ai_space.h"
#include "agent_manager.h"
#include "stalker_combat_cover_policy.h"
#include "hit_memory_manager.h"
#include "entitycondition.h"
#include "stalker_combat_diagnostics.h"

#define DISABLE_COVER_BEFORE_DETOUR

#if 0 // def DEBUG
#define TEST_MENTAL_STATE
#endif // DEBUG

const float TEMP_DANGER_DISTANCE = 5.f;
const u32 TEMP_DANGER_INTERVAL = 120000;

const float CLOSE_MOVE_DISTANCE = -10.f;

const u32 CROUCH_LOOK_OUT_DELTA = 5000;

using namespace StalkerSpace;
using namespace StalkerDecisionSpace;

typedef CStalkerActionBase::_edge_value_type _edge_value_type;

#ifdef _DEBUG
//#	define SILENT_COMBAT
#endif

namespace
{
// Short tactical moves only. Validate the straight AI-graph corridor and restrictions;
// the ordinary movement manager still owns the actual path and physical motion.
bool stalker_local_destination(CAI_Stalker& object, const Fvector& requested, Fvector& destination, u32& vertex)
{
    const Fvector start = object.Position();
    if (start.distance_to_sqr(requested) > 36.f || !ai().level_graph().valid_vertex_position(requested)) return false;
    vertex = ai().level_graph().check_position_in_direction(object.ai_location().level_vertex_id(), start, requested);
    if (!ai().level_graph().valid_vertex_id(vertex) || !object.movement().accessible(vertex)) return false;
    destination = requested;
    destination.y = ai().level_graph().vertex_plane_y(vertex, requested.x, requested.z);
    if (_abs(destination.y - start.y) > 1.5f || !object.movement().accessible(destination)) return false;
    Fvector delta;
    delta.sub(destination, start);
    const float length = delta.magnitude();
    if (length < .35f) return false;
    for (u32 i = 1; i != 4; ++i)
    {
        Fvector intermediate;
        intermediate.mad(start, delta, float(i) * .25f);
        if (!object.movement().accessible(intermediate)) return false;
    }
    delta.div(length);
    for (u32 i = 0; i != 2; ++i)
    {
        Fvector origin = start;
        origin.y += i ? 1.4f : .7f;
        collide::rq_result hit;
        if (Level().ObjectSpace.RayPick(origin, delta, length, collide::rqtBoth, hit, &object)) return false;
    }
    return true;
}

bool stalker_local_firing_line(CAI_Stalker& object, const Fvector& destination, const Fvector& target)
{
    CWeapon* weapon = smart_cast<CWeapon*>(object.inventory().ActiveItem());
    if (!weapon) return false;
    Fvector offset;
    offset.sub(destination, object.Position());
    for (u32 i = 0; i != 2; ++i)
    {
        Fvector origin = i ? weapon->get_CurrentFirePoint() : object.eye_matrix.c;
        origin.add(offset);
        Fvector direction;
        direction.sub(target, origin);
        const float distance = direction.magnitude();
        if (distance < EPS_L) return false;
        direction.div(distance);
        collide::rq_result hit;
        if (Level().ObjectSpace.RayPick(origin, direction, distance, collide::rqtBoth, hit, &object) &&
            hit.O != object.memory().enemy().selected()) return false;
    }
    return true;
}

bool stalker_local_space(CAI_Stalker& object, const Fvector& point, u32 vertex, bool enemy_info)
{
    const CCoverPoint temporary(point, vertex);
    return object.agent_manager().location().suitable(&object, &temporary, enemy_info) &&
        object.agent_manager().location().danger(&temporary, &object) >= .4f;
}

bool stalker_grenade_escape(CAI_Stalker& object, const Fvector& grenade, float radius, Fvector& destination, u32& vertex)
{
    Fvector away;
    away.sub(object.Position(), grenade);
    away.y = 0.f;
    if (away.square_magnitude() < EPS_L) away.set(0.f, 0.f, 1.f);
    away.normalize_safe();
    const Fvector side = Fvector().set(-away.z, 0.f, away.x);
    const float directions[5][2] = {{1.f,0.f},{.707107f,.707107f},{.707107f,-.707107f},{0.f,1.f},{0.f,-1.f}};
    const float old_distance = object.Position().distance_to_sqr(grenade);
    float best_distance = old_distance;
    bool found = false;
    // At most ten straight corridor probes; no speculative A* searches.
    for (u32 ring = 0; ring != 2; ++ring)
    for (u32 i = 0; i != 5; ++i)
    {
        Fvector offset, candidate, corrected;
        offset.mul(away, directions[i][0]);
        offset.mad(side, directions[i][1]);
        offset.mul(ring ? radius * .5f : radius);
        candidate.add(object.Position(), offset);
        u32 candidate_vertex;
        if (!stalker_local_destination(object, candidate, corrected, candidate_vertex)) continue;
        // Emergency escape keeps separation from members, but a danger-location score
        // must not reject every point inside the same grenade's danger radius.
        const CCoverPoint temporary(corrected, candidate_vertex);
        if (!object.agent_manager().location().suitable(&object, &temporary, false)) continue;
        const float distance = corrected.distance_to_sqr(grenade);
        if (!stalker_combat_cover_policy::escape_improves(old_distance, distance) || distance <= best_distance) continue;
        found = true;
        best_distance = distance;
        destination = corrected;
        vertex = candidate_vertex;
    }
    return found;
}
}

//////////////////////////////////////////////////////////////////////////
// CStalkerActionGetItemToKill
//////////////////////////////////////////////////////////////////////////

CStalkerActionGetItemToKill::CStalkerActionGetItemToKill(CAI_Stalker* object, LPCSTR action_name)
	: inherited(object, action_name)
{
}

void CStalkerActionGetItemToKill::initialize()
{
	inherited::initialize();

	object().sound().remove_active_sounds(u32(eStalkerSoundMaskNoHumming));

	object().sight().setup(
		CSightAction(object().m_best_found_item_to_kill ? &object().m_best_found_item_to_kill->object() : 0, true));

	object().movement().set_mental_state(eMentalStateDanger);
}

void CStalkerActionGetItemToKill::finalize()
{
	inherited::finalize();

	object().sight().clear();
	object().sight().setup(CSightAction(SightManager::eSightTypePathDirection, false));

	if (!object().g_Alive())
		return;

	object().sound().set_sound_mask(0);
}

void CStalkerActionGetItemToKill::execute()
{
#ifdef TEST_MENTAL_STATE
	VERIFY((start_level_time() == Device.dwTimeGlobal) || (object().movement().mental_state() == eMentalStateDanger));
#endif // TEST_MENTAL_STATE

	inherited::execute();

	if (!object().m_best_found_item_to_kill)
		return;

	object().movement().set_level_dest_vertex(
		object().m_best_found_item_to_kill->object().ai_location().level_vertex_id());
	object().movement().set_desired_position(&object().m_best_found_item_to_kill->object().Position());
	object().movement().set_desired_direction(0);
	object().movement().set_path_type(MovementManager::ePathTypeLevelPath);
	object().movement().set_detail_path_type(DetailPathManager::eDetailPathTypeSmooth);
	object().movement().set_body_state(object().movement().body_state() == eBodyStateCrouch ? eBodyStateCrouch
																							: eBodyStateStand);
	object().movement().set_movement_type(eMovementTypeWalk);
	object().set_goal(eObjectActionIdle);
}

//////////////////////////////////////////////////////////////////////////
// CStalkerActionMakeItemKilling
//////////////////////////////////////////////////////////////////////////

CStalkerActionMakeItemKilling::CStalkerActionMakeItemKilling(CAI_Stalker* object, LPCSTR action_name)
	: inherited(object, action_name)
{
}

void CStalkerActionMakeItemKilling::initialize()
{
	inherited::initialize();

	object().sound().remove_active_sounds(u32(eStalkerSoundMaskNoHumming));

	object().sight().clear();
	object().sight().add_action(
		eSightActionTypeWatchItem,
		xr_new<CSightControlAction>(1.f, 3000, CSightAction(SightManager::eSightTypePathDirection)));
	object().sight().add_action(
		eSightActionTypeWatchEnemy,
		xr_new<CSightControlAction>(
			1.f, 3000,
			CSightAction(SightManager::eSightTypePosition, object().memory().enemy().selected()->Position(), false)));

	object().movement().set_mental_state(eMentalStateDanger);
}

void CStalkerActionMakeItemKilling::finalize()
{
	inherited::finalize();

	object().sight().clear();
	object().sight().setup(CSightAction(SightManager::eSightTypePathDirection, false));

	if (!object().g_Alive())
		return;

	object().sound().set_sound_mask(0);
}

void CStalkerActionMakeItemKilling::execute()
{
#ifdef TEST_MENTAL_STATE
	VERIFY((start_level_time() == Device.dwTimeGlobal) || (object().movement().mental_state() == eMentalStateDanger));
#endif // TEST_MENTAL_STATE

	inherited::execute();

	if (!object().m_best_found_ammo)
		return;

	object().movement().set_level_dest_vertex(object().m_best_found_ammo->object().ai_location().level_vertex_id());
	object().movement().set_desired_position(&object().m_best_found_ammo->object().Position());
	object().movement().set_desired_direction(0);
	object().movement().set_path_type(MovementManager::ePathTypeLevelPath);
	object().movement().set_detail_path_type(DetailPathManager::eDetailPathTypeSmooth);
	object().movement().set_body_state(eBodyStateStand);
	object().movement().set_movement_type(eMovementTypeWalk);
    Fvector known;
    if (object().combat_target_position(known, true))
        object().sight().setup(CSightAction(SightManager::eSightTypePosition, known, true));
	object().set_goal(eObjectActionIdle);
}

//////////////////////////////////////////////////////////////////////////
// CStalkerActionRetreatFromEnemy
//////////////////////////////////////////////////////////////////////////

CStalkerActionRetreatFromEnemy::CStalkerActionRetreatFromEnemy(CAI_Stalker* object, LPCSTR action_name)
	: inherited(object, action_name)
{
}

void CStalkerActionRetreatFromEnemy::initialize()
{
	inherited::initialize();
}

void CStalkerActionRetreatFromEnemy::finalize()
{
	inherited::finalize();

	if (!object().g_Alive())
		return;

	object().sound().set_sound_mask(0);
}

void CStalkerActionRetreatFromEnemy::execute()
{
	inherited::execute();

	if (!object().memory().enemy().selected())
		return;

	if (!object().memory().enemy().selected())
		return;

	CMemoryInfo mem_object = object().memory().memory(object().memory().enemy().selected());

	object().movement().set_movement_type(eMovementTypeRun);
	object().movement().set_path_type(MovementManager::ePathTypeLevelPath);
	object().movement().set_detail_path_type(DetailPathManager::eDetailPathTypeSmooth);
	object().movement().set_mental_state(eMentalStatePanic);
	object().movement().set_body_state(eBodyStateStand);

	object().m_ce_far->setup(mem_object.m_object_params.m_position, 0.f, 300.f);
	const CCoverPoint* point = ai().cover_manager().best_cover(object().Position(), 30.f, *object().m_ce_far,
															   CStalkerMovementRestrictor(m_object, true));
	if (!point)
	{
		object().m_ce_far->setup(mem_object.m_object_params.m_position, 0.f, 300.f);
		point = ai().cover_manager().best_cover(object().Position(), 50.f, *object().m_ce_far,
												CStalkerMovementRestrictor(m_object, true));
	}

	if (point)
	{
		object().movement().set_level_dest_vertex(point->level_vertex_id());
		object().movement().set_desired_position(&point->position());
		object().CObjectHandler::set_goal(eObjectActionIdle);
		object().sight().setup(CSightAction(SightManager::eSightTypePathDirection, false));
	}
	else
	{
		if (object().memory().visual().visible_now(object().memory().enemy().selected()))
		{
			object().movement().set_mental_state(eMentalStateDanger);
			//			u32												min_queue_size, max_queue_size, min_queue_interval,
			//max_queue_interval; 			float											distance =
			//object().memory().enemy().selected()->Position().distance_to(object().Position()); 			select_queue_params
			//(distance,min_queue_size, max_queue_size, min_queue_interval, max_queue_interval);
			//			object().CObjectHandler::set_goal				(eObjectActionFire1,object().best_weapon(),min_queue_size,
			//max_queue_size, min_queue_interval, max_queue_interval);
			fire();
		}
		else
		{
			object().CObjectHandler::set_goal(eObjectActionIdle);
			object().sight().setup(CSightAction(SightManager::eSightTypeCover, true));
		}
	}

#ifndef SILENT_COMBAT
	play_panic_sound(0, 0, 10000);
#endif
}

_edge_value_type CStalkerActionRetreatFromEnemy::weight(const CSConditionState& condition0,
														const CSConditionState& condition1) const
{
	return (_edge_value_type(100));
}

//////////////////////////////////////////////////////////////////////////
// CStalkerActionGetReadyToKill
//////////////////////////////////////////////////////////////////////////

CStalkerActionGetReadyToKill::CStalkerActionGetReadyToKill(bool affect_properties, CAI_Stalker* object,
														   LPCSTR action_name)
	: inherited(object, action_name)
{
	m_affect_properties = affect_properties;
}

void CStalkerActionGetReadyToKill::initialize()
{
	inherited::initialize();

	m_body_state = object().movement().body_state();
	//	m_movement_type										= Random.randI(2) ? eMovementTypeRun : eMovementTypeWalk;
	//	m_movement_type										= eMovementTypeRun;

	object().movement().set_desired_direction(0);
	object().movement().set_path_type(MovementManager::ePathTypeLevelPath);
	object().movement().set_detail_path_type(DetailPathManager::eDetailPathTypeSmooth);
	object().movement().set_nearest_accessible_position();
	object().movement().set_mental_state(eMentalStateDanger);
	object().movement().set_body_state(m_body_state);
	//	object().movement().set_movement_type				(eMovementTypeRun);
	//	object().sight().setup								(CSightAction(SightManager::eSightTypePathDirection));
	if (m_affect_properties)
	{
		m_storage->set_property(eWorldPropertyInCover, false);
		m_storage->set_property(eWorldPropertyLookedOut, false);
		m_storage->set_property(eWorldPropertyPositionHolded, false);
		m_storage->set_property(eWorldPropertyEnemyDetoured, false);

		m_enable_enemy_change = object().memory().enemy().enable_enemy_change();
		object().memory().enemy().enable_enemy_change(false);
	}
}

void CStalkerActionGetReadyToKill::finalize()
{
	inherited::finalize();

	if (m_affect_properties)
		object().memory().enemy().enable_enemy_change(m_enable_enemy_change);
}

void CStalkerActionGetReadyToKill::execute()
{
#ifdef TEST_MENTAL_STATE
	VERIFY((start_level_time() == Device.dwTimeGlobal) || (object().movement().mental_state() == eMentalStateDanger));
#endif // TEST_MENTAL_STATE

	inherited::execute();

	if (!object().m_best_item_to_kill)
		return;

	if (!object().memory().enemy().selected())
		return;

	if (object().movement().detail().distance_to_target() < 2.f)
	{
		object().movement().set_movement_type(eMovementTypeWalk);
		object().sight().setup(CSightAction(SightManager::eSightTypeCurrentDirection));
	}
	else
	{
		object().movement().set_movement_type(eMovementTypeRun);
		object().sight().setup(CSightAction(SightManager::eSightTypePathDirection));
	}

	if (object().movement().detail().distance_to_target() > CLOSE_MOVE_DISTANCE)
		object().movement().set_body_state(eBodyStateStand);
	//	else {
	//		object().movement().set_movement_type	(m_movement_type);
	//	}

	CMemoryInfo mem_object = object().memory().memory(object().memory().enemy().selected());
	Fvector position = mem_object.m_object_params.m_position;

	const CCoverPoint* point = object().best_cover(position);
	if (point)
	{
		object().movement().set_level_dest_vertex(point->level_vertex_id());
		object().movement().set_desired_position(&point->position());
		//		object().movement().set_movement_type		(eMovementTypeRun);
		if (object().movement().path_completed() || object().Position().distance_to(point->position()) < 1.f)
		{
			//			object().movement().set_body_state		(eBodyStateCrouch);
			object().brain().affect_cover(true);
		}
		else
		{
			//			object().movement().set_body_state		(eBodyStateStand);
			object().brain().affect_cover(false);
		}
	}
	else
	{
		object().brain().affect_cover(true);
		object().movement().set_movement_type(eMovementTypeStand);
		//		object().movement().set_body_state			(eBodyStateCrouch);
		object().movement().set_nearest_accessible_position();
	}

	//	if (object().memory().visual().visible_now(object().memory().enemy().selected()))
	//		object().sight().setup	(CSightAction(object().memory().enemy().selected(),true));
	//	else
	//		object().sight().setup
	//(CSightAction(SightManager::eSightTypePosition,mem_object.m_object_params.m_position,true));

	//	if ((point && !point->position().similar(object().Position(),.5f)) || !object().movement().path_completed())
	//		object().sight().setup			(CSightAction(SightManager::eSightTypePathDirection));

	if (m_affect_properties)
		aim_ready();
	else
		aim_ready_force_full();

	if (object().movement().path_completed())
		object().best_cover_can_try_advance();
}

//////////////////////////////////////////////////////////////////////////
// CStalkerActionKillEnemy
//////////////////////////////////////////////////////////////////////////

CStalkerActionKillEnemy::CStalkerActionKillEnemy(CAI_Stalker* object, LPCSTR action_name)
	: inherited(object, action_name)
{
}

void CStalkerActionKillEnemy::initialize()
{
	inherited::initialize();
    m_fire_recovery_cover = 0;
    m_fire_recovery_started = Device.dwTimeGlobal;
    m_fire_recovery_settled = Device.dwTimeGlobal;
    m_fire_recovery_last_attempt = Device.dwTimeGlobal - 2500u;
    m_fire_recovery_retry = false;
	object().movement().set_desired_direction(0);
	object().movement().set_path_type(MovementManager::ePathTypeLevelPath);
	object().movement().set_detail_path_type(DetailPathManager::eDetailPathTypeSmooth);
	object().movement().set_nearest_accessible_position();
	object().movement().set_mental_state(eMentalStateDanger);
	//	object().movement().set_body_state			(m_storage->property(eWorldPropertyUseCrouchToLookOut) ?
	//eBodyStateCrouch : eBodyStateStand);
	object().movement().set_movement_type(eMovementTypeStand);
	m_storage->set_property(eWorldPropertyLookedOut, false);
	m_storage->set_property(eWorldPropertyPositionHolded, false);
	m_storage->set_property(eWorldPropertyEnemyDetoured, false);
#ifndef SILENT_COMBAT
	play_attack_sound(0, 0, 6000, 4000);
#endif
	object().brain().affect_cover(true);
}

void CStalkerActionKillEnemy::finalize()
{
	inherited::finalize();
}

void CStalkerActionKillEnemy::execute()
{
    inherited::execute();
    fire(); // Also establishes the known aim point when the shot is blocked.
    const CEntityAlive* enemy = object().memory().enemy().selected();
    const CMemoryInfo memory = enemy ? object().memory().memory(enemy) : CMemoryInfo();
    if (!memory.m_object)
    {
        if (m_fire_recovery_cover)
        {
            object().movement().set_nearest_accessible_position();
            object().movement().set_movement_type(eMovementTypeStand);
            m_fire_recovery_cover = 0;
        }
        return;
    }
    const u32 now = Device.dwTimeGlobal;
    const Fvector position = memory.m_object_params.m_position;
    if (m_fire_recovery_cover)
    {
        // The cover watchdog remains responsible for no-progress recovery. A changed
        // candidate starts its own move; it must not inherit the old fire-blocked timer.
        const CCoverPoint* point = object().best_cover(position);
        if (point && point != m_fire_recovery_cover)
        {
            m_fire_recovery_cover = point;
            m_fire_recovery_started = now;
            object().movement().set_level_dest_vertex(point->level_vertex_id());
            object().movement().set_desired_position(&point->position());
        }
        const bool reached = point && object().Position().distance_to_sqr(point->position()) < 1.f;
        const bool failed = point && now - m_fire_recovery_started >= 250u &&
            (object().movement().level_path().failed() || object().movement().path_completed());
        const bool expired = now - m_fire_recovery_started >= 10000u;
        if (!point || reached || failed || expired)
        {
            if (point && !reached)
                object().abandon_best_cover(failed ? "fire_recovery_path_failed" : "fire_recovery_timeout");
            m_fire_recovery_cover = 0;
            m_fire_recovery_settled = now;
            m_fire_recovery_last_attempt = now;
            m_fire_recovery_retry = !reached;
            object().movement().set_nearest_accessible_position();
            object().movement().set_movement_type(eMovementTypeStand);
            if (reached) m_storage->set_property(eWorldPropertyInCover, true);
            return;
        }
        const bool long_move = object().Position().distance_to_sqr(point->position()) > 9.f;
        object().movement().set_body_state(eBodyStateStand);
        object().movement().set_movement_type(long_move ? eMovementTypeRun : eMovementTypeWalk);
        return;
    }
    if (object().combat_fire_blocked_time() >= 1800u && now - m_fire_recovery_settled >= 1800u &&
        now - m_fire_recovery_last_attempt >= 2500u)
    {
        // These actions also serve the special kill branches, which do not require
        // InCover. Perform the move here instead of relying on a planner transition.
        if (!m_fire_recovery_retry)
            object().abandon_best_cover(object().combat_fire_block_reason());
        m_fire_recovery_retry = true;
        m_fire_recovery_last_attempt = now;
        m_fire_recovery_settled = now;
        m_storage->set_property(eWorldPropertyInCover, false);
        m_storage->set_property(eWorldPropertyLookedOut, false);
        m_storage->set_property(eWorldPropertyPositionHolded, false);
        m_storage->set_property(eWorldPropertyEnemyDetoured, false);
        const CCoverPoint* point = object().best_cover(position);
        if (point && object().Position().distance_to_sqr(point->position()) >= 1.f)
        {
            m_fire_recovery_cover = point;
            m_fire_recovery_started = now;
            object().movement().set_level_dest_vertex(point->level_vertex_id());
            object().movement().set_desired_position(&point->position());
            object().movement().set_body_state(eBodyStateStand);
            object().movement().set_movement_type(
                object().Position().distance_to_sqr(point->position()) > 9.f ? eMovementTypeRun : eMovementTypeWalk);
            if (stalker_combat_diagnostics::enabled(object().ID()))
                stalker_combat_diagnostics::log(object().ID(), stalker_combat_diagnostics::movement,
                    "fire_reposition", "cover=%u", point->level_vertex_id());
        }
        else
        {
            object().movement().set_nearest_accessible_position();
            object().movement().set_movement_type(eMovementTypeStand);
            if (point)
            {
                m_fire_recovery_retry = false;
                m_storage->set_property(eWorldPropertyInCover, true);
            }
        }
        return;
    }
    if (!m_fire_recovery_retry) object().best_cover(position);
}

//////////////////////////////////////////////////////////////////////////
// CStalkerActionTakeCover
//////////////////////////////////////////////////////////////////////////

CStalkerActionTakeCover::CStalkerActionTakeCover(CAI_Stalker* object, LPCSTR action_name)
	: inherited(object, action_name)
{
}

void CStalkerActionTakeCover::initialize()
{
	inherited::initialize();

	m_body_state = object().movement().body_state();
	//	m_movement_type								= Random.randI(2) ? eMovementTypeRun : eMovementTypeWalk;
	m_movement_type = eMovementTypeWalk;

	object().movement().set_desired_direction(0);
	object().movement().set_path_type(MovementManager::ePathTypeLevelPath);
	object().movement().set_detail_path_type(DetailPathManager::eDetailPathTypeSmooth);
	object().movement().set_mental_state(eMentalStateDanger);
	object().movement().set_body_state(m_body_state);
	object().movement().set_movement_type(m_movement_type);
	m_storage->set_property(eWorldPropertyLookedOut, false);
	m_storage->set_property(eWorldPropertyPositionHolded, false);
	m_storage->set_property(eWorldPropertyEnemyDetoured, false);

#ifndef SILENT_COMBAT
	if (object().memory().enemy().selected()->human_being())
	{
		if (object().agent_manager().member().can_cry_noninfo_phrase())
			if (object().Position().distance_to_sqr(object().memory().enemy().selected()->Position()) < _sqr(10.f))
				if (object().memory().visual().visible_now(object().memory().enemy().selected()) &&
					object().agent_manager().member().group_behaviour())
					object().sound().play(eStalkerSoundBackup, 0, 0, 6000, 4000);
	}
#endif
}

void CStalkerActionTakeCover::finalize()
{
	inherited::finalize();
}

void CStalkerActionTakeCover::execute()
{
#ifdef TEST_MENTAL_STATE
	VERIFY((start_level_time() == Device.dwTimeGlobal) || (object().movement().mental_state() == eMentalStateDanger));
#endif // TEST_MENTAL_STATE

	inherited::execute();

	CMemoryInfo mem_object = object().memory().memory(object().memory().enemy().selected());

	if (!mem_object.m_object)
		return;

	Fvector position = mem_object.m_object_params.m_position;
	const CCoverPoint* point = object().best_cover(position);
	if (point)
	{
        const bool long_move = object().Position().distance_to_sqr(point->position()) > 9.f;
        object().movement().set_movement_type(long_move ? eMovementTypeRun : eMovementTypeWalk);
        object().movement().set_body_state(long_move ? eBodyStateStand : m_body_state);
		object().movement().set_level_dest_vertex(point->level_vertex_id());
		object().movement().set_desired_position(&point->position());
		if (object().movement().path_completed() && object().Position().distance_to(point->position()) < 1.f)
			object().brain().affect_cover(true);
		else
			object().brain().affect_cover(false);
	}
	else
	{
		object().movement().set_nearest_accessible_position();
        object().movement().set_movement_type(eMovementTypeStand);
		object().brain().affect_cover(true);
	}

	//.	Add fire here
	//	if (object().memory().visual().visible_now(object().memory().enemy().selected()) && object().can_kill_enemy())
	//	if (object().memory().visual().visible_now(object().memory().enemy().selected()))
    Fvector aim_position;
    if (object().combat_target_position(aim_position, true))
        object().sight().setup(CSightAction(SightManager::eSightTypePosition, aim_position, true));
	if (fire_make_sense())
	{
		fire();
	}
	else
	{
		aim_ready();
	}

	if (object().movement().path_completed())
	{ // && (object().memory().enemy().selected()->Position().distance_to_sqr(object().Position()) >= 10.f))
		object().best_cover_can_try_advance();
		m_storage->set_property(eWorldPropertyInCover, true);
	}


}

//////////////////////////////////////////////////////////////////////////
// CStalkerActionLookOut
//////////////////////////////////////////////////////////////////////////

CStalkerActionLookOut::CStalkerActionLookOut(CAI_Stalker* object, LPCSTR action_name) : inherited(object, action_name)
{
	m_crouch_look_out_random.seed(u32(CPU::QPC() & 0xffffffff));
	m_last_change_time = 0;
}

void CStalkerActionLookOut::initialize()
{
	inherited::initialize();
    m_peek_anchor = object().Position();
    m_peek_last_query = Device.dwTimeGlobal - 500u;
    m_peek_started = Device.dwTimeGlobal;
    m_peek_moving = false;

	if (Device.dwTimeGlobal >= m_last_change_time + CROUCH_LOOK_OUT_DELTA)
	{
		m_storage->set_property(eWorldPropertyUseCrouchToLookOut, !!m_crouch_look_out_random.random(2));
		m_last_change_time = Device.dwTimeGlobal;
	}

	object().movement().set_desired_direction(0);
	object().movement().set_path_type(MovementManager::ePathTypeLevelPath);
	object().movement().set_detail_path_type(DetailPathManager::eDetailPathTypeSmooth);
	object().movement().set_mental_state(eMentalStateDanger);

	object().movement().set_body_state(m_storage->property(eWorldPropertyUseCrouchToLookOut) ? eBodyStateCrouch
																							 : eBodyStateStand);
	object().movement().set_movement_type(eMovementTypeWalk);
	object().movement().set_nearest_accessible_position();

	if (object().ready_to_detour())
		aim_ready();
	else
	{
		aim_ready_force_full();
		object().movement().set_movement_type(eMovementTypeStand);
	}

	set_inertia_time(1000);
	object().brain().affect_cover(true);
}

// Danger-in-direction actions still use this externally linked helper, including
// when no enemy is selected. Keep their eye-to-static-cover probe independent of
// the combat firing solution used by CStalkerActionLookOut.
float current_cover(CAI_Stalker* object)
{
	Fvector position, direction;
	position = object->eye_matrix.c;
	direction = object->eye_matrix.k;
	collide::rq_result ray_query_result;
	BOOL result = Level().ObjectSpace.RayPick(position, direction, 10.f, collide::rqtStatic, ray_query_result, NULL);

	if (!result)
		return (100.f);

	return (ray_query_result.range);
}

void CStalkerActionLookOut::finalize()
{
	inherited::finalize();
}

void CStalkerActionLookOut::execute()
{
    inherited::execute();
    Fvector target;
    if (!object().combat_target_position(target, true)) return;
    object().sight().setup(CSightAction(SightManager::eSightTypePosition, target, true));
    if (object().combat_fire_solution())
    {
        object().movement().set_nearest_accessible_position();
        object().movement().set_movement_type(eMovementTypeStand);
        m_storage->set_property(eWorldPropertyLookedOut, true);
        return;
    }
    // Failed local peeking gives the existing hold/detour plan a bounded turn.
    if (Device.dwTimeGlobal - m_peek_started >= 2500u)
    {
        object().movement().set_nearest_accessible_position();
        object().movement().set_movement_type(eMovementTypeStand);
        m_storage->set_property(eWorldPropertyLookedOut, true);
        return;
    }
    if (m_peek_moving && !object().movement().path_completed() && !object().movement().level_path().failed()) return;
    if (Device.dwTimeGlobal - m_peek_last_query < 500u) return;
    m_peek_last_query = Device.dwTimeGlobal;
    Fvector toward;
    toward.sub(target, m_peek_anchor);
    toward.y = 0.f;
    if (toward.square_magnitude() < EPS_L) return;
    toward.normalize_safe();
    const Fvector side = Fvector().set(-toward.z, 0.f, toward.x);
    const float offsets[4] = {1.2f, -1.2f, 2.f, -2.f};
    for (u32 i = 0; i != 4; ++i)
    {
        Fvector requested, destination;
        requested.mad(m_peek_anchor, side, offsets[i]);
        u32 vertex;
        if (!stalker_local_destination(object(), requested, destination, vertex) ||
            !stalker_local_space(object(), destination, vertex, true) ||
            !stalker_local_firing_line(object(), destination, target)) continue;
        object().movement().set_level_dest_vertex(vertex);
        object().movement().set_desired_position(&destination);
        object().movement().set_movement_type(eMovementTypeWalk);
        m_peek_moving = true;
        if (stalker_combat_diagnostics::enabled(object().ID()))
            stalker_combat_diagnostics::log(object().ID(), stalker_combat_diagnostics::action, "local_peek", "vertex=%u side=%u", vertex, i);
        return;
    }
    object().movement().set_movement_type(eMovementTypeStand);
}

//////////////////////////////////////////////////////////////////////////
// CStalkerActionHoldPosition
//////////////////////////////////////////////////////////////////////////

CStalkerActionHoldPosition::CStalkerActionHoldPosition(CAI_Stalker* object, LPCSTR action_name)
	: inherited(object, action_name)
{
}

void CStalkerActionHoldPosition::initialize()
{
	inherited::initialize();
	object().movement().set_desired_direction(0);
	object().movement().set_path_type(MovementManager::ePathTypeLevelPath);
	object().movement().set_detail_path_type(DetailPathManager::eDetailPathTypeSmooth);
	object().movement().set_nearest_accessible_position();
	object().movement().set_mental_state(eMentalStateDanger);
	object().movement().set_body_state(m_storage->property(eWorldPropertyUseCrouchToLookOut) ? eBodyStateCrouch
																							 : eBodyStateStand);
	object().movement().set_movement_type(eMovementTypeStand);

	aim_ready();

	set_inertia_time(1000 + ::Random32.random(2000));
	object().brain().affect_cover(true);
}

void CStalkerActionHoldPosition::finalize()
{
	inherited::finalize();
}

void CStalkerActionHoldPosition::execute()
{
#ifdef TEST_MENTAL_STATE
	VERIFY((start_level_time() == Device.dwTimeGlobal) || (object().movement().mental_state() == eMentalStateDanger));
#endif // TEST_MENTAL_STATE

	inherited::execute();

	CMemoryInfo mem_object = object().memory().memory(object().memory().enemy().selected());

	if (!mem_object.m_object)
		return;

    if (object().combat_fire_blocked_time() >= 750u)
        m_storage->set_property(eWorldPropertyLookedOut, false);

    Fvector aim_position;
    if (object().combat_target_position(aim_position, true))
        object().sight().setup(CSightAction(SightManager::eSightTypePosition, aim_position, true));

	if (completed())
	{
		if (object().agent_manager().member().can_detour() || !object().agent_manager().member().cover_detouring() ||
			!fire_make_sense())
		{
			m_storage->set_property(eWorldPropertyPositionHolded, true);
			m_storage->set_property(eWorldPropertyInCover, false);
		}
	}

	if (object().agent_manager().member().cover_detouring() && fire_make_sense())
	{
		//		object().sound().play		(eStalkerSoundDetour,3000,3000,10000,10000);
		object().sound().play(eStalkerSoundNeedBackup, 3000, 3000, 10000, 10000);
		fire();
	}
	else
	{
		aim_ready();
	}

	if (object().memory().enemy().selected())
	{
		CMemoryInfo mem_object = object().memory().memory(object().memory().enemy().selected());

		if (mem_object.m_object)
		{
			object().best_cover(mem_object.m_object_params.m_position);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// CStalkerActionDetourEnemy
//////////////////////////////////////////////////////////////////////////

CStalkerActionDetourEnemy::CStalkerActionDetourEnemy(CAI_Stalker* object, LPCSTR action_name)
	: inherited(object, action_name)
{
}

void CStalkerActionDetourEnemy::initialize()
{
	inherited::initialize();
	object().agent_manager().member().member(&object()).detour(true);
	object().movement().set_desired_direction(0);
	object().movement().set_path_type(MovementManager::ePathTypeLevelPath);
	object().movement().set_detail_path_type(DetailPathManager::eDetailPathTypeSmooth);
	object().movement().set_mental_state(eMentalStateDanger);
	object().movement().set_body_state(eBodyStateStand);
	object().movement().set_movement_type(eMovementTypeRun);

	aim_ready();

#ifdef DISABLE_COVER_BEFORE_DETOUR
	if (/**(Random.randF(1.f) < .8f) && /**/ object().agent_manager().member().member(m_object).cover())
		object().agent_manager().location().add(xr_new<CDangerCoverLocation>(
			object().agent_manager().member().member(m_object).cover(), Device.dwTimeGlobal, TEMP_DANGER_INTERVAL,
			TEMP_DANGER_DISTANCE, object().agent_manager().member().mask(&object())));
#endif

	object().agent_manager().member().member(m_object).cover(0);

	//#ifndef SILENT_COMBAT
	if (object().memory().enemy().selected()->human_being() && object().agent_manager().member().group_behaviour())
		//		object().sound().play			(eStalkerSoundNeedBackup);
		object().sound().play(eStalkerSoundDetour);
	//#endif
}

void CStalkerActionDetourEnemy::finalize()
{
	inherited::finalize();

	if (object().g_Alive())
		object().agent_manager().member().member(&object()).detour(false);
}

void CStalkerActionDetourEnemy::execute()
{
#ifdef TEST_MENTAL_STATE
	VERIFY((start_level_time() == Device.dwTimeGlobal) || (object().movement().mental_state() == eMentalStateDanger));
#endif // TEST_MENTAL_STATE

	inherited::execute();

	CMemoryInfo mem_object = object().memory().memory(object().memory().enemy().selected());

	if (!mem_object.m_object)
		return;

	if (object().movement().path_completed())
	{
		Fvector position = mem_object.m_object_params.m_position;

		object().m_ce_angle->setup(position, 10.f, object().ffGetRange(), mem_object.m_object_params.m_level_vertex_id);
		const CCoverPoint* point = ai().cover_manager().best_cover(object().Position(), 10.f, *object().m_ce_angle,
																   CStalkerMovementRestrictor(m_object, true));
		if (!point)
		{
			object().m_ce_angle->setup(position, 10.f, object().ffGetRange(),
									   mem_object.m_object_params.m_level_vertex_id);
			point = ai().cover_manager().best_cover(object().Position(), 30.f, *object().m_ce_angle,
													CStalkerMovementRestrictor(m_object, true));
		}

		if (point)
		{
			object().movement().set_level_dest_vertex(point->level_vertex_id());
			object().movement().set_desired_position(&point->position());
		}
		else
			object().movement().set_nearest_accessible_position();

		if (object().movement().path_completed())
			m_storage->set_property(eWorldPropertyEnemyDetoured, true);
	}

	object().sight().setup(CSightAction(SightManager::eSightTypePosition, mem_object.m_object_params.m_position, true));
}

//////////////////////////////////////////////////////////////////////////
// CStalkerActionSearchEnemy
//////////////////////////////////////////////////////////////////////////

CStalkerActionSearchEnemy::CStalkerActionSearchEnemy(CAI_Stalker* object, LPCSTR action_name)
	: inherited(object, action_name)
{
}

void CStalkerActionSearchEnemy::initialize()
{
	inherited::initialize();
    m_sneak_decided = m_sneak_active = m_sneak_arrived = false;
    m_search_has_anchor = false;
    m_search_moving = false;
    m_search_started = Device.dwTimeGlobal;
    m_search_stage_time = Device.dwTimeGlobal;
    m_search_stage = 0;
    m_search_memory_time = 0;
    m_search_enemy_id = u16(-1);
    object().movement().set_desired_position(0);
	object().movement().set_desired_direction(0);
	object().movement().set_path_type(MovementManager::ePathTypeLevelPath);
	object().movement().set_detail_path_type(DetailPathManager::eDetailPathTypeSmooth);
	object().movement().set_mental_state(eMentalStateDanger);
	object().movement().set_body_state(eBodyStateStand);
	object().movement().set_movement_type(eMovementTypeWalk);

	aim_ready();

	object().agent_manager().member().member(m_object).cover(0);
}

void CStalkerActionSearchEnemy::finalize()
{
    stop_sneak("sneak_finalize");
	inherited::finalize();
}

void CStalkerActionSearchEnemy::stop_sneak(const char* reason)
{
    if (!object().g_Alive()) { m_sneak_active = false; return; }
    object().agent_manager().member().release_scout(object().ID());
    if (!m_sneak_active) return;
    m_sneak_active = false;
    object().movement().set_body_state(eBodyStateStand);
    object().movement().set_nearest_accessible_position();
    object().movement().set_movement_type(eMovementTypeStand);
    m_search_anchor = object().Position();
    m_search_stage = 1;
    m_search_stage_time = Device.dwTimeGlobal;
    m_search_moving = false;
    if (stalker_combat_diagnostics::enabled(object().ID()))
        stalker_combat_diagnostics::log(object().ID(), stalker_combat_diagnostics::action,
            reason, "enemy=%u elapsed_ms=%u", u32(m_search_enemy_id), Device.dwTimeGlobal - m_sneak_started);
}

bool CStalkerActionSearchEnemy::try_sneak(u32 now, bool squad_visible)
{
    if (m_sneak_decided) return false;
    m_sneak_decided = true;
    CAgentMemberManager& members = object().agent_manager().member();
    const bool grouped = members.group_behaviour();
    const CDangerObject* danger = object().memory().danger().selected();
    const bool unsafe = object().wounded() || object().critically_wounded() ||
        (danger && danger->type() == CDangerObject::eDangerTypeGrenade);
    if (!stalker_combat_awareness::sneak_candidate(now - m_search_memory_time,
        object().Position().distance_to_sqr(m_search_anchor), squad_visible, unsafe,
        object().ID(), m_search_memory_time, grouped)) return false;
    Fvector away;
    away.sub(object().Position(), m_search_anchor);
    away.y = 0.f;
    away.normalize_safe();
    Fvector requested;
    requested.mad(m_search_anchor, away, 4.f);
    u32 vertex;
    if (!stalker_local_destination(object(), requested, m_sneak_destination, vertex) ||
        !stalker_local_space(object(), m_sneak_destination, vertex, true)) return false;
    if (grouped && !members.claim_scout(&object(), m_search_enemy_id, now)) return false;
    m_sneak_active = true;
    m_sneak_arrived = false;
    m_sneak_started = now;
    m_sneak_hit_revision = object().memory().hit().recent_hit_revision();
    m_sneak_health = object().conditions().health();
    object().movement().set_level_dest_vertex(vertex);
    object().movement().set_desired_position(&m_sneak_destination);
    object().movement().set_body_state(eBodyStateCrouch);
    object().movement().set_movement_type(eMovementTypeWalk);
    if (stalker_combat_diagnostics::enabled(object().ID()))
        stalker_combat_diagnostics::log(object().ID(), stalker_combat_diagnostics::action,
            "sneak_start", "enemy=%u memory_ms=%u grouped=%u", u32(m_search_enemy_id), now - m_search_memory_time, u32(grouped));
    return true;
}

bool CStalkerActionSearchEnemy::update_sneak(u32 now)
{
    if (!m_sneak_active) return false;
    const CDangerObject* danger = object().memory().danger().selected();
    const bool unsafe = object().wounded() || object().critically_wounded() ||
        object().conditions().health() < m_sneak_health ||
        object().memory().hit().recent_hit_revision() != m_sneak_hit_revision ||
        (danger && danger->type() == CDangerObject::eDangerTypeGrenade);
    const bool lease_lost = object().agent_manager().member().group_behaviour() &&
        !object().agent_manager().member().claim_scout(&object(), m_search_enemy_id, now);
    if (unsafe || lease_lost || now - m_sneak_started >= 6000u ||
        (now - m_sneak_started >= 250u && object().movement().level_path().failed()))
    {
        stop_sneak(unsafe ? "sneak_threat" : "sneak_timeout_or_path");
        return true; // Do not overwrite the cancellation with a new move in this tick.
    }
    Fvector look = m_search_observed_position;
    look.y += 1.f;
    object().sight().setup(CSightAction(SightManager::eSightTypePosition, look, true));
    if (!m_sneak_arrived && (object().Position().distance_to_sqr(m_sneak_destination) <= .64f ||
        (now - m_sneak_started >= 250u && object().movement().path_completed())))
    {
        if (object().Position().distance_to_sqr(m_sneak_destination) > 2.25f)
        {
            stop_sneak("sneak_unreachable");
            return true;
        }
        m_sneak_arrived = true;
        m_sneak_arrival_time = now;
        object().movement().set_nearest_accessible_position();
        object().movement().set_movement_type(eMovementTypeStand);
    }
    if (m_sneak_arrived && now - m_sneak_arrival_time >= 1000u) stop_sneak("sneak_inspected");
    return true;
}

void CStalkerActionSearchEnemy::execute()
{
    inherited::execute();
    const CEntityAlive* enemy = object().memory().enemy().selected();
    if (!enemy) { stop_sneak("sneak_no_enemy"); return; }
    if (object().memory().visual().visible_now(enemy))
    {
        stop_sneak("sneak_visible");
        object().movement().set_movement_type(eMovementTypeStand);
        return; // The planner returns to visible-target combat without disabling memory.
    }
    const CMemoryInfo memory = object().memory().memory(enemy);
    if (!memory.m_object) { stop_sneak("sneak_no_memory"); return; }
    const CVisibleObject* visual = object().memory().visual().visible_object(enemy);
    const bool squad_visible = visual && visual->visible(squad_mask_type(-1));
    const u32 now = Device.dwTimeGlobal;
    const bool new_enemy = !m_search_has_anchor || m_search_enemy_id != enemy->ID();
    const bool fresh_report = new_enemy ||
        stalker_combat_cover_policy::newer_observation(memory.m_level_time, m_search_memory_time);
    const bool changed_position = new_enemy || (fresh_report &&
        memory.m_object_params.m_position.distance_to_sqr(m_search_observed_position) >= 1.f);
    if (fresh_report)
    {
        // A new direct observation refreshes patience even at the same position.
        // Its original timestamp survives relaying, so relays cannot prolong the budget.
        m_search_memory_time = memory.m_level_time;
        m_search_started = now;
    }
    if (changed_position)
    {
        stop_sneak("sneak_report_changed");
        if (new_enemy) m_sneak_decided = false;
        m_search_anchor = memory.m_object_params.m_position;
        m_search_observed_position = m_search_anchor;
        m_search_enemy_id = enemy->ID();
        m_search_vertex = memory.m_object_params.m_level_vertex_id;
        m_search_direction.sub(m_search_anchor, memory.m_self_params.m_position);
        m_search_direction.y = 0.f;
        if (m_search_direction.square_magnitude() < EPS_L) m_search_direction.set(0.f, 0.f, 1.f);
        m_search_direction.normalize_safe();
        m_search_stage_time = now;
        m_search_stage = 0;
        m_search_has_anchor = true;
        m_search_moving = true;
        if (try_sneak(now, squad_visible))
        {
            update_sneak(now);
            return;
        }
        else if (object().agent_manager().member().other_scout(object().ID(), enemy->ID(), now))
        {
            // Search around our own position instead of following the assigned scout.
            m_search_anchor = object().Position();
            m_search_stage = 1;
            m_search_moving = false;
            object().movement().set_nearest_accessible_position();
        }
        else if (object().movement().accessible(m_search_vertex) && object().movement().accessible(m_search_anchor))
        {
            object().movement().set_level_dest_vertex(m_search_vertex);
            object().movement().set_desired_position(&m_search_anchor);
        }
        else object().movement().set_nearest_accessible_position(m_search_anchor, m_search_vertex);
        object().movement().set_movement_type(eMovementTypeWalk);
    }
    if (update_sneak(now)) return;
    const u32 report_age = now - m_search_started;
    if (stalker_combat_cover_policy::finish_search(report_age, m_search_stage > 3, squad_visible))
    {
        object().memory().enable(enemy, false);
        object().movement().set_nearest_accessible_position();
        object().movement().set_movement_type(eMovementTypeStand);
        return;
    }
    if (m_search_stage == 0)
    {
        object().sight().setup(CSightAction(SightManager::eSightTypePosition, m_search_anchor, true));
        if (!object().movement().path_completed() && !object().movement().level_path().failed() &&
            now - m_search_stage_time < 6000u) return;
        // If the exact last-known node cannot be reached, search only around our reachable position.
        if (object().Position().distance_to_sqr(m_search_anchor) > 16.f) m_search_anchor = object().Position();
        m_search_stage = 1;
        m_search_stage_time = now;
        m_search_moving = false;
    }
    if (m_search_stage > 3)
    {
        // Another member's live visual contact must never be disabled by our search.
        // Same-position reports update patience without rebuilding this finished route.
        object().movement().set_nearest_accessible_position();
        object().movement().set_movement_type(eMovementTypeStand);
        Fvector aim;
        if (object().combat_target_position(aim, true))
            object().sight().setup(CSightAction(SightManager::eSightTypePosition, aim, true));
        return;
    }
    const Fvector side = Fvector().set(-m_search_direction.z, 0.f, m_search_direction.x);
    Fvector sector;
    if (m_search_stage == 1) sector = side;
    else if (m_search_stage == 2) sector = Fvector(side).invert();
    else sector = m_search_direction;
    Fvector look;
    look.mad(m_search_anchor, sector, 6.f);
    look.y += 1.f;
    object().sight().setup(CSightAction(SightManager::eSightTypePosition, look, true));
    if (!m_search_moving)
    {
        Fvector requested, destination;
        requested.mad(m_search_anchor, sector, 2.5f);
        u32 vertex;
        if (stalker_local_destination(object(), requested, destination, vertex) &&
            stalker_local_space(object(), destination, vertex, false))
        {
            object().movement().set_level_dest_vertex(vertex);
            object().movement().set_desired_position(&destination);
            object().movement().set_movement_type(eMovementTypeWalk);
        }
        else
        {
            object().movement().set_nearest_accessible_position();
            object().movement().set_movement_type(eMovementTypeStand);
        }
        m_search_moving = true;
        if (stalker_combat_diagnostics::enabled(object().ID()))
            stalker_combat_diagnostics::log(object().ID(), stalker_combat_diagnostics::action,
                "search_sector", "stage=%u memory_ms=%u", m_search_stage, now - m_search_memory_time);
    }
    if (stalker_combat_cover_policy::search_step_expired(now - m_search_stage_time))
    {
        ++m_search_stage;
        m_search_stage_time = now;
        m_search_moving = false;
    }
}

//////////////////////////////////////////////////////////////////////////
// CStalkerActionPostCombatWait
//////////////////////////////////////////////////////////////////////////

CStalkerActionPostCombatWait::CStalkerActionPostCombatWait(CAI_Stalker* object, LPCSTR action_name)
	: inherited(object, action_name)
{
}

void CStalkerActionPostCombatWait::initialize()
{
	inherited::initialize();

	object().movement().set_movement_type(eMovementTypeStand);

	EObjectAction action = eObjectActionAimReady1;
	if (m_storage->property(eWorldPropertyKilledWounded))
		action = eObjectActionIdle;

	if (object().inventory().ActiveItem() && object().best_weapon() &&
		(object().inventory().ActiveItem()->object().ID() == object().best_weapon()->object().ID()))
		object().set_goal(action, object().best_weapon());
	else
	{
		if (object().inventory().m_slots[1].m_pIItem)
		{
			CWeaponMagazined* temp = smart_cast<CWeaponMagazined*>(object().inventory().m_slots[1].m_pIItem);
			if (object().inventory().ActiveItem() && temp &&
				(object().inventory().ActiveItem()->object().ID() == temp->ID()))
				object().set_goal(action, object().inventory().m_slots[1].m_pIItem);
		}
	}

	if (m_storage->property(eWorldPropertyKilledWounded))
		return;

	if (object().memory().enemy().last_enemy() &&
		object().memory().visual().visible_now(object().memory().enemy().last_enemy()))
		object().sight().setup(CSightAction(object().memory().enemy().last_enemy(), true, true));
}

void CStalkerActionPostCombatWait::execute()
{
	inherited::execute();
}

void CStalkerActionPostCombatWait::finalize()
{
	inherited::finalize();
}

//////////////////////////////////////////////////////////////////////////
// CStalkerActionGetDistance
//////////////////////////////////////////////////////////////////////////

CStalkerActionGetDistance::CStalkerActionGetDistance(CAI_Stalker* object, LPCSTR action_name)
	: inherited(object, action_name)
{
}

void CStalkerActionGetDistance::initialize()
{
	inherited::initialize();
	object().movement().set_desired_direction(0);
	object().movement().set_path_type(MovementManager::ePathTypeLevelPath);
	object().movement().set_detail_path_type(DetailPathManager::eDetailPathTypeSmooth);
	object().movement().set_mental_state(eMentalStateDanger);
	object().movement().set_body_state(eBodyStateStand);
	object().movement().set_movement_type(eMovementTypeRun);
}

void CStalkerActionGetDistance::execute()
{
#ifdef TEST_MENTAL_STATE
	VERIFY((start_level_time() == Device.dwTimeGlobal) || (object().movement().mental_state() == eMentalStateDanger));
#endif // TEST_MENTAL_STATE

	inherited::execute();

	if (!object().memory().enemy().selected())
		return;

	CMemoryInfo mem_object = object().memory().memory(object().memory().enemy().selected());

	if (!mem_object.m_object)
		return;

	Fvector position = mem_object.m_object_params.m_position;

	if (object().inventory().ActiveItem() && object().best_weapon() &&
		(object().inventory().ActiveItem()->object().ID() == object().best_weapon()->object().ID()))
		aim_ready();

	object().sight().setup(CSightAction(SightManager::eSightTypePosition, position, true));

	if (!object().movement().path_completed() && (Device.dwTimeGlobal >= (mem_object.m_level_time + 1000)))
		return;

	object().m_ce_best_by_time->setup(position, 10.f, object().ffGetRange(), 10.f);
	const CCoverPoint* point = ai().cover_manager().best_cover(object().Position(), 10.f, *object().m_ce_best_by_time,
															   CStalkerMovementRestrictor(m_object, true));
	if (!point)
	{
		object().m_ce_best_by_time->setup(position, 10.f, object().ffGetRange(), 10.f);
		point = ai().cover_manager().best_cover(object().Position(), 30.f, *object().m_ce_best_by_time,
												CStalkerMovementRestrictor(m_object, true));
	}

	if (point)
	{
		object().movement().set_level_dest_vertex(point->level_vertex_id());
		object().movement().set_desired_position(&point->position());
	}
	else
		object().movement().set_nearest_accessible_position();
}

void CStalkerActionGetDistance::finalize()
{
	inherited::finalize();
}

//////////////////////////////////////////////////////////////////////////
// CStalkerActionHideFromGrenade
//////////////////////////////////////////////////////////////////////////

CStalkerActionHideFromGrenade::CStalkerActionHideFromGrenade(CAI_Stalker* object, LPCSTR action_name)
	: inherited(object, action_name)
{
}

void CStalkerActionHideFromGrenade::initialize()
{
	inherited::initialize();
    object().invalidate_best_cover();
    m_grenade_last_query = Device.dwTimeGlobal - 400u;
    m_grenade_object_id = u16(-1);
    m_grenade_last_position = object().Position();
    m_grenade_escape_active = false;

	m_storage->set_property(eWorldPropertyUseSuddenness, false);

	object().movement().set_desired_direction(0);
	object().movement().set_path_type(MovementManager::ePathTypeLevelPath);
	object().movement().set_detail_path_type(DetailPathManager::eDetailPathTypeSmooth);
	object().movement().set_mental_state(eMentalStateDanger);
	object().movement().set_body_state(eBodyStateStand);
	object().movement().set_movement_type(eMovementTypeRun);
	object().m_ce_best->invalidate();

	m_storage->set_property(eWorldPropertyInCover, false);
	m_storage->set_property(eWorldPropertyLookedOut, false);
	m_storage->set_property(eWorldPropertyPositionHolded, false);
	m_storage->set_property(eWorldPropertyEnemyDetoured, false);
}

void CStalkerActionHideFromGrenade::execute()
{
    inherited::execute();
    const CDangerObject* danger = object().memory().danger().selected();
    if (!danger || danger->type() != CDangerObject::eDangerTypeGrenade) return;
    const u32 now = Device.dwTimeGlobal;
    const Fvector position = danger->position();
    const CObject* explosive = danger->dependent_object();
    const u16 explosive_id = explosive ? explosive->ID() : u16(-1);
    const bool moved = explosive_id != m_grenade_object_id ||
        position.distance_to_sqr(m_grenade_last_position) > 1.f;
    if (moved)
    {
        object().invalidate_best_cover();
        m_grenade_last_position = position;
        m_grenade_object_id = explosive_id;
        m_grenade_escape_active = false;
    }
    const CMissile* missile = smart_cast<const CMissile*>(explosive);
    const bool known_fuse = missile && missile->destroy_time() != u32(-1);
    const u32 fuse_ms = known_fuse ? stalker_combat_cover_policy::fuse_remaining(now, missile->destroy_time()) : u32(-1);
    // Read the already configured danger/run velocity; do not mutate movement state to query speed.
    const float run_speed = object().movement().detail().velocity(
        StalkerMovement::eVelocityRunDangerStandPositive).linear_velocity;
    const float travel_budget = known_fuse ? stalker_combat_cover_policy::grenade_travel_budget(fuse_ms, run_speed) : 30.f;
    const float escape_radius = stalker_combat_cover_policy::grenade_escape_radius(travel_budget);
    const bool keep_escape = m_grenade_escape_active && !moved &&
        !object().movement().path_completed() && !object().movement().level_path().failed() &&
        now - m_grenade_last_query < 1500u;
    const CCoverPoint* point = keep_escape ? 0 : object().best_cover(position);
    if (point)
    {
        const float cover_distance = object().Position().distance_to(point->position());
        // Nearby shelter is useful immediately; a far route cannot be relied on before detonation.
        if (known_fuse && cover_distance > 1.f && cover_distance > travel_budget) point = 0;
    }
    if (point)
    {
        object().movement().set_level_dest_vertex(point->level_vertex_id());
        object().movement().set_desired_position(&point->position());
        object().movement().set_movement_type(eMovementTypeRun);
        object().movement().set_body_state(eBodyStateStand);
        m_grenade_escape_active = false;
    }
    else if (!keep_escape && (moved || now - m_grenade_last_query >= 400u))
    {
        m_grenade_last_query = now;
        m_grenade_last_position = position;
        object().agent_manager().member().member(m_object).cover(0);
        Fvector escape;
        u32 vertex;
        if (stalker_grenade_escape(object(), position, escape_radius, escape, vertex))
        {
            object().movement().set_level_dest_vertex(vertex);
            object().movement().set_desired_position(&escape);
            object().movement().set_movement_type(eMovementTypeRun);
            object().movement().set_body_state(eBodyStateStand);
            m_grenade_escape_active = true;
            if (stalker_combat_diagnostics::enabled(object().ID()))
                stalker_combat_diagnostics::log(object().ID(), stalker_combat_diagnostics::grenade,
                    "escape_without_cover", "vertex=%u fuse_ms=%u step=%.2f", vertex, fuse_ms, escape_radius);
        }
        else
        {
            object().movement().set_nearest_accessible_position();
            object().movement().set_movement_type(eMovementTypeStand);
            object().movement().set_body_state(eBodyStateCrouch);
            m_grenade_escape_active = false;
        }
    }
    object().sight().setup(CSightAction(SightManager::eSightTypePathDirection, true, true));
    if (object().memory().enemy().selected()) aim_ready();
    else object().CObjectHandler::set_goal(eObjectActionIdle, object().best_weapon());
    if (point && object().movement().path_completed())
    {
        object().movement().set_movement_type(eMovementTypeStand);
        object().movement().set_body_state(eBodyStateCrouch);
    }
}

void CStalkerActionHideFromGrenade::finalize()
{
	inherited::finalize();
}

//////////////////////////////////////////////////////////////////////////
// CStalkerActionSuddenAttack
//////////////////////////////////////////////////////////////////////////

CStalkerActionSuddenAttack::CStalkerActionSuddenAttack(CAI_Stalker* object, LPCSTR action_name)
	: inherited(object, action_name)
{
}

void CStalkerActionSuddenAttack::initialize()
{
	inherited::initialize();
    m_sudden_started = Device.dwTimeGlobal;
    object().movement().set_desired_position(0);

	object().movement().set_desired_direction(0);
	object().movement().set_path_type(MovementManager::ePathTypeLevelPath);
	object().movement().set_detail_path_type(DetailPathManager::eDetailPathTypeSmooth);
	object().movement().set_mental_state(eMentalStateDanger);

	if (!object().memory().enemy().selected())
		return;

	aim_ready();
}

void CStalkerActionSuddenAttack::finalize()
{
	inherited::finalize();
}

void CStalkerActionSuddenAttack::execute()
{
    inherited::execute();
    const CEntityAlive* enemy = object().memory().enemy().selected();
    Fvector position, aim;
    if (!enemy || !object().combat_target_position(position) || !object().combat_target_position(aim, true)) return;
    if (object().agent_manager().member().combat_members().size() > 1 ||
        !object().memory().visual().visible_now(enemy) || Device.dwTimeGlobal - m_sudden_started >= 5000u)
    {
        m_storage->set_property(eWorldPropertyUseSuddenness, false);
        return;
    }
    object().sight().setup(CSightAction(SightManager::eSightTypePosition, aim, true));
    const int weapon_type = object().best_weapon() ? object().best_weapon()->object().ef_weapon_type() : 6;
    const float distance = object().Position().distance_to(position);
    const float attack_distance = stalker_combat_cover_policy::ambush_distance(weapon_type);
    if (distance <= attack_distance)
    {
        object().movement().set_nearest_accessible_position();
        object().movement().set_movement_type(eMovementTypeStand);
        object().movement().set_body_state(eBodyStateStand);
        fire();
        if (object().combat_fire_solution() || object().combat_fire_blocked_time() >= 1500u)
            m_storage->set_property(eWorldPropertyUseSuddenness, false);
        return;
    }
    // Approach a valid firing position; if no cover exists, close only until weapon range.
    const CCoverPoint* cover = object().best_cover(position);
    if (cover)
    {
        object().movement().set_level_dest_vertex(cover->level_vertex_id());
        object().movement().set_desired_position(&cover->position());
    }
    else
    {
        const CMemoryInfo memory = object().memory().memory(enemy);
        object().movement().set_nearest_accessible_position(position, memory.m_object_params.m_level_vertex_id);
    }
    object().movement().set_body_state(eBodyStateStand);
    object().movement().set_movement_type(distance > attack_distance + 10.f ? eMovementTypeRun : eMovementTypeWalk);
    aim_ready();
}

//////////////////////////////////////////////////////////////////////////
// CStalkerActionKillEnemyIfPlayerOnThePath
//////////////////////////////////////////////////////////////////////////

CStalkerActionKillEnemyIfPlayerOnThePath::CStalkerActionKillEnemyIfPlayerOnThePath(CAI_Stalker* object,
																				   LPCSTR action_name)
	: inherited(object, action_name)
{
}

void CStalkerActionKillEnemyIfPlayerOnThePath::initialize()
{
	inherited::initialize();

	object().movement().set_mental_state(eMentalStateDanger);
	object().movement().set_movement_type(eMovementTypeStand);
	object().movement().force_update(true);

	m_storage->set_property(eWorldPropertyInCover, false);
	m_storage->set_property(eWorldPropertyLookedOut, false);
	m_storage->set_property(eWorldPropertyPositionHolded, false);
	m_storage->set_property(eWorldPropertyEnemyDetoured, false);

	play_attack_sound(0, 0, 6000, 4000);

	object().brain().affect_cover(true);
}

void CStalkerActionKillEnemyIfPlayerOnThePath::finalize()
{
	inherited::finalize();

	object().movement().force_update(false);
}

void CStalkerActionKillEnemyIfPlayerOnThePath::execute()
{
#ifdef TEST_MENTAL_STATE
	VERIFY((start_level_time() == Device.dwTimeGlobal) || (object().movement().mental_state() == eMentalStateDanger));
#endif // TEST_MENTAL_STATE

	inherited::execute();

	fire();

	if (!object().memory().enemy().selected()) return;
	CMemoryInfo mem_object = object().memory().memory(object().memory().enemy().selected());
	if (!mem_object.m_object) return;
	Fvector position = mem_object.m_object_params.m_position;
	const CCoverPoint* point = object().best_cover(position);
	if (point)
	{
		object().movement().set_level_dest_vertex(point->level_vertex_id());
		object().movement().set_desired_position(&point->position());
	}
}

//////////////////////////////////////////////////////////////////////////
// CStalkerActionCriticalHit
//////////////////////////////////////////////////////////////////////////

CStalkerActionCriticalHit::CStalkerActionCriticalHit(CAI_Stalker* object, LPCSTR action_name)
	: inherited(object, action_name)
{
}

void CStalkerActionCriticalHit::initialize()
{
	inherited::initialize();

	object().brain().affect_cover(false);
	object().movement().set_movement_type(eMovementTypeStand);

	if (object().memory().enemy().selected())
	{
		u32 min_queue_size, max_queue_size, min_queue_interval, max_queue_interval;
		float distance = object().memory().enemy().selected()->Position().distance_to(object().Position());
		select_queue_params(distance, min_queue_size, max_queue_size, min_queue_interval, max_queue_interval);
		object().CObjectHandler::set_goal(eObjectActionIdle, object().best_weapon(), min_queue_size, max_queue_size,
										  min_queue_interval, max_queue_interval);
	}
	else
		object().CObjectHandler::set_goal(eObjectActionIdle, object().best_weapon());

	object().sight().setup(CSightAction(SightManager::eSightTypeCurrentDirection, true, true));
	object().sound().play(eStalkerSoundInjuring);
}

void CStalkerActionCriticalHit::finalize()
{
	inherited::finalize();
	m_storage->set_property(eWorldPropertyCriticallyWounded, false);
}

void CStalkerActionCriticalHit::execute()
{
	inherited::execute();
}
