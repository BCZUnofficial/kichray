#include "stdafx.h"
#include <dinput.h>
#include "HUDmanager.h"
#include "../xr_ioconsole.h"
#include "../xr_ioc_cmd.h"
#include "entity_alive.h"
#include "game_sv_single.h"
#include "alife_simulator.h"
#include "alife_simulator_header.h"
#include "level_graph.h"
#include "../fdemorecord.h"
#include "level.h"
#include "xr_level_controller.h"
#include "game_cl_base.h"
#include "stalker_movement_manager.h"
#include "Inventory.h"
#include "WeaponHUD.h"
#include "xrServer.h"
#include "autosave_manager.h"

#include "actor.h"
#include "huditem.h"
#include "ui/UIDialogWnd.h"
#include "clsid_game.h"
#include "../xr_input.h"
#include "saved_game_wrapper.h"

#ifdef DEBUG
#include "ai/monsters/BaseMonster/base_monster.h"
#endif

#ifdef DEBUG
extern void try_change_current_entity();
extern void restore_actor();
#endif

bool g_bDisableAllInput = false;
extern float g_fTimeFactor;

// KichRay: fast-forward only the in-game clock/environment.
// This does NOT change Device.time_factor(), so physics, actor movement and FPS
// keep running at normal real-time speed.
static const float KICH_SKIPTIME_FACTOR = 1000.f;
static bool g_kich_skiptime_console = false;
static bool g_kich_skiptime_key = false;
static bool g_kich_skiptime_active = false;
static bool g_kich_skiptime_command_registered = false;
static game_sv_Single* g_kich_skiptime_owner = 0;
static float g_kich_skiptime_restore_game = 1.f;
static float g_kich_skiptime_restore_environment = 1.f;

static game_sv_Single* kich_skiptime_single_game()
{
	// IMPORTANT: GameID() calls Game().Type(), and Game() dereferences
	// Level().game. During CLevel::IR_OnActivate() the level object may already
	// exist while the client game state is still null. Never call GameID() here.
	if (!g_pGameLevel)
		return 0;

	CLevel& level = Level();
	if (!level.Server || !level.Server->game)
		return 0;

	game_sv_Single* game_single = smart_cast<game_sv_Single*>(level.Server->game);
	if (!game_single || game_single->Type() != GAME_SINGLE)
		return 0;

	return game_single;
}

static void kich_skiptime_apply()
{
	game_sv_Single* game_single = kich_skiptime_single_game();
	const bool enable = g_kich_skiptime_console || g_kich_skiptime_key;

	if (!game_single)
	{
		g_kich_skiptime_owner = 0;
		g_kich_skiptime_active = false;
		return;
	}

	// A level transition creates a new server game object. Establish a fresh
	// restore baseline instead of reusing factors captured on the old level.
	if (g_kich_skiptime_owner != game_single)
	{
		g_kich_skiptime_owner = game_single;
		g_kich_skiptime_active = false;
	}

	if (enable)
	{
		if (!g_kich_skiptime_active)
		{
			g_kich_skiptime_restore_game = game_single->GetGameTimeFactor();
			g_kich_skiptime_restore_environment =
				game_single->game_GameState::GetGameTimeFactor();
			g_kich_skiptime_active = true;
		}

		// Single-player ALife uses game_sv_Single::SetGameTimeFactor().
		game_single->SetGameTimeFactor(KICH_SKIPTIME_FACTOR);

		// In SoC single-player, game_sv_Single::GetEnvironmentGameTime() and
		// GetEnvironmentGameTimeFactor() intentionally read the inherited BASE
		// game clock (not the separate environment fields). Update that base clock
		// explicitly so sky, sun and weather advance together with ALife time.
		game_single->game_GameState::SetGameTimeFactor(KICH_SKIPTIME_FACTOR);
		return;
	}

	if (!g_kich_skiptime_active)
		return;

	game_single->SetGameTimeFactor(g_kich_skiptime_restore_game);
	game_single->game_GameState::SetGameTimeFactor(g_kich_skiptime_restore_environment);
	g_kich_skiptime_active = false;
}

