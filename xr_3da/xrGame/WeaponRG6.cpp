
#include "stdafx.h"
#include "WeaponRG6.h"

#include "entity.h"
#include "explosiveRocket.h"
#include "level.h"
#include "clsid_game.h"
#include "inventory.h"
#include "inventoryOwner.h"
#include "MathUtils.h"

#ifdef DEBUG
#include "phdebug.h"
#endif

CWeaponRG6::CWeaponRG6() : m_pendingRocketSpawns(0)
{
}

CWeaponRG6::~CWeaponRG6()
{
}

void CWeaponRG6::SpawnMissingRockets()
{
	if (OnClient())
		return;

	if (iAmmoElapsed <= 0 || m_magazine.empty())
		return;

	// Учитываем уже существующие гранаты и ожидаемые
	// события их прикрепления.
	const u32 present = getRocketCount() + m_pendingRocketSpawns;

	const u32 required = (u32)iAmmoElapsed;

	if (present >= required)
		return;

	// Тип берём из реально заряженного патрона.
	shared_str grenade_name = m_magazine.back().m_ammoSect;

	shared_str fake_grenade_name = pSettings->r_string(*grenade_name, "fake_grenade_name");

	if (!fake_grenade_name.size())
		return;

	const u32 missing = required - present;

	for (u32 i = 0; i < missing; ++i)
	{
		inheritedRL::SpawnRocket(*fake_grenade_name, this);

		++m_pendingRocketSpawns;
	}
}

BOOL CWeaponRG6::net_Spawn(CSE_Abstract* DC)
{
	m_pendingRocketSpawns = 0;

	BOOL result = inheritedSG::net_Spawn(DC);

	if (!result)
		return result;

	SpawnMissingRockets();

	return result;
}

void CWeaponRG6::Load(LPCSTR section)
{
	inheritedRL::Load(section);
	inheritedSG::Load(section);
}

void CWeaponRG6::FireStart()
{
	if (GetState() != eIdle)
		return;

	// При пустом барабане передаём управление
	// штатной логике: щелчок или автоперезарядка.
	// Даже если осталась внутренняя fake-граната,
	// запускать её нельзя.
	if (iAmmoElapsed <= 0)
	{
		inheritedSG::FireStart();
		return;
	}

	// Патрон есть, но физическая граната
	// ещё не прикреплена. Выстрел запрещён.
	if (!getCurrentRocket())
		return;

	// Только переводим оружие в состояние стрельбы.
	// Саму гранату запускаем в OnShot().
	inheritedSG::FireStart();
}

void CWeaponRG6::OnShot()
{
	// Штатные звук, анимация и эффекты выстрела.
	inheritedSG::OnShot();

	// Дополнительная защита от рассинхронизации.
	if (iAmmoElapsed <= 0 || !getCurrentRocket())
		return;

	Fvector p1, d;
	p1.set(get_LastFP());
	d.set(get_LastFD());

	CEntity* E = smart_cast<CEntity*>(H_Parent());

	if (E)
	{
		CInventoryOwner* io = smart_cast<CInventoryOwner*>(H_Parent());

		if (io && NULL == io->inventory().ActiveItem())
		{
			Log("current_state", GetState());
			Log("next_state", GetNextState());
			Log("state_time", m_dwStateTime);
			Log("item_sect", cNameSect().c_str());
			Log("H_Parent", H_Parent()->cNameSect().c_str());
		}

		E->g_fireParams(this, p1, d);
	}

	Fmatrix launch_matrix;
	launch_matrix.identity();

	launch_matrix.k.set(d);

	Fvector::generate_orthonormal_basis(launch_matrix.k, launch_matrix.j, launch_matrix.i);

	launch_matrix.c.set(p1);

	if (IsZoomed() && H_Parent() && H_Parent()->CLS_ID == CLSID_OBJECT_ACTOR)
	{
		H_Parent()->setEnabled(FALSE);
		setEnabled(FALSE);

		collide::rq_result RQ;

		BOOL HasPick = Level().ObjectSpace.RayPick(p1, d, 300.0f, collide::rqtStatic, RQ, this);

		setEnabled(TRUE);
		H_Parent()->setEnabled(TRUE);

		if (HasPick)
		{
			Fvector Transference;
			Transference.mul(d, RQ.range);

			Fvector res[2];

#ifdef DEBUG
			DBG_OpenCashedDraw();

			DBG_DrawLine(p1, Fvector().add(p1, d), D3DCOLOR_XRGB(255, 0, 0));
#endif

			u8 canfire0 = TransferenceAndThrowVelToThrowDir(Transference, CRocketLauncher::m_fLaunchSpeed,
															EffectiveGravity(), res);

#ifdef DEBUG
			if (canfire0 > 0)
				DBG_DrawLine(p1, Fvector().add(p1, res[0]), D3DCOLOR_XRGB(0, 255, 0));

			if (canfire0 > 1)
				DBG_DrawLine(p1, Fvector().add(p1, res[1]), D3DCOLOR_XRGB(0, 0, 255));

			DBG_ClosedCashedDraw(30000);
#endif

			if (canfire0 != 0)
				d = res[0];
		}
	}

	d.normalize();
	d.mul(m_fLaunchSpeed);

	VERIFY2(_valid(launch_matrix), "CWeaponRG6::OnShot. Invalid launch_matrix");

	CRocketLauncher::LaunchRocket(launch_matrix, d, zero_vel);

	CExplosiveRocket* pGrenade = smart_cast<CExplosiveRocket*>(getCurrentRocket());

	VERIFY(pGrenade);

	if (H_Parent())
		pGrenade->SetInitiator(H_Parent()->ID());

	if (OnServer())
	{
		NET_Packet P;

		u_EventGen(P, GE_LAUNCH_ROCKET, ID());

		P.w_u16(u16(getCurrentRocket()->ID()));

		u_EventSend(P);
	}

	dropCurrentRocket();
}

u8 CWeaponRG6::AddCartridge(u8 cnt)
{
	// Запоминаем фактическое количество патронов.
	const u32 before = (u32)m_magazine.size();

	inheritedSG::AddCartridge(cnt);

	const u32 after = (u32)m_magazine.size();
	const u32 loaded = after - before;

	// Создаём только недостающие гранаты.
	// Старые fake-объекты не дублируем.
	SpawnMissingRockets();

	// Возвращаем число НЕ добавленных патронов.
	// Это нужно автомату трёхфазной перезарядки.
	return (u8)(cnt - loaded);
}

void CWeaponRG6::OnEvent(NET_Packet& P, u16 type)
{
	inheritedSG::OnEvent(P, type);

	u16 id;

	switch (type)
	{
	case GE_OWNERSHIP_TAKE: {
		P.r_u16(id);

		inheritedRL::AttachRocket(id, this);

		// Получено подтверждение прикрепления.
		if (m_pendingRocketSpawns > 0)
			--m_pendingRocketSpawns;

		break;
	}

	case GE_OWNERSHIP_REJECT:
	case GE_LAUNCH_ROCKET: {
		const bool bLaunch = (type == GE_LAUNCH_ROCKET);

		P.r_u16(id);

		inheritedRL::DetachRocket(id, bLaunch);

		break;
	}
	}
}
