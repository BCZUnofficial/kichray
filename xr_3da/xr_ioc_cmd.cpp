#include "stdafx.h"
#include "kichgelkhut_console_filter.h"
#include "igame_level.h"

#include "xr_effgamma.h"
#include "x_ray.h"
#include "xr_ioconsole.h"
#include "xr_ioc_cmd.h"
#include "fbasicvisual.h"
#include "cameramanager.h"
#include "environment.h"
#include "IGame_Persistent.h"
#include "xr_input.h"
#include "CustomHUD.h"
#include "SkeletonAnimated.h"
#include "ResourceManager.h"

#include "xr_object.h"
#include "KichR4Native.h"
#include "KichR4RecordControl.h"
#include "KichR4OpticalRules.h"

xr_token snd_freq_token[] = {{"22khz", sf_22K}, {"44khz", sf_44K}, {0, 0}};
xr_token snd_model_token[] = {{"Default", 0}, {"Normal", 1}, {"Light", 2}, {"High", 3}, {0, 0}};

extern xr_token* vid_mode_token;

xr_token vid_quality_token[] = {{"renderer_r1", 0}, {"renderer_r2a", 1}, {"renderer_r2", 2}, {0, 0}};

xr_token vid_bpp_token[] = {{"16", 16}, {"32", 32}, {0, 0}};
// E1.7 recording commands. Kept in the EXE; none launches a full snapshot.
#if !defined(DEDICATED_SERVER) && !defined(_EDITOR)
namespace {
bool R4RecordCommandAvailable(){
    if(KichR4_NativeActive())return true;
    Msg("! R4 recording commands require native R4 (-kich_r4_game).");return false;
}
bool R4RecordBoolean(LPCSTR args,bool& value){
    if(!args || (args[0]!='0' && args[0]!='1') || args[1])return false;
    value=args[0]=='1';return true;
}
}
class CCC_LogRecord : public IConsole_Command {
public:
    CCC_LogRecord(LPCSTR name):IConsole_Command(name){bEmptyArgsHandled=TRUE;}
    void Execute(LPCSTR args){
        if(!R4RecordCommandAvailable())return;
        if(!args || !args[0]){Msg("[R4 record] requested=%u active=%u; log_record 1 starts, 0 stops; errors remain enabled",
            unsigned(kich_r4_record::Requested()),unsigned(kich_r4_record::Enabled()));return;}
        bool value=false;if(!R4RecordBoolean(args,value)){Msg("! usage: log_record 0|1");return;}
        KichR4_RecordSet(value);
        Msg("[R4 record] requested=%u; applies at next frame boundary; errors remain enabled",unsigned(value));
    }
    void Status(TStatus& value){sprintf_s(value,"%u",unsigned(kich_r4_record::Requested()));}
    void Save(IWriter*){}
};
class CCC_LogMark : public IConsole_Command {
public:
    CCC_LogMark(LPCSTR name):IConsole_Command(name){bEmptyArgsHandled=TRUE;bLowerCaseArgs=FALSE;}
    void Execute(LPCSTR args){
        if(!R4RecordCommandAvailable())return;
        unsigned length=0;
        if(args)for(;length<64 && args[length];++length){const unsigned char c=static_cast<unsigned char>(args[length]);
            if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.' || c==':'))break;}
        if(!args || !length || length>=64 || args[length]){Msg("! usage: log_mark label (1..63 letters/digits or _-.:)");return;}
        if(!kich_r4_record::Requested()){Msg("! log_mark requires log_record 1");return;}
        KichR4_RecordMark(args);
    }
    void Save(IWriter*){}
};
class CCC_LogWatch : public IConsole_Command {
public:
    CCC_LogWatch(LPCSTR name):IConsole_Command(name){bEmptyArgsHandled=TRUE;}
    void Execute(LPCSTR args){
        if(!R4RecordCommandAvailable())return;
        bool value=false;if(!R4RecordBoolean(args,value)){Msg("! usage: log_watch 0|1 (1 selects an object along the camera ray)");return;}
        if(value && !kich_r4_record::Requested()){Msg("! log_watch 1 requires log_record 1");return;}
        KichR4_RecordWatch(value);
    }
    void Save(IWriter*){}
};
class CCC_R4DebugView : public IConsole_Command {
public:
    CCC_R4DebugView(LPCSTR name):IConsole_Command(name){bEmptyArgsHandled=TRUE;}
    void Execute(LPCSTR args){
        if(!R4RecordCommandAvailable())return;
        if(!args || !args[0]){const unsigned view=KichR4_RecordView();Msg("[R4 record] debug_view=%u %s",view,kich_r4_optics::DebugView(view));return;}
        unsigned view=kich_r4_optics::DebugViewCount;
        const char* aliases[]={"normal","hemi","baked_rgb","sun_mask","sun_shadow"};
        const unsigned values[]={0,5,6,7,8};
        for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i)if(!xr_strcmp(args,aliases[i])){view=values[i];break;}
        if(view==kich_r4_optics::DebugViewCount){unsigned value=0,i=0;
            for(;args[i]>='0' && args[i]<='9' && i<3;++i)value=value*10+unsigned(args[i]-'0');
            if(i && !args[i] && value<kich_r4_optics::DebugViewCount)view=value;}
        if(view>=kich_r4_optics::DebugViewCount){Msg("! usage: r4_debug_view normal|hemi|baked_rgb|sun_mask|sun_shadow or 0..%u",unsigned(kich_r4_optics::DebugViewCount)-1);return;}
        KichR4_RecordView(view);Msg("[R4 record] debug_view=%u %s; diagnostic frames excluded from normal FPS",view,kich_r4_optics::DebugView(view));
    }
    void Status(TStatus& value){sprintf_s(value,"%u",KichR4_RecordView());}
    void Save(IWriter*){}
};
#endif
// End E1.7 recording commands.
#if !defined(DEDICATED_SERVER) && !defined(_EDITOR)
// P0-S11 env_sun_override <altitude_deg> <heading_deg> [added_colour]: hold the
// sun/moon at one direction (heading and altitude it shines from, as the F9 log
// and CEffect_Thunderbolt::Bolt measure them) and optionally add a colour to it,
// as a thunderbolt adds 0.9 of its flash. CEnvironment::OnFrame applies it in
// the engine, so R2 and R4 render the same light at one pose: a lightning flash
// (altitude 10..20, about the sun's heading) is otherwise random. "off" releases
// it; no arguments print the current sun. Not saved.
ENGINE_API Fvector4 ps_env_sun_override = {0, 0, 0, 0}; // altitude, heading, added colour, enabled
class CCC_EnvSunOverride : public IConsole_Command {
public:
    CCC_EnvSunOverride(LPCSTR name):IConsole_Command(name){bEmptyArgsHandled=TRUE;}
    void Execute(LPCSTR args){
        if(!args || !args[0]){
            if(g_pGamePersistent){Fvector from;from.set(g_pGamePersistent->Environment().CurrentEnv.sun_dir).invert();float h,p;from.getHP(h,p);
                Msg("env_sun_override: sun from heading %.1f altitude %.1f deg; override %s",rad2deg(h),rad2deg(p),ps_env_sun_override.w>.5f?"on":"off");}
            return;}
        if(!xr_strcmp(args,"off")){ps_env_sun_override.w=0;Msg("env_sun_override: off");return;}
        float altitude=0,heading=0,added=0;const int read=sscanf(args,"%f %f %f",&altitude,&heading,&added);
        if(read<2 || !_valid(altitude) || !_valid(heading) || !_valid(added)){
            Msg("! usage: env_sun_override <altitude_deg 1..89> <heading_deg> [added_colour] | off");return;}
        ps_env_sun_override.set(clampr(altitude,1.f,89.f),heading,_max(added,0.f),1.f);
        Msg("env_sun_override: from heading %.1f altitude %.1f deg, colour +%.2f",ps_env_sun_override.y,ps_env_sun_override.x,ps_env_sun_override.z);
    }
    void Status(TStatus& value){if(ps_env_sun_override.w>.5f)sprintf_s(value,"%.1f %.1f %.2f",ps_env_sun_override.x,ps_env_sun_override.y,ps_env_sun_override.z);else strcpy_s(value,"off");}
    void Save(IWriter*){}
};
#endif
//-----------------------------------------------------------------------
class CCC_Quit : public IConsole_Command
{
  public:
	CCC_Quit(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = TRUE;
	};
	virtual void Execute(LPCSTR args)
	{
		//		TerminateProcess(GetCurrentProcess(),0);
		Console->Hide();
		Engine.Event.Defer("KERNEL:disconnect");
		Engine.Event.Defer("KERNEL:quit");
	}
};
//-----------------------------------------------------------------------
#ifdef DEBUG_MEMORY_MANAGER
class CCC_MemStat : public IConsole_Command
{
  public:
	CCC_MemStat(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = TRUE;
	};
	virtual void Execute(LPCSTR args)
	{
		string_path fn;
		if (args && args[0])
			sprintf_s(fn, sizeof(fn), "%s.dump", args);
		else
			strcpy_s_s(fn, sizeof(fn), "x:\\$memory$.dump");
		Memory.mem_statistic(fn);
		//		g_pStringContainer->dump			();
		//		g_pSharedMemoryContainer->dump		();
	}
};
#endif // DEBUG_MEMORY_MANAGER