// Used by CALifeTimeManager::save(): saving during fast-forward keeps the
// already reached game time, but writes the normal pre-skip factor to the save.
bool kich_skiptime_save_factor(float& factor)
{
	// Do not reuse a factor captured from an already destroyed server game
	// object during a level transition/load.
	game_sv_Single* game_single = kich_skiptime_single_game();
	if (!g_kich_skiptime_active || !game_single || g_kich_skiptime_owner != game_single)
		return false;

	factor = g_kich_skiptime_restore_game;
	return true;
}

class CCC_KichSkipTime : public IConsole_Command
{
  public:
	CCC_KichSkipTime(LPCSTR N) : IConsole_Command(N){};

	virtual void Execute(LPCSTR args)
	{
		if (EQ(args, "1") || EQ(args, "on"))
			g_kich_skiptime_console = true;
		else if (EQ(args, "0") || EQ(args, "off"))
			g_kich_skiptime_console = false;
		else
		{
			InvalidSyntax();
			return;
		}

		kich_skiptime_apply();
		Msg("* g_skiptime: %s", g_kich_skiptime_console ? "on" : "off");
	}

	virtual void Status(TStatus& S)
	{
		strcpy_s(S, g_kich_skiptime_console ? "1" : "0");
	}

	virtual void Info(TInfo& I)
	{
		strcpy_s(I, "'1/0' or 'on/off'");
	}

	virtual void Save(IWriter*)
	{
		// Runtime experiment: do not write g_skiptime to user.ltx.
	}
};

static void kich_skiptime_register_command()
{
	if (g_kich_skiptime_command_registered || !Console)
		return;

	static CCC_KichSkipTime skiptime_command("g_skiptime");
	Console->AddCommand(&skiptime_command);
	g_kich_skiptime_command_registered = true;
}

#define CURRENT_ENTITY() (game ? ((GameID() == GAME_SINGLE) ? CurrentEntity() : CurrentControlEntity()) : NULL)

void CLevel::IR_OnMouseWheel(int direction)
{
	if (g_bDisableAllInput)
		return;

	if (HUD().GetUI()->IR_OnMouseWheel(direction))
		return;
	if (Device.Paused())
		return;

	if (game && Game().IR_OnMouseWheel(direction))
		return;

	if (HUD().GetUI()->MainInputReceiver())
		return;
	if (CURRENT_ENTITY())
	{
		IInputReceiver* IR = smart_cast<IInputReceiver*>(smart_cast<CGameObject*>(CURRENT_ENTITY()));
		if (IR)
			IR->IR_OnMouseWheel(direction);
	}
}

static int mouse_button_2_key[] = {MOUSE_1, MOUSE_2, MOUSE_3};

void CLevel::IR_OnMousePress(int btn)
{
	IR_OnKeyboardPress(mouse_button_2_key[btn]);
}
void CLevel::IR_OnMouseRelease(int btn)
{
	IR_OnKeyboardRelease(mouse_button_2_key[btn]);
}
void CLevel::IR_OnMouseHold(int btn)
{
	IR_OnKeyboardHold(mouse_button_2_key[btn]);
}

void CLevel::IR_OnMouseMove(int dx, int dy)
{
	if (g_bDisableAllInput)
		return;
	if (pHUD->GetUI()->IR_OnMouseMove(dx, dy))
		return;
	if (Device.Paused())
		return;
	if (CURRENT_ENTITY())
	{
		IInputReceiver* IR = smart_cast<IInputReceiver*>(smart_cast<CGameObject*>(CURRENT_ENTITY()));
		if (IR)
			IR->IR_OnMouseMove(dx, dy);
	}
}

class vtune_
{
	BOOL enabled_;

  public:
	vtune_()
	{
		enabled_ = FALSE;
	}
	void enable()
	{
		if (!enabled_)
		{
			Engine.External.tune_resume();
			enabled_ = TRUE;
			Msg("vtune : enabled");
		}
	}
	void disable()
	{
		if (enabled_)
		{
			Engine.External.tune_pause();
			enabled_ = FALSE;
			Msg("vtune : disabled");
		}
	}
} vtune;

