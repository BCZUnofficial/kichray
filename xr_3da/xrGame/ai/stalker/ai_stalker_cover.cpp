////////////////////////////////////////////////////////////////////////////
//	Module 		: ai_stalker_cover.cpp
//	Created 	: 25.04.2006
//  Modified 	: 25.04.2006
//	Author		: Dmitriy Iassenev
//	Description :
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "ai_stalker.h"
#include "../../cover_point.h"
#include "../../cover_evaluators.h"
#include "../../ai_space.h"
#include "../../cover_manager.h"
#include "../../stalker_movement_restriction.h"
#include "../../level_graph.h"
#include "../../stalker_movement_manager.h"
#include "../../level_path_manager.h"
#include "../../agent_manager.h"
#include "../../agent_member_manager.h"
#include "../../agent_location_manager.h"
#include "../../inventory_item.h"
#include "../../stalker_combat_cover_policy.h"
#include "../../stalker_combat_diagnostics.h"

extern const float MIN_SUITABLE_ENEMY_DISTANCE = 3.f;

#ifdef _DEBUG
static int g_advance_search_count = 0;
static int g_near_cover_search_count = 0;
static int g_far_cover_search_count = 0;
#endif

void CAI_Stalker::subscribe_on_best_cover_changed(const on_best_cover_changed_delegate& delegate)
{
	VERIFY(m_cover_delegates.end() == std::find(m_cover_delegates.begin(), m_cover_delegates.end(), delegate));
	m_cover_delegates.push_back(delegate);
}

void CAI_Stalker::unsubscribe_on_best_cover_changed(const on_best_cover_changed_delegate& delegate)
{
	cover_delegates::iterator I = std::find(m_cover_delegates.begin(), m_cover_delegates.end(), delegate);
	VERIFY(I != m_cover_delegates.end());
	m_cover_delegates.erase(I);
}

void CAI_Stalker::on_best_cover_changed(const CCoverPoint* new_cover, const CCoverPoint* old_cover)
{
	cover_delegates::const_iterator I = m_cover_delegates.begin();
	cover_delegates::const_iterator E = m_cover_delegates.end();
	for (; I != E; ++I)
		(*I)(new_cover, old_cover);
}

namespace
{
float combat_cover_exposure(const CCoverPoint* cover, const Fvector& threat)
{
    Fvector direction;
    float yaw, pitch;
    direction.sub(threat, cover->position());
    direction.getHP(yaw, pitch);
    return ai().level_graph().cover_in_direction(yaw, cover->level_vertex_id());
}

class CStalkerCombatCoverEvaluator : public CCoverEvaluatorBest
{
    int m_weapon_type;
public:
    CStalkerCombatCoverEvaluator(CAI_Stalker* object)
        : CCoverEvaluatorBest(&object->movement().restrictions()),
          m_weapon_type(object->best_weapon() ? object->best_weapon()->object().ef_weapon_type() : 6) {}

    void evaluate(const CCoverPoint* cover, float weight)
    {
        if (weight <= EPS_L) return;
        const float enemy_distance = m_enemy_position.distance_to(cover->position());
        if (enemy_distance < m_min_distance) return;
        if (enemy_distance > m_max_distance && enemy_distance > m_current_distance) return;
        Fvector direction;
        direction.sub(m_enemy_position, cover->position());
        const float score = stalker_combat_cover_policy::cover_score(
            combat_cover_exposure(cover, m_enemy_position),
            ai().level_graph().neighbour_in_direction(direction, cover->level_vertex_id()), weight,
            m_start_position.distance_to(cover->position()), enemy_distance, m_weapon_type);
        if (score > m_best_value) return;
        if (score == m_best_value && m_selected && cover->level_vertex_id() >= m_selected->level_vertex_id()) return;
        m_best_value = score;
        m_selected = cover;
    }
};

class CCombatCoverRestrictor : public CStalkerMovementRestrictor
{
    CAI_Stalker* m_stalker;
public:
    explicit CCombatCoverRestrictor(CAI_Stalker* object)
        : CStalkerMovementRestrictor(object, true, false), m_stalker(object) {}
    bool operator()(const CCoverPoint* cover) const
    {
        return !m_stalker->combat_cover_blocked(cover) && CStalkerMovementRestrictor::operator()(cover);
    }
};
}

void CAI_Stalker::reset_best_cover_state()
{
    m_best_cover = 0;
    m_best_cover_actual = false;
    m_best_cover_value = flt_max;
    m_best_cover_can_try_advance = false;
    m_best_cover_advance_cover = 0;
    m_best_cover_last_search_time = Device.dwTimeGlobal - 1000u;
    m_best_cover_last_change_time = Device.dwTimeGlobal;
    m_best_cover_search_position = Position();
    m_best_cover_enemy_position = Position();
    m_best_cover_exposure = 0.f;
    m_failed_combat_cover_next = 0;
    for (u32 i = 0; i != 4; ++i) { m_failed_combat_covers[i].cover = 0; m_failed_combat_covers[i].time = 0; }
    m_cover_progress_point = 0;
    m_cover_progress_position = Position();
    m_cover_progress_time = Device.dwTimeGlobal;
}