#ifdef DEBUG_MEMORY_MANAGER
class CCC_DbgMemCheck : public IConsole_Command
{
  public:
	CCC_DbgMemCheck(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = TRUE;
	};
	virtual void Execute(LPCSTR args)
	{
		if (Memory.debug_mode)
		{
			Memory.dbg_check();
		}
		else
		{
			Msg("~ Run with -mem_debug options.");
		}
	}
};
#endif // DEBUG_MEMORY_MANAGER

class CCC_DbgStrCheck : public IConsole_Command
{
  public:
	CCC_DbgStrCheck(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = TRUE;
	};
	virtual void Execute(LPCSTR args)
	{
		g_pStringContainer->verify();
	}
};

class CCC_DbgStrDump : public IConsole_Command
{
  public:
	CCC_DbgStrDump(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = TRUE;
	};
	virtual void Execute(LPCSTR args)
	{
		g_pStringContainer->dump();
	}
};

//-----------------------------------------------------------------------
class CCC_MotionsStat : public IConsole_Command
{
  public:
	CCC_MotionsStat(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = TRUE;
	};
	virtual void Execute(LPCSTR args)
	{
		g_pMotionsContainer->dump();
	}
};
class CCC_TexturesStat : public IConsole_Command
{
  public:
	CCC_TexturesStat(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = TRUE;
	};
	virtual void Execute(LPCSTR args)
	{
		Device.Resources->_DumpMemoryUsage();
	}
};
//-----------------------------------------------------------------------
class CCC_E_Dump : public IConsole_Command
{
  public:
	CCC_E_Dump(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = TRUE;
	};
	virtual void Execute(LPCSTR args)
	{
		Engine.Event.Dump();
	}
};
class CCC_E_Signal : public IConsole_Command
{
  public:
	CCC_E_Signal(LPCSTR N) : IConsole_Command(N){};
	virtual void Execute(LPCSTR args)
	{
		char Event[128], Param[128];
		Event[0] = 0;
		Param[0] = 0;
		sscanf(args, "%[^,],%s", Event, Param);
		Engine.Event.Signal(Event, (u64)Param);
	}
};
//-----------------------------------------------------------------------
class CCC_Help : public IConsole_Command
{
  public:
	CCC_Help(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = TRUE;
	};
	virtual void Execute(LPCSTR args)
	{
		Log("- --- Command listing: start ---");
		CConsole::vecCMD_IT it;
		for (it = Console->Commands.begin(); it != Console->Commands.end(); it++)
		{
			IConsole_Command& C = *(it->second);
			TStatus _S;
			C.Status(_S);
			TInfo _I;
			C.Info(_I);

			Msg("%-20s (%-10s) --- %s", C.Name(), _S, _I);
		}
		Log("- --- Command listing: end ----");
	}
};
//-----------------------------------------------------------------------
void crashthread(void*)
{
	Sleep(1000);
	Msg("~ crash thread activated");
	u64 clk = CPU::GetCLK();
	CRandom rndg;
	rndg.seed(s32(clk));
	for (;;)
	{
		Sleep(1);
		__try
		{
			// try {
			union {
				struct
				{
					u8 _b0;
					u8 _b1;
					u8 _b2;
					u8 _b3;
				};
				uintptr_t _ptri;
				u32* _ptr;
			} rndptr;
			rndptr._b0 = u8(rndg.randI(0, 256));
			rndptr._b1 = u8(rndg.randI(0, 256));
			rndptr._b2 = u8(rndg.randI(0, 256));
			rndptr._b3 = u8(rndg.randI(0, 256));
			rndptr._ptri &= (1ul < 31ul) - 1;
			*rndptr._ptr = 0xBAADF00D;
			//} catch(...) {
			//	// OK
			//}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			// OK
		}
	}
}
class CCC_Crash : public IConsole_Command
{
  public:
	CCC_Crash(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = TRUE;
	};
	virtual void Execute(LPCSTR args)
	{
		thread_spawn(crashthread, "crash", 0, 0);
	}
};