// Îáðàáîòêà íàæàòèÿ êëàâèø
extern bool g_block_pause;

void CLevel::IR_OnKeyboardPress(int key)
{
	kich_skiptime_register_command();
	kich_skiptime_apply();

	// NUMPAD7 is a dedicated hold-to-fast-forward key. It is deliberately
	// handled before action bindings so a mod/user bind cannot leave the mode
	// stuck when the key is released.
	if (key == DIK_NUMPAD7 && IsGameTypeSingle() && !g_bDisableAllInput && !Device.Paused())
	{
		g_kich_skiptime_key = true;
		kich_skiptime_apply();
		return;
	}

	bool b_ui_exist = (pHUD && pHUD->GetUI());

	//.	if (DIK_F10 == key)		vtune.enable();
	//.	if (DIK_F11 == key)		vtune.disable();

	EGameActions _curr = get_binded_action(key);
	switch (_curr)
	{

	case kSCREENSHOT:
		Render->Screenshot();
		return;
		break;

	case kCONSOLE:
		Console->Show();
		return;
		break;

	case kQUIT: {
		if (b_ui_exist && HUD().GetUI()->MainInputReceiver())
		{
			if (HUD().GetUI()->MainInputReceiver()->IR_OnKeyboardPress(key))
				return; // special case for mp and main_menu
			HUD().GetUI()->StartStopMenu(HUD().GetUI()->MainInputReceiver(), true);
		}
		else
			Console->Execute("main_menu");
		return;
	}
	break;

	case kPAUSE:
		if (!g_block_pause)
		{
			if (IsGameTypeSingle())
			{
				Device.Pause(!Device.Paused(), TRUE, TRUE, "li_pause_key");
			}
		}
		return;
		break;
	};

	if (g_bDisableAllInput)
		return;
	if (!b_ui_exist)
		return;

	if (b_ui_exist && pHUD->GetUI()->IR_OnKeyboardPress(key))
		return;

	if (Device.Paused())
		return;

	if (game && Game().IR_OnKeyboardPress(key))
		return;

	if (_curr == kQUICK_SAVE && IsGameTypeSingle())
	{
		Console->Execute("save");
		return;
	}
	if (_curr == kQUICK_LOAD && IsGameTypeSingle())
	{
#ifdef DEBUG
		FS.get_path("$game_config$")->m_Flags.set(FS_Path::flNeedRescan, TRUE);
		FS.get_path("$game_scripts$")->m_Flags.set(FS_Path::flNeedRescan, TRUE);
		FS.rescan_pathes();
#endif // DEBUG
		string_path saved_game, command;
		strconcat(sizeof(saved_game), saved_game, Core.UserName, "_", "quicksave");
		if (!CSavedGameWrapper::valid_saved_game(saved_game))
			return;

		strconcat(sizeof(command), command, "load ", saved_game);
		Console->Execute(command);
		return;
	}

#ifndef MASTER_GOLD
	switch (key)
	{
	case DIK_NUMPAD5: {
		if (GameID() != GAME_SINGLE)
		{
			Msg("For this game type Demo Record is disabled.");
			///				return;
		};
		Console->Hide();
		Console->Execute("demo_record 1");
	}
	break;
#endif // MASTER_GOLD
#ifdef DEBUG
	case DIK_RETURN:
		bDebug = !bDebug;
		return;

	case DIK_BACK:
		if (GameID() == GAME_SINGLE)
			HW.Caps.SceneMode = (HW.Caps.SceneMode + 1) % 3;
		return;

	case DIK_F4: {
		if (pInput->iGetAsyncKeyState(DIK_LALT))
			break;

		if (pInput->iGetAsyncKeyState(DIK_RALT))
			break;

		bool bOk = false;
		u32 i = 0, j, n = Objects.o_count();
		if (pCurrentEntity)
			for (; i < n; ++i)
				if (Objects.o_get_by_iterator(i) == pCurrentEntity)
					break;
		if (i < n)
		{
			j = i;
			bOk = false;
			for (++i; i < n; ++i)
			{
				CEntityAlive* tpEntityAlive = smart_cast<CEntityAlive*>(Objects.o_get_by_iterator(i));
				if (tpEntityAlive)
				{
					bOk = true;
					break;
				}
			}
			if (!bOk)
				for (i = 0; i < j; ++i)
				{
					CEntityAlive* tpEntityAlive = smart_cast<CEntityAlive*>(Objects.o_get_by_iterator(i));
					if (tpEntityAlive)
					{
						bOk = true;
						break;
					}
				}
			if (bOk)
			{
				CObject* tpObject = CurrentEntity();
				CObject* __I = Objects.o_get_by_iterator(i);
				CObject** I = &__I;

				SetEntity(*I);
				if (tpObject != *I)
				{
					CActor* pActor = smart_cast<CActor*>(tpObject);
					if (pActor)
						pActor->inventory().Items_SetCurrentEntityHud(false);
				}
				if (tpObject)
				{
					Engine.Sheduler.Unregister(tpObject);
					Engine.Sheduler.Register(tpObject, TRUE);
				};
				Engine.Sheduler.Unregister(*I);
				Engine.Sheduler.Register(*I, TRUE);

				CActor* pActor = smart_cast<CActor*>(*I);
				if (pActor)
				{
					pActor->inventory().Items_SetCurrentEntityHud(true);

					CHudItem* pHudItem = smart_cast<CHudItem*>(pActor->inventory().ActiveItem());
					if (pHudItem)
					{
						pHudItem->OnStateSwitch(pHudItem->GetState());
					}
				}
			}
		}
		return;
	}
	case MOUSE_1: {
		if (GameID() != GAME_SINGLE)
			break;
		if (pInput->iGetAsyncKeyState(DIK_LALT))
		{
			if (CurrentEntity()->CLS_ID == CLSID_OBJECT_ACTOR)
				try_change_current_entity();
			else
				restore_actor();
			return;
		}
		break;
	}
		/**/

	case DIK_DIVIDE:
		if (OnServer())
		{
			//			float NewTimeFactor				= pSettings->r_float("alife","time_factor");

			if (GameID() == GAME_SINGLE)
				Server->game->SetGameTimeFactor(g_fTimeFactor);
			else
			{
				Server->game->SetEnvironmentGameTimeFactor(g_fTimeFactor);
				Server->game->SetGameTimeFactor(g_fTimeFactor);
			};
		}
		break;
	case DIK_MULTIPLY:
		if (OnServer())
		{
			float NewTimeFactor = 1000.f;
			if (GameID() == GAME_SINGLE)
				Server->game->SetGameTimeFactor(NewTimeFactor);
			else
			{
				Server->game->SetEnvironmentGameTimeFactor(NewTimeFactor);
				//				Server->game->SetGameTimeFactor(NewTimeFactor);
			};
		}
		break;
#endif
#ifdef DEBUG
	case DIK_F9: {
		//		if (!ai().get_alife())
		//			break;
		//		const_cast<CALifeSimulatorHeader&>(ai().alife().header()).set_state(ALife::eZoneStateSurge);
		if (GameID() != GAME_SINGLE)
		{
			extern INT g_sv_SendUpdate;
			g_sv_SendUpdate = 1;
		};
		break;
	}
		return;
//	case DIK_F10:{
//		ai().level_graph().set_dest_point();
//		ai().level_graph().build_detail_path();
//		if (!Objects.FindObjectByName("m_stalker_e0000") || !Objects.FindObjectByName("localhost/dima"))
//			return;
//		if (!m_bSynchronization) {
//			m_bSynchronization	= true;
//			ai().level_graph().set_start_point();
//			m_bSynchronization	= false;
//		}
//		luabind::functor<void>	functor;
//		ai().script_engine().functor("alife_test.set_switch_online",functor);
//		functor(0,false);
//	}
//		return;
//	case DIK_F11:
//		ai().level_graph().build_detail_path();
//		if (!Objects.FindObjectByName("m_stalker_e0000") || !Objects.FindObjectByName("localhost/dima"))
//			return;
//		if (!m_bSynchronization) {
//			m_bSynchronization	= true;
//			ai().level_graph().set_dest_point();
//			ai().level_graph().select_cover_point();
//			m_bSynchronization	= false;
//		}
//		return;
#endif // DEBUG
#ifndef MASTER_GOLD
	}
#endif // MASTER_GOLD

	if (bindConsoleCmds.execute(key))
		return;

	if (b_ui_exist && HUD().GetUI()->MainInputReceiver())
		return;
	if (CURRENT_ENTITY())
	{
		IInputReceiver* IR = smart_cast<IInputReceiver*>(smart_cast<CGameObject*>(CURRENT_ENTITY()));
		if (IR)
			IR->IR_OnKeyboardPress(get_binded_action(key));
	}

#ifdef _DEBUG
	CObject* obj = Level().Objects.FindObjectByName("monster");
	if (obj)
	{
		CBaseMonster* monster = smart_cast<CBaseMonster*>(obj);
		if (monster)
			monster->debug_on_key(key);
	}
#endif
}