void CAI_Stalker::invalidate_best_cover()
{
    m_best_cover_actual = false;
}

bool CAI_Stalker::combat_cover_blocked(const CCoverPoint* cover) const
{
    for (u32 i = 0; i != 4; ++i)
        if (m_failed_combat_covers[i].cover == cover &&
            !stalker_combat_cover_policy::failure_expired(Device.dwTimeGlobal - m_failed_combat_covers[i].time))
            return true;
    return false;
}

void CAI_Stalker::abandon_best_cover(const char* reason)
{
    if (!m_best_cover) return;
    const u32 index = m_failed_combat_cover_next++ % 4;
    m_failed_combat_covers[index].cover = m_best_cover;
    m_failed_combat_covers[index].time = Device.dwTimeGlobal;
    m_cover_progress_point = 0;
    invalidate_best_cover();
    agent_manager().member().member(this).cover(0);
    if (stalker_combat_diagnostics::enabled(ID()))
        stalker_combat_diagnostics::log(ID(), stalker_combat_diagnostics::movement, reason,
            "cover=%u retry_ms=5000", m_best_cover->level_vertex_id());
}

bool CAI_Stalker::update_cover_progress()
{
    if (!m_best_cover || movement().target_movement_type() == MonsterSpace::eMovementTypeStand ||
        movement().level_dest_vertex_id() != m_best_cover->level_vertex_id() || movement().path_completed() ||
        Position().distance_to_sqr(m_best_cover->position()) < 1.f)
    {
        m_cover_progress_point = 0;
        return false;
    }
    if (m_cover_progress_point != m_best_cover || Position().distance_to_sqr(m_cover_progress_position) >= .1225f)
    {
        m_cover_progress_point = m_best_cover;
        m_cover_progress_position = Position();
        m_cover_progress_time = Device.dwTimeGlobal;
        return false;
    }
    const bool failed = movement().level_path().failed();
    if (!stalker_combat_cover_policy::movement_stalled(Device.dwTimeGlobal - m_cover_progress_time, failed)) return false;
    abandon_best_cover(failed ? "cover_path_failed" : "cover_no_progress");
    return true;
}

const CCoverPoint* CAI_Stalker::find_best_cover(const Fvector& position_to_cover_from)
{
    // A stack evaluator has no shared inertia: the bounded stalker cache is the single owner of actuality.
    CStalkerCombatCoverEvaluator evaluator(this);
    CCombatCoverRestrictor restrictor(this);
    evaluator.setup(position_to_cover_from, MIN_SUITABLE_ENEMY_DISTANCE, 170.f, MIN_SUITABLE_ENEMY_DISTANCE);
    const CCoverPoint* point = ai().cover_manager().best_cover(Position(), 10.f, evaluator, restrictor);
    if (point) return point;
    evaluator.setup(position_to_cover_from, MIN_SUITABLE_ENEMY_DISTANCE, 170.f, MIN_SUITABLE_ENEMY_DISTANCE);
    return ai().cover_manager().best_cover(Position(), 30.f, evaluator, restrictor);
}

float CAI_Stalker::best_cover_value(const Fvector& position_to_cover_from)
{
    if (!m_best_cover) return flt_max;
    CStalkerCombatCoverEvaluator evaluator(this);
    evaluator.setup(position_to_cover_from, MIN_SUITABLE_ENEMY_DISTANCE, 170.f, MIN_SUITABLE_ENEMY_DISTANCE);
    evaluator.initialize(Position(), true);
    evaluator.evaluate(m_best_cover, CStalkerMovementRestrictor(this, true).weight(m_best_cover));
    return evaluator.best_value();
}

void CAI_Stalker::accept_best_cover(const CCoverPoint* cover, const Fvector& threat)
{
    if (cover != m_best_cover)
    {
        const CCoverPoint* old_cover = m_best_cover;
        on_best_cover_changed(cover, old_cover);
        m_best_cover = cover;
        m_best_cover_last_change_time = Device.dwTimeGlobal;
        m_cover_progress_point = 0;
        if (stalker_combat_diagnostics::enabled(ID()))
            stalker_combat_diagnostics::log(ID(), stalker_combat_diagnostics::cover, "cover_changed",
                "old=%u new=%u", old_cover ? old_cover->level_vertex_id() : u32(-1),
                cover ? cover->level_vertex_id() : u32(-1));
    }
    agent_manager().location().make_suitable(this, cover);
    m_best_cover_value = best_cover_value(threat);
    m_best_cover_exposure = cover ? combat_cover_exposure(cover, threat) : 0.f;
    m_best_cover_enemy_position = threat;
    m_best_cover_search_position = Position();
    m_best_cover_last_search_time = Device.dwTimeGlobal;
    m_best_cover_advance_cover = 0;
    m_best_cover_can_try_advance = false;
    m_best_cover_actual = true;
}

void CAI_Stalker::best_cover_can_try_advance()
{
    if (m_best_cover && Device.dwTimeGlobal - m_best_cover_last_search_time >= 1000u)
        m_best_cover_can_try_advance = true;
}