class CCC_DumpResources : public IConsole_Command
{
  public:
	CCC_DumpResources(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = TRUE;
	};
	virtual void Execute(LPCSTR args)
	{
		Device.Resources->Dump(args != NULL);
	}
};

XRCORE_API void _dump_open_files(int mode);
class CCC_DumpOpenFiles : public IConsole_Command
{
  public:
	CCC_DumpOpenFiles(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = FALSE;
	};
	virtual void Execute(LPCSTR args)
	{
		int _mode = atoi(args);
		_dump_open_files(_mode);
	}
};

//-----------------------------------------------------------------------
class CCC_SaveCFG : public IConsole_Command
{
  public:
	CCC_SaveCFG(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = TRUE;
	};
	virtual void Execute(LPCSTR args)
	{
		string_path cfg_full_name;
		strcpy_s(cfg_full_name, (xr_strlen(args) > 0) ? args : Console->ConfigFile);

		bool b_abs_name = xr_strlen(cfg_full_name) > 2 && cfg_full_name[1] == ':';

		if (!b_abs_name)
			FS.update_path(cfg_full_name, "$app_data_root$", cfg_full_name);

		if (strext(cfg_full_name))
			*strext(cfg_full_name) = 0;
		strcat(cfg_full_name, ".ltx");

		BOOL b_allow = TRUE;
		if (FS.exist(cfg_full_name))
			b_allow = SetFileAttributes(cfg_full_name, FILE_ATTRIBUTE_NORMAL);

		if (b_allow)
		{
			IWriter* F = FS.w_open(cfg_full_name);
			CConsole::vecCMD_IT it;
			for (it = Console->Commands.begin(); it != Console->Commands.end(); it++)
				it->second->Save(F);
			FS.w_close(F);
			Msg("Config-file [%s] saved successfully", cfg_full_name);
		}
		else
			Msg("!Cannot store config file [%s]", cfg_full_name);
	}
};
CCC_LoadCFG::CCC_LoadCFG(LPCSTR N) : IConsole_Command(N){};

