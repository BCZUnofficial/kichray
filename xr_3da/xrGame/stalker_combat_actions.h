////////////////////////////////////////////////////////////////////////////
//	Module 		: stalker_combat_actions.h
//	Created 	: 25.03.2004
//  Modified 	: 26.03.2004
//	Author		: Dmitriy Iassenev
//	Description : Stalker combat action classes
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "stalker_combat_action_base.h"
#include "random32.h"

class CCoverPoint;

namespace MonsterSpace
{
enum EBodyState;
enum EMovementType;
} // namespace MonsterSpace

//////////////////////////////////////////////////////////////////////////
// CStalkerActionGetItemToKill
//////////////////////////////////////////////////////////////////////////

class CStalkerActionGetItemToKill : public CStalkerActionCombatBase
{
  protected:
	typedef CStalkerActionCombatBase inherited;

  public:
	CStalkerActionGetItemToKill(CAI_Stalker* object, LPCSTR action_name = "");
	virtual void initialize();
	virtual void execute();
	virtual void finalize();
};

//////////////////////////////////////////////////////////////////////////
// CStalkerActionMakeItemKilling
//////////////////////////////////////////////////////////////////////////

class CStalkerActionMakeItemKilling : public CStalkerActionCombatBase
{
  protected:
	typedef CStalkerActionCombatBase inherited;

  public:
	CStalkerActionMakeItemKilling(CAI_Stalker* object, LPCSTR action_name = "");
	virtual void initialize();
	virtual void execute();
	virtual void finalize();
};

//////////////////////////////////////////////////////////////////////////
// CStalkerActionRetreatFromEnemy
//////////////////////////////////////////////////////////////////////////

class CStalkerActionRetreatFromEnemy : public CStalkerActionCombatBase
{
  protected:
	typedef CStalkerActionCombatBase inherited;

  public:
	CStalkerActionRetreatFromEnemy(CAI_Stalker* object, LPCSTR action_name = "");
	virtual void initialize();
	virtual void execute();
	virtual void finalize();
	virtual _edge_value_type weight(const CSConditionState& condition0, const CSConditionState& condition1) const;
};

//////////////////////////////////////////////////////////////////////////
// CStalkerActionGetReadyToKill
//////////////////////////////////////////////////////////////////////////

class CStalkerActionGetReadyToKill : public CStalkerActionCombatBase
{
  protected:
	typedef CStalkerActionCombatBase inherited;

  private:
	MonsterSpace::EBodyState m_body_state;
	MonsterSpace::EMovementType m_movement_type;
	bool m_affect_properties;
	bool m_enable_enemy_change;

  public:
	CStalkerActionGetReadyToKill(bool affect_properties, CAI_Stalker* object, LPCSTR action_name = "");
	virtual void initialize();
	virtual void execute();
	virtual void finalize();
};

//////////////////////////////////////////////////////////////////////////
// CStalkerActionKillEnemy
//////////////////////////////////////////////////////////////////////////

class CStalkerActionKillEnemy : public CStalkerActionCombatBase
{
  private:
	const CCoverPoint* m_fire_recovery_cover;
	u32 m_fire_recovery_started;
	u32 m_fire_recovery_settled;
	u32 m_fire_recovery_last_attempt;
	bool m_fire_recovery_retry;

  protected:
	typedef CStalkerActionCombatBase inherited;

  public:
	CStalkerActionKillEnemy(CAI_Stalker* object, LPCSTR action_name = "");
	virtual void initialize();
	virtual void execute();
	virtual void finalize();
};

//////////////////////////////////////////////////////////////////////////
// CStalkerActionTakeCover
//////////////////////////////////////////////////////////////////////////

class CStalkerActionTakeCover : public CStalkerActionCombatBase
{
  protected:
	typedef CStalkerActionCombatBase inherited;

  private:
	MonsterSpace::EBodyState m_body_state;
	MonsterSpace::EMovementType m_movement_type;

  public:
	CStalkerActionTakeCover(CAI_Stalker* object, LPCSTR action_name = "");
	virtual void initialize();
	virtual void execute();
	virtual void finalize();
};

//////////////////////////////////////////////////////////////////////////
// CStalkerActionLookOut
//////////////////////////////////////////////////////////////////////////

class CStalkerActionLookOut : public CStalkerActionCombatBase
{
  protected:
	typedef CStalkerActionCombatBase inherited;

  private:
	u32 m_last_change_time;
	CRandom32 m_crouch_look_out_random;
	Fvector m_peek_anchor;
	u32 m_peek_last_query;
	u32 m_peek_started;
	bool m_peek_moving;

  public:
	CStalkerActionLookOut(CAI_Stalker* object, LPCSTR action_name = "");
	virtual void initialize();
	virtual void execute();
	virtual void finalize();
};

//////////////////////////////////////////////////////////////////////////
// CStalkerActionHoldPosition
//////////////////////////////////////////////////////////////////////////

class CStalkerActionHoldPosition : public CStalkerActionCombatBase
{
  protected:
	typedef CStalkerActionCombatBase inherited;