void CLevel::IR_OnKeyboardRelease(int key)
{
	if (key == DIK_NUMPAD7)
	{
		g_kich_skiptime_key = false;
		kich_skiptime_apply();
		return;
	}

	bool b_ui_exist = (pHUD && pHUD->GetUI());

	if (g_bDisableAllInput)
		return;
	if (b_ui_exist && pHUD->GetUI()->IR_OnKeyboardRelease(key))
		return;
	if (Device.Paused())
		return;
	if (game && Game().OnKeyboardRelease(get_binded_action(key)))
		return;

	if (b_ui_exist && HUD().GetUI()->MainInputReceiver())
		return;
	if (CURRENT_ENTITY())
	{
		IInputReceiver* IR = smart_cast<IInputReceiver*>(smart_cast<CGameObject*>(CURRENT_ENTITY()));
		if (IR)
			IR->IR_OnKeyboardRelease(get_binded_action(key));
	}
}

void CLevel::IR_OnKeyboardHold(int key)
{
	if (g_bDisableAllInput)
		return;

	bool b_ui_exist = (pHUD && pHUD->GetUI());

	if (b_ui_exist && pHUD->GetUI()->IR_OnKeyboardHold(key))
		return;
	if (b_ui_exist && HUD().GetUI()->MainInputReceiver())
		return;
	if (Device.Paused())
		return;
	if (CURRENT_ENTITY())
	{
		IInputReceiver* IR = smart_cast<IInputReceiver*>(smart_cast<CGameObject*>(CURRENT_ENTITY()));
		if (IR)
			IR->IR_OnKeyboardHold(get_binded_action(key));
	}
}

void CLevel::IR_OnMouseStop(int /**axis/**/, int /**value/**/)
{
}

void CLevel::IR_OnActivate()
{
	kich_skiptime_register_command();

	// IR_OnActivate is also called while a level is still loading. At that point
	// CLevel may exist before Level().game/server game are fully initialized.
	// Only restore a physical-key fast-forward that was actually active; never
	// touch game time unconditionally from this startup callback.
	if (g_kich_skiptime_key)
	{
		g_kich_skiptime_key = false;
		kich_skiptime_apply();
	}

	if (!pInput)
		return;
	int i;
	for (i = 0; i < CInput::COUNT_KB_BUTTONS; i++)
	{
		if (IR_GetKeyState(i))
		{

			EGameActions action = get_binded_action(i);
			switch (action)
			{
			case kFWD:
			case kBACK:
			case kL_STRAFE:
			case kR_STRAFE:
			case kLEFT:
			case kRIGHT:
			case kUP:
			case kDOWN:
			case kCROUCH:
			case kACCEL:
			case kL_LOOKOUT:
			case kR_LOOKOUT:
			case kWPN_FIRE: {
				IR_OnKeyboardPress(i);
			}
			break;
			};
		};
	}
}