void CCC_LoadCFG::Execute(LPCSTR args)
{
	Msg("Executing config-script \"%s\"...", args);
	string_path cfg_name;

	strcpy_s(cfg_name, args);
	if (strext(cfg_name))
		*strext(cfg_name) = 0;
	strcat(cfg_name, ".ltx");

	string_path cfg_full_name;

	FS.update_path(cfg_full_name, "$app_data_root$", cfg_name);

	if (NULL == FS.exist(cfg_full_name))
		strcpy_s(cfg_full_name, cfg_name);

	IReader* F = FS.r_open(cfg_full_name);

	string1024 str;
	if (F != NULL)
	{
		while (!F->eof())
		{
			F->r_string(str, sizeof(str));
			if (allow(str))
			{
				char command[128];
				if (kichgelkhut_console::debug_config_command(str, command) &&
					Console->Commands.find(command) == Console->Commands.end())
				{
					Msg("* [config] Skipped unavailable debug command: %s", command);
					continue;
				}
				Console->Execute(str);
			}
		}
		FS.r_close(F);
		Msg("[%s] successfully loaded.", cfg_full_name);
	}
	else
	{
		Msg("! Cannot open script file [%s]", cfg_full_name);
	}
}

CCC_LoadCFG_custom::CCC_LoadCFG_custom(LPCSTR cmd) : CCC_LoadCFG(cmd)
{
	strcpy_s(m_cmd, cmd);
};
bool CCC_LoadCFG_custom::allow(LPCSTR cmd)
{
	return (cmd == strstr(cmd, m_cmd));
};

//-----------------------------------------------------------------------
class CCC_Start : public IConsole_Command
{
	void parse(LPSTR dest, LPCSTR args, LPCSTR name)
	{
		dest[0] = 0;
		if (strstr(args, name))
			sscanf(strstr(args, name) + xr_strlen(name), "(%[^)])", dest);
	}

  public:
	CCC_Start(LPCSTR N) : IConsole_Command(N){};
	virtual void Execute(LPCSTR args)
	{
		/*		if (g_pGameLevel)	{
					Log		("! Please disconnect/unload first");
					return;
				}
		*/
		string4096 op_server, op_client;
		op_server[0] = 0;
		op_client[0] = 0;

		parse(op_server, args, "server"); // 1. server
		parse(op_client, args, "client"); // 2. client

		if (!op_client[0] && strstr(op_server, "single"))
			strcpy_s(op_client, "localhost");

		if (0 == xr_strlen(op_client))
		{
			Log("! Can't start game without client. Arguments: '%s'.", args);
			return;
		}
		Engine.Event.Defer("KERNEL:start", u64(xr_strlen(op_server) ? xr_strdup(op_server) : 0),
						   u64(xr_strdup(op_client)));
	}
};

class CCC_Disconnect : public IConsole_Command
{
  public:
	CCC_Disconnect(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = TRUE;
	};
	virtual void Execute(LPCSTR args)
	{
		Engine.Event.Defer("KERNEL:disconnect");
	}
};
//-----------------------------------------------------------------------
class CCC_VID_Reset : public IConsole_Command
{
  public:
	CCC_VID_Reset(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = TRUE;
	};
	virtual void Execute(LPCSTR args)
	{
		if (Device.b_is_Ready)
		{
			Device.Reset();
		}
	}
};
class CCC_VidMode : public CCC_Token
{
	u32 _dummy;

  public:
	CCC_VidMode(LPCSTR N) : CCC_Token(N, &_dummy, NULL)
	{
		bEmptyArgsHandled = FALSE;
	};
	virtual void Execute(LPCSTR args)
	{
		u32 _w, _h;
		int cnt = sscanf(args, "%dx%d", &_w, &_h);
		if (cnt == 2)
		{
			psCurrentVidMode[0] = _w;
			psCurrentVidMode[1] = _h;
		}
		else
		{
			Msg("! Wrong video mode [%s]", args);
			return;
		}
	}
	virtual void Status(TStatus& S)
	{
		sprintf_s(S, sizeof(S), "%dx%d", psCurrentVidMode[0], psCurrentVidMode[1]);
	}
	virtual xr_token* GetToken()
	{
		return vid_mode_token;
	}
	virtual void Info(TInfo& I)
	{
		strcpy_s(I, sizeof(I), "change screen resolution WxH");
	}
};
//-----------------------------------------------------------------------
class CCC_SND_Restart : public IConsole_Command
{
  public:
	CCC_SND_Restart(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = TRUE;
	};
	virtual void Execute(LPCSTR args)
	{
		Sound->_restart();
	}
};