  public:
	CStalkerActionHoldPosition(CAI_Stalker* object, LPCSTR action_name = "");
	virtual void initialize();
	virtual void execute();
	virtual void finalize();
};

//////////////////////////////////////////////////////////////////////////
// CStalkerActionGetDistance
//////////////////////////////////////////////////////////////////////////

class CStalkerActionGetDistance : public CStalkerActionCombatBase
{
  protected:
	typedef CStalkerActionCombatBase inherited;

  public:
	CStalkerActionGetDistance(CAI_Stalker* object, LPCSTR action_name = "");
	virtual void initialize();
	virtual void execute();
	virtual void finalize();
};

//////////////////////////////////////////////////////////////////////////
// CStalkerActionDetourEnemy
//////////////////////////////////////////////////////////////////////////

class CStalkerActionDetourEnemy : public CStalkerActionCombatBase
{
  private:
	const CCoverPoint* m_enemy_cover;

  protected:
	typedef CStalkerActionCombatBase inherited;

  public:
	CStalkerActionDetourEnemy(CAI_Stalker* object, LPCSTR action_name = "");
	virtual void initialize();
	virtual void execute();
	virtual void finalize();
};

//////////////////////////////////////////////////////////////////////////
// CStalkerActionSearchEnemy
//////////////////////////////////////////////////////////////////////////

class CStalkerActionSearchEnemy : public CStalkerActionCombatBase
{
  private:
	Fvector m_search_anchor;
	Fvector m_search_observed_position;
	u16 m_search_enemy_id;
	Fvector m_search_direction;
	u32 m_search_vertex;
	u32 m_search_memory_time;
	u32 m_search_stage_time;
	u32 m_search_stage;
	bool m_search_has_anchor;
	bool m_search_moving;
    bool m_sneak_decided, m_sneak_active, m_sneak_arrived;
    u32 m_sneak_started, m_sneak_arrival_time, m_sneak_hit_revision, m_sneak_near_miss_revision;
    float m_sneak_health;
    Fvector m_sneak_destination;
    void stop_sneak(const char* reason);
    bool try_sneak(u32 now, bool squad_visible);
    bool update_sneak(u32 now);

  protected:
	typedef CStalkerActionCombatBase inherited;

  public:
	CStalkerActionSearchEnemy(CAI_Stalker* object, LPCSTR action_name = "");
	virtual void initialize();
	virtual void execute();
	virtual void finalize();
};

//////////////////////////////////////////////////////////////////////////
// CStalkerActionPostCombatWait
//////////////////////////////////////////////////////////////////////////

class CStalkerActionPostCombatWait : public CStalkerActionCombatBase
{
  protected:
	typedef CStalkerActionCombatBase inherited;

  public:
	CStalkerActionPostCombatWait(CAI_Stalker* object, LPCSTR action_name = "");
	virtual void initialize();
	virtual void execute();
	virtual void finalize();
};

//////////////////////////////////////////////////////////////////////////
// CStalkerActionHideFromGrenade
//////////////////////////////////////////////////////////////////////////

class CStalkerActionHideFromGrenade : public CStalkerActionCombatBase
{
  private:
	u32 m_grenade_last_query;
	u16 m_grenade_object_id;
	Fvector m_grenade_last_position;
	bool m_grenade_escape_active;
  protected:
	typedef CStalkerActionCombatBase inherited;

  public:
	CStalkerActionHideFromGrenade(CAI_Stalker* object, LPCSTR action_name = "");
	virtual void initialize();
	virtual void execute();
	virtual void finalize();
};

//////////////////////////////////////////////////////////////////////////
// CStalkerActionSuddenAttack
//////////////////////////////////////////////////////////////////////////

class CStalkerActionSuddenAttack : public CStalkerActionCombatBase
{
  private:
	u32 m_sudden_started;
  protected:
	typedef CStalkerActionCombatBase inherited;

  public:
	CStalkerActionSuddenAttack(CAI_Stalker* object, LPCSTR action_name = "");
	virtual void initialize();
	virtual void execute();
	virtual void finalize();
};

//////////////////////////////////////////////////////////////////////////
// CStalkerActionKillEnemyIfPlayerOnThePath
//////////////////////////////////////////////////////////////////////////

class CStalkerActionKillEnemyIfPlayerOnThePath : public CStalkerActionCombatBase
{
  protected:
	typedef CStalkerActionCombatBase inherited;

  public:
	CStalkerActionKillEnemyIfPlayerOnThePath(CAI_Stalker* object, LPCSTR action_name = "");
	virtual void initialize();
	virtual void execute();
	virtual void finalize();
};

//////////////////////////////////////////////////////////////////////////
// CStalkerActionCriticalHit
//////////////////////////////////////////////////////////////////////////

class CStalkerActionCriticalHit : public CStalkerActionCombatBase
{
  protected:
	typedef CStalkerActionCombatBase inherited;

  public:
	CStalkerActionCriticalHit(CAI_Stalker* object, LPCSTR action_name = "");
	virtual void initialize();
	virtual void execute();
	virtual void finalize();
};

#include "stalker_combat_actions_inline.h"