void CAI_Stalker::update_best_cover_actuality(const Fvector& threat)
{
    if (!m_best_cover_actual) return;
    const u32 age = Device.dwTimeGlobal - m_best_cover_last_search_time;
    if (!m_best_cover)
    {
        if (stalker_combat_cover_policy::retry_empty(age, Position().distance_to_sqr(m_best_cover_search_position),
                                                    threat.distance_to_sqr(m_best_cover_enemy_position)))
            invalidate_best_cover();
        return;
    }
    if (combat_cover_blocked(m_best_cover) || !movement().accessible(m_best_cover->position()) ||
        !CStalkerMovementRestrictor(this, true, false)(m_best_cover) ||
        m_best_cover->position().distance_to_sqr(threat) < _sqr(MIN_SUITABLE_ENEMY_DISTANCE))
    {
        invalidate_best_cover();
        return;
    }
    if (age >= 750u && (stalker_combat_cover_policy::exposure_worsened(
            combat_cover_exposure(m_best_cover, threat), m_best_cover_exposure) ||
            best_cover_value(threat) >= m_best_cover_value + 1.f))
    {
        invalidate_best_cover();
        return;
    }
    if (age >= 1000u && (m_best_cover_can_try_advance ||
            threat.distance_to_sqr(m_best_cover_enemy_position) >= 9.f ||
            Position().distance_to_sqr(m_best_cover_search_position) >= 16.f))
        invalidate_best_cover();
}

const CCoverPoint* CAI_Stalker::best_cover(const Fvector& threat)
{
    update_cover_progress();
    update_best_cover_actuality(threat);
    if (m_best_cover_actual) return m_best_cover;
    const CCoverPoint* candidate = find_best_cover(threat);
    const bool old_safe = m_best_cover && !combat_cover_blocked(m_best_cover) &&
        movement().accessible(m_best_cover->position()) && CStalkerMovementRestrictor(this, true, false)(m_best_cover) &&
        m_best_cover->position().distance_to_sqr(threat) >= _sqr(MIN_SUITABLE_ENEMY_DISTANCE) &&
        CStalkerMovementRestrictor(this, true, false).weight(m_best_cover) >= .4f;
    if (candidate != m_best_cover && old_safe)
    {
        if (!candidate) candidate = m_best_cover;
        else
        {
            CStalkerCombatCoverEvaluator evaluator(this);
            evaluator.setup(threat, MIN_SUITABLE_ENEMY_DISTANCE, 170.f, MIN_SUITABLE_ENEMY_DISTANCE);
            evaluator.initialize(Position(), true);
            evaluator.evaluate(candidate, CStalkerMovementRestrictor(this, true, false).weight(candidate));
            if (!stalker_combat_cover_policy::accept_change(best_cover_value(threat), evaluator.best_value(),
                    Device.dwTimeGlobal - m_best_cover_last_change_time, old_safe))
                candidate = m_best_cover;
        }
    }
    accept_best_cover(candidate, threat);
    return m_best_cover;
}

void CAI_Stalker::on_restrictions_change()
{
	inherited::on_restrictions_change();
	invalidate_best_cover();
#ifdef _DEBUG
	Msg("* [%6d][%s] on_restrictions_change", Device.dwTimeGlobal, *cName());
#endif
}

void CAI_Stalker::on_enemy_change(const CEntityAlive* enemy)
{
	inherited::on_enemy_change(enemy);
	m_item_actuality = false;
	invalidate_best_cover();
#ifdef _DEBUG
//	Msg									("* [%6d][%s] on_enemy_change",Device.dwTimeGlobal,*cName());
#endif
}

void CAI_Stalker::on_danger_location_add(const CDangerLocation& location)
{
	if (!m_best_cover)
		return;

	if (m_best_cover->position().distance_to_sqr(location.position()) <= _sqr(location.m_radius))
	{
#ifdef _DEBUG
//		Msg								("* [%6d][%s] on_danger_add",Device.dwTimeGlobal,*cName());
#endif
		invalidate_best_cover();
	}
}

void CAI_Stalker::on_danger_location_remove(const CDangerLocation& location)
{
	if (!m_best_cover)
	{
		if (Position().distance_to_sqr(location.position()) <= _sqr(location.m_radius))
		{
#ifdef _DEBUG
//			Msg							("* [%6d][%s] on_danger_remove",Device.dwTimeGlobal,*cName());
#endif
			invalidate_best_cover();
		}

		return;
	}

	if (m_best_cover->position().distance_to_sqr(location.position()) <= _sqr(location.m_radius))
	{
#ifdef _DEBUG
//		Msg								("* [%6d][%s] on_danger_remove",Device.dwTimeGlobal,*cName());
#endif
		invalidate_best_cover();
	}
}

void CAI_Stalker::on_cover_blocked(const CCoverPoint* cover)
{
#ifdef _DEBUG
//	Msg									("* [%6d][%s] cover is blocked",Device.dwTimeGlobal,*cName());
#endif
	invalidate_best_cover();
}