//-----------------------------------------------------------------------
float ps_gamma = 1.f, ps_brightness = 1.f, ps_contrast = 1.f;
class CCC_Gamma : public CCC_Float
{
  public:
	CCC_Gamma(LPCSTR N, float* V) : CCC_Float(N, V, 0.5f, 1.5f)
	{
	}

	virtual void Execute(LPCSTR args)
	{
		CCC_Float::Execute(args);
		Device.Gamma.Gamma(ps_gamma);
		Device.Gamma.Brightness(ps_brightness);
		Device.Gamma.Contrast(ps_contrast);
		Device.Gamma.Update();
	}
};

//-----------------------------------------------------------------------

extern INT g_bDR_LM_UsePointsBBox;
extern INT g_bDR_LM_4Steps;
extern INT g_iDR_LM_Step;
extern Fvector g_DR_LM_Min, g_DR_LM_Max;

class CCC_DR_ClearPoint : public IConsole_Command
{
  public:
	CCC_DR_ClearPoint(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = TRUE;
	};
	virtual void Execute(LPCSTR args)
	{
		g_DR_LM_Min.x = 1000000.0f;
		g_DR_LM_Min.z = 1000000.0f;

		g_DR_LM_Max.x = -1000000.0f;
		g_DR_LM_Max.z = -1000000.0f;

		Msg("Local BBox (%f, %f) - (%f, %f)", g_DR_LM_Min.x, g_DR_LM_Min.z, g_DR_LM_Max.x, g_DR_LM_Max.z);
	}
};

class CCC_DR_TakePoint : public IConsole_Command
{
  public:
	CCC_DR_TakePoint(LPCSTR N) : IConsole_Command(N)
	{
		bEmptyArgsHandled = TRUE;
	};
	virtual void Execute(LPCSTR args)
	{
		Fvector CamPos = Device.vCameraPosition;

		if (g_DR_LM_Min.x > CamPos.x)
			g_DR_LM_Min.x = CamPos.x;
		if (g_DR_LM_Min.z > CamPos.z)
			g_DR_LM_Min.z = CamPos.z;

		if (g_DR_LM_Max.x < CamPos.x)
			g_DR_LM_Max.x = CamPos.x;
		if (g_DR_LM_Max.z < CamPos.z)
			g_DR_LM_Max.z = CamPos.z;

		Msg("Local BBox (%f, %f) - (%f, %f)", g_DR_LM_Min.x, g_DR_LM_Min.z, g_DR_LM_Max.x, g_DR_LM_Max.z);
	}
};

class CCC_DR_UsePoints : public CCC_Integer
{
  public:
	CCC_DR_UsePoints(LPCSTR N, int* V, int _min = 0, int _max = 999) : CCC_Integer(N, V, _min, _max){};
	virtual void Save(IWriter* F){};
};

ENGINE_API BOOL r2_sun_static = TRUE;

u32 renderer_value = 0;
class CCC_r2 : public CCC_Token
{
	typedef CCC_Token inherited;

  public:
	CCC_r2(LPCSTR N) : inherited(N, &renderer_value, vid_quality_token)
	{
		renderer_value = 0;
	};

	virtual void Execute(LPCSTR args)
	{
#ifdef DEDICATED_SERVER
		inherited::Execute("renderer_r1");
#else
		inherited::Execute(args);
#endif // DEDICATED_SERVER

		psDeviceFlags.set(rsR2, (renderer_value > 0));
		r2_sun_static = (renderer_value != 2);
	}

	virtual void Save(IWriter* F)
	{
		if (!strstr(Core.Params, "-r2"))
		{
			inherited::Save(F);
		}
	}
};
//-----------------------------------------------------------------------
ENGINE_API float psHUD_FOV = 0.45f;

extern int psSkeletonUpdate;
extern int rsDVB_Size;
extern int rsDIB_Size;
extern int psNET_ClientUpdate;
extern int psNET_ClientPending;
extern int psNET_ServerUpdate;
extern int psNET_ServerPending;
extern int psNET_DedicatedSleep;
extern char psNET_Name[32];
extern Flags32 psEnvFlags;
extern float r__dtex_range;

extern int g_ErrorLineCount;

ENGINE_API int ps_r__Supersample = 1;
// P0-S5 R4 DX11 volumetric sun shafts; independent of the R2 module's r2_sun_shafts.
ENGINE_API int ps_r4_sun_shafts = 2;
ENGINE_API float ps_r4_sun_shafts_intensity = 1.f;
// P0-S5 R4 sun shadow map resolution (rounded to 1024/2048/4096).
ENGINE_API int ps_r4_sun_shadow_size = 4096;
// P0-S7 R4 512 m sun cascade resolution (rounded to 2048/4096/8192): its texels are the widest.
ENGINE_API int ps_r4_sun_far_size = 8192;
// P0-S11 R4 512 m sun cascade rendered once every N frames, reused in between (1 = every frame).
ENGINE_API int ps_r4_sun_far_interval = 2;
// P0-S11 R4 sun caster faces: 0 front, 1 R2 layout (contact front, 128/512 m back faces), 2 back faces.
ENGINE_API int ps_r4_sun_shadow_faces = 1;
// P0-S7 R4 grass in lamp (local light) shadow maps; R2 never draws it there. Leaves stay out either way.
ENGINE_API int ps_r4_lamp_grass_shadows = 0;
// P0-S9: 0 disables lamp shadow maps (diagnostic, and a performance option).
ENGINE_API int ps_r4_lamp_shadows = 1;
// P0-S11 R4 NPC torch shadows: 1 as R2 (every torch that passes the lamp selection casts); 0 NPC torches
// light without a shadow map (the actor's torch keeps its shadow).
ENGINE_API int ps_r4_npc_torch_shadows = 1;
// P0-S10 R4 shadow filter for the sun and lamps: 0 compact (four bilinear comparisons, receiver lifted
// along its normal), 1 wide (P0-S5..S9 plane-extrapolated 6x6). Sun reach mask and the lift in texels.
ENGINE_API int ps_r4_shadow_filter = 0;
ENGINE_API int ps_r4_sun_mask = 1;
ENGINE_API float ps_r4_shadow_normal_bias = 2.f;
// P0-S11 R4 shadow terminator: cm a receiver on a triangle turned from the light is lifted (0 = off, as R2,
// which has no per-facet receiver: the lift lit whole triangles and 2x2 quads unlike their neighbours).
ENGINE_API int ps_r4_shadow_terminator = 0;
// P0-S11 R4 shadow receiver lean (sun and lamps): 1 along the G-buffer (shading) normal, compact filter;
// 0 along the facet from screen derivatives (P0-S10), which is neither surface at mesh seams and creases.
ENGINE_API int ps_r4_shadow_receiver = 1;
// P0-S10 R4 shadowed-lamp fade with screen size over the r__geometry_lod window (0 = off).
ENGINE_API float ps_r4_lamp_fade = 1.f;
// P0-S11 R4 HDR probe: records the next N frames' combine terms at the screen centre in the log (counts down).
ENGINE_API int ps_r4_hdr_probe = 0;
// P0-S11 R4 lamp shadow face hold: ms an HOM-occluded face stays rendered before it reads lit (0 = at once).
ENGINE_API int ps_r4_lamp_face_hold = 100;
// P0-S11 R4 background DDS reads for world textures (UI/HUD and precache stay synchronous); 0 = all synchronous.
ENGINE_API int ps_r4_dds_async = 1;
// P0-S11 R4 detail cache/visibility and HOM on the engine's parallel thread, as R2 r2_mt (0 = on the render thread).
ENGINE_API int ps_r4_mt_details = 1;
// P0-S11 R4 helper threads that share the grass instance assembly with its thread (0 = that thread alone).
ENGINE_API int ps_r4_detail_threads = 3;
// P0-S5 R4 grass draw distance = r__detail_radius x this (cache: 192 m); grass shadows keep r__detail_radius.
ENGINE_API float ps_r4_detail_radius_scale = 3.f;
void CCC_Register()
{
	// General
#if !defined(DEDICATED_SERVER) && !defined(_EDITOR)
    CMD1(CCC_LogRecord, "log_record");
    CMD1(CCC_LogMark, "log_mark");
    CMD1(CCC_LogWatch, "log_watch");
    CMD1(CCC_R4DebugView, "r4_debug_view");
    CMD1(CCC_EnvSunOverride, "env_sun_override");
    CMD4(CCC_Integer, "r4_sun_shafts", &ps_r4_sun_shafts, 0, 3);
    CMD4(CCC_Float, "r4_sun_shafts_intensity", &ps_r4_sun_shafts_intensity, 0.f, 2.f);
    CMD4(CCC_Integer, "r4_sun_shadow_size", &ps_r4_sun_shadow_size, 1024, 4096);
    CMD4(CCC_Integer, "r4_sun_far_size", &ps_r4_sun_far_size, 2048, 8192);
    CMD4(CCC_Integer, "r4_sun_far_interval", &ps_r4_sun_far_interval, 1, 4);
    CMD4(CCC_Integer, "r4_sun_shadow_faces", &ps_r4_sun_shadow_faces, 0, 2);
    CMD4(CCC_Integer, "r4_lamp_grass_shadows", &ps_r4_lamp_grass_shadows, 0, 1);
    CMD4(CCC_Integer, "r4_lamp_shadows", &ps_r4_lamp_shadows, 0, 1);
    CMD4(CCC_Integer, "r4_npc_torch_shadows", &ps_r4_npc_torch_shadows, 0, 1);
    CMD4(CCC_Integer, "r4_shadow_filter", &ps_r4_shadow_filter, 0, 1);
    CMD4(CCC_Integer, "r4_sun_mask", &ps_r4_sun_mask, 0, 1);
    CMD4(CCC_Float, "r4_shadow_normal_bias", &ps_r4_shadow_normal_bias, 0.f, 4.f);
    CMD4(CCC_Integer, "r4_shadow_terminator", &ps_r4_shadow_terminator, 0, 100);
    CMD4(CCC_Integer, "r4_shadow_receiver", &ps_r4_shadow_receiver, 0, 1);
    CMD4(CCC_Float, "r4_lamp_fade", &ps_r4_lamp_fade, 0.f, 2.f);
    CMD4(CCC_Integer, "r4_hdr_probe", &ps_r4_hdr_probe, 0, 600);
    CMD4(CCC_Integer, "r4_lamp_face_hold", &ps_r4_lamp_face_hold, 0, 1000);
    CMD4(CCC_Integer, "r4_dds_async", &ps_r4_dds_async, 0, 1);
    CMD4(CCC_Integer, "r4_mt_details", &ps_r4_mt_details, 0, 1);
    CMD4(CCC_Integer, "r4_detail_threads", &ps_r4_detail_threads, 0, 8);
    CMD4(CCC_Float, "r4_detail_radius_scale", &ps_r4_detail_radius_scale, 1.f, 3.f);
#endif
	CMD1(CCC_Help, "help");
	CMD1(CCC_Quit, "quit");
	CMD1(CCC_Start, "start");
	CMD1(CCC_Disconnect, "disconnect");
	CMD1(CCC_SaveCFG, "cfg_save");
	CMD1(CCC_LoadCFG, "cfg_load");

#ifdef DEBUG
	//	CMD1(CCC_Crash,		"crash"					);

	CMD1(CCC_MotionsStat, "stat_motions");
	CMD1(CCC_TexturesStat, "stat_textures");
#endif

#ifdef DEBUG_MEMORY_MANAGER
	CMD1(CCC_MemStat, "dbg_mem_dump");
	CMD1(CCC_DbgMemCheck, "dbg_mem_check");
#endif // DEBUG_MEMORY_MANAGER

#ifdef DEBUG
	CMD3(CCC_Mask, "mt_particles", &psDeviceFlags, mtParticles);

	CMD1(CCC_DbgStrCheck, "dbg_str_check");
	CMD1(CCC_DbgStrDump, "dbg_str_dump");

	CMD3(CCC_Mask, "mt_sound", &psDeviceFlags, mtSound);
	CMD3(CCC_Mask, "mt_physics", &psDeviceFlags, mtPhysics);
	CMD3(CCC_Mask, "mt_network", &psDeviceFlags, mtNetwork);

	// Events
	CMD1(CCC_E_Dump, "e_list");
	CMD1(CCC_E_Signal, "e_signal");

	CMD3(CCC_Mask, "rs_wireframe", &psDeviceFlags, rsWireframe);
	CMD3(CCC_Mask, "rs_clear_bb", &psDeviceFlags, rsClearBB);
	CMD3(CCC_Mask, "rs_occlusion", &psDeviceFlags, rsOcclusion);

	CMD3(CCC_Mask, "rs_detail", &psDeviceFlags, rsDetails);
	CMD4(CCC_Float, "r__dtex_range", &r__dtex_range, 5, 175);

	CMD3(CCC_Mask, "rs_constant_fps", &psDeviceFlags, rsConstantFPS);
	CMD3(CCC_Mask, "rs_render_statics", &psDeviceFlags, rsDrawStatic);
	CMD3(CCC_Mask, "rs_render_dynamics", &psDeviceFlags, rsDrawDynamic);
#endif

	// Render device states
	CMD4(CCC_Integer, "r__supersample", &ps_r__Supersample, 1, 4);

	CMD3(CCC_Mask, "rs_v_sync", &psDeviceFlags, rsVSync);
	//	CMD3(CCC_Mask,		"rs_disable_objects_as_crows",&psDeviceFlags,	rsDisableObjectsAsCrows	);
	CMD3(CCC_Mask, "rs_fullscreen", &psDeviceFlags, rsFullscreen);
	CMD3(CCC_Mask, "rs_refresh_60hz", &psDeviceFlags, rsRefresh60hz);
	CMD3(CCC_Mask, "rs_stats", &psDeviceFlags, rsStatistic);
	CMD4(CCC_Float, "rs_vis_distance", &psVisDistance, 0.4f, 1.5f);

#ifdef DEBUG
	CMD3(CCC_Mask, "rs_cam_pos", &psDeviceFlags, rsCameraPos);
	CMD3(CCC_Mask, "rs_occ_draw", &psDeviceFlags, rsOcclusionDraw);
	CMD3(CCC_Mask, "rs_occ_stats", &psDeviceFlags, rsOcclusionStats);
	CMD4(CCC_Integer, "rs_skeleton_update", &psSkeletonUpdate, 2, 128);
#endif // DEBUG

	CMD2(CCC_Gamma, "rs_c_gamma", &ps_gamma);
	CMD2(CCC_Gamma, "rs_c_brightness", &ps_brightness);
	CMD2(CCC_Gamma, "rs_c_contrast", &ps_contrast);
	//	CMD4(CCC_Integer,	"rs_vb_size",			&rsDVB_Size,		32,		4096);
	//	CMD4(CCC_Integer,	"rs_ib_size",			&rsDIB_Size,		32,		4096);

	// Texture manager
	CMD4(CCC_Integer, "texture_lod", &psTextureLOD, 0, 4);
	CMD4(CCC_Integer, "net_dedicated_sleep", &psNET_DedicatedSleep, 0, 64);

	// General video control
	CMD1(CCC_VidMode, "vid_mode");

#ifdef DEBUG
	CMD3(CCC_Token, "vid_bpp", &psCurrentBPP, vid_bpp_token);
#endif // DEBUG

	CMD1(CCC_VID_Reset, "vid_restart");

	// Sound
	CMD2(CCC_Float, "snd_volume_eff", &psSoundVEffects);
	CMD2(CCC_Float, "snd_volume_music", &psSoundVMusic);
	//.	CMD3(CCC_Token,		"snd_freq",				&psSoundFreq,		snd_freq_token			);
	//.	CMD3(CCC_Token,		"snd_model",			&psSoundModel,		snd_model_token			);
	CMD1(CCC_SND_Restart, "snd_restart");
	CMD3(CCC_Mask, "snd_acceleration", &psSoundFlags, ss_Hardware);
	CMD3(CCC_Mask, "snd_efx", &psSoundFlags, ss_EAX);
	CMD4(CCC_Integer, "snd_targets", &psSoundTargets, 4, 32);
	CMD4(CCC_Integer, "snd_cache_size", &psSoundCacheSizeMB, 4, 32);

#ifdef DEBUG
	CMD3(CCC_Mask, "snd_stats", &g_stats_flags, st_sound);
	CMD3(CCC_Mask, "snd_stats_min_dist", &g_stats_flags, st_sound_min_dist);
	CMD3(CCC_Mask, "snd_stats_max_dist", &g_stats_flags, st_sound_max_dist);
	CMD3(CCC_Mask, "snd_stats_ai_dist", &g_stats_flags, st_sound_ai_dist);
	CMD3(CCC_Mask, "snd_stats_info_name", &g_stats_flags, st_sound_info_name);
	CMD3(CCC_Mask, "snd_stats_info_object", &g_stats_flags, st_sound_info_object);

	CMD4(CCC_Integer, "error_line_count", &g_ErrorLineCount, 6, 1024);
#endif // DEBUG

	// Mouse
	CMD3(CCC_Mask, "mouse_invert", &psMouseInvert, 1);
	psMouseSens = 0.12f;
	CMD4(CCC_Float, "mouse_sens", &psMouseSens, 0.05f, 0.6f);

	// Camera
	CMD2(CCC_Float, "cam_inert", &psCamInert);
	CMD2(CCC_Float, "cam_slide_inert", &psCamSlideInert);

	CMD1(CCC_r2, "renderer");
	// psSoundRolloff	= pSettings->r_float	("sound","rolloff");		clamp(psSoundRolloff, EPS_S,	2.f);
	psSoundOcclusionScale = pSettings->r_float("sound", "occlusion_scale");
	clamp(psSoundOcclusionScale, 0.1f, .5f);

	extern int g_Dump_Export_Obj;
	extern int g_Dump_Import_Obj;
	CMD4(CCC_Integer, "net_dbg_dump_export_obj", &g_Dump_Export_Obj, 0, 1);
	CMD4(CCC_Integer, "net_dbg_dump_import_obj", &g_Dump_Import_Obj, 0, 1);

	if (strstr(Core.Params, "designer"))
	{
		CMD1(CCC_DR_TakePoint, "demo_record_take_point");
		CMD1(CCC_DR_ClearPoint, "demo_record_clear_points");
		CMD4(CCC_DR_UsePoints, "demo_record_use_points", &g_bDR_LM_UsePointsBBox, 0, 1);
		CMD4(CCC_DR_UsePoints, "demo_record_4step", &g_bDR_LM_4Steps, 0, 1);
		CMD4(CCC_DR_UsePoints, "demo_record_step", &g_iDR_LM_Step, 0, 3);
	}
	CMD1(CCC_DumpResources, "dump_resources");
	CMD1(CCC_DumpOpenFiles, "dump_open_files");
	//#endif

	extern int g_svTextConsoleUpdateRate;
	CMD4(CCC_Integer, "sv_console_update_rate", &g_svTextConsoleUpdateRate, 1, 100);

	extern int g_svDedicateServerUpdateReate;
	CMD4(CCC_Integer, "sv_dedicated_server_update_rate", &g_svDedicateServerUpdateReate, 1, 1000);
};
