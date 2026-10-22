#include "StdAfx.h"
#include "..\_d3d_extensions.h"
#include "..\xrLevel.h"
#include "..\igame_persistent.h"
#include "..\environment.h"
#include "light_db.h"
#include "R2LightSnapshot.h"

CLight_DB::CLight_DB()
{
}

CLight_DB::~CLight_DB()
{
}

void CLight_DB::Load(IReader* fs)
{
	IReader* F = 0;

	// Lights itself
	sun_original = NULL;
	sun_adapted = NULL;
	{
		F = fs->open_chunk(fsL_LIGHT_DYNAMIC);

		u32 size = F->length();
		u32 element = sizeof(Flight) + 4;
		u32 count = size / element;
		VERIFY(count * element == size);
		v_static.reserve(count);
		for (u32 i = 0; i < count; i++)
		{
			Flight Ldata;
			light* L = Create();
			L->flags.bStatic = true;
			L->set_type(IRender_Light::POINT);

#if RENDER == R_R1
			L->set_shadow(false);
#else
			L->set_shadow(true);
#endif
			u32 controller = 0;
			F->r(&controller, 4);
			L->snapshot_record=i;L->snapshot_controller=controller;L->snapshot_record_valid=true;
			F->r(&Ldata, sizeof(Flight));
			if (Ldata.type == D3DLIGHT_DIRECTIONAL)
			{
				Fvector tmp_R;
				tmp_R.set(1, 0, 0);

				// directional (base)
				sun_original = L;
				L->set_type(IRender_Light::DIRECT);
				L->set_shadow(true);
				L->set_rotation(Ldata.direction, tmp_R);

				// copy to env-sun
				sun_adapted = L = Create();
				L->snapshot_record=i;L->snapshot_controller=controller;L->snapshot_record_valid=true;
				L->flags.bStatic = true;
				L->set_type(IRender_Light::DIRECT);
				L->set_shadow(true);
				L->set_rotation(Ldata.direction, tmp_R);
			}
			else
			{
				Fvector tmp_D, tmp_R;
				tmp_D.set(0, 0, -1); // forward
				tmp_R.set(1, 0, 0);	 // right

				// point
				v_static.push_back(L);
				L->set_position(Ldata.position);
				L->set_rotation(tmp_D, tmp_R);
				L->set_range(Ldata.range);
				L->set_color(Ldata.diffuse);
				L->set_active(true);
				//				R_ASSERT			(L->spatial.sector	);
			}
		}

		F->close();
	}
	R_ASSERT2(sun_original && sun_adapted, "Where is sun?");

	// fake spot
	/*
	if (0)
	{
		Fvector	P;			P.set(-5.58f,	-0.00f + 2, -3.63f);
		Fvector	D;			D.set(0,-1,0);
		light*	fake		= Create();
		fake->set_type		(IRender_Light::SPOT);
		fake->set_color		(1,1,1);
		fake->set_cone		(deg2rad(60.f));
		fake->set_direction	(D);
		fake->set_position	(P);
		fake->set_range		(3.f);
		fake->set_active	(true);
	}
	*/
}

void CLight_DB::Unload()
{
	v_static.clear();
	sun_original.destroy();
	sun_adapted.destroy();
}

light* CLight_DB::Create()
{
	light* L = xr_new<light>();
	L->flags.bStatic = false;
	L->flags.bActive = false;
	L->flags.bShadow = true;
	return L;
}

#if RENDER == R_R1
void CLight_DB::add_light(light* L)
{
	if (Device.dwFrame == L->frame_render)
		return;
	L->frame_render = Device.dwFrame;
	if (L->flags.bStatic)
		return; // skip static lighting, 'cause they are in lmaps
	if (ps_r1_flags.test(R1FLAG_DLIGHTS))
		RImplementation.L_Dynamic->add(L);
}
#endif

#if RENDER == R_R2
void CLight_DB::add_light(light* L)
{
	kich_r2_light_snapshot::Record(L,"candidate","OBSERVED","CLight_DB::add_light");
	if (Device.dwFrame == L->frame_render)
	{kich_r2_light_snapshot::Record(L,"duplicate_visit","REJECTED","ALREADY_PROCESSED_THIS_FRAME");return;}
	L->frame_render = Device.dwFrame;
	if (RImplementation.o.noshadows)
		L->flags.bShadow = FALSE;
	if(g_render_snapshot_active){RenderSnapshot_U64("LIGHT_POLICY","R2FLAG_R1LIGHTS_requested",ps_r2_ls_flags.test(R2FLAG_R1LIGHTS)?1:0);RenderSnapshot_U64("LIGHT_POLICY","R2FLAG_R1LIGHTS_applied",ps_r2_ls_flags.test(R2FLAG_R1LIGHTS)?1:0);RenderSnapshot_U64("LIGHT_POLICY","noshadows_applied",RImplementation.o.noshadows?1:0);}
	if (L->flags.bStatic && !ps_r2_ls_flags.test(R2FLAG_R1LIGHTS))
	{kich_r2_light_snapshot::Record(L,"policy","REJECTED","STATIC_LIGHT_DISABLED_R2FLAG_R1LIGHTS");return;}
	kich_r2_light_snapshot::Record(L,"policy","ACCEPTED",RImplementation.o.noshadows?"SHADOWS_DISABLED_OPTION":"CURRENT_R2_POLICY");
	L->_export(package);
}
#endif

void CLight_DB::Update()
{
	if(g_render_snapshot_active){
		RenderSnapshot_U64("LIGHT_POLICY","level_registered_count",v_static.size());
		if(RenderSnapshot_Detailed())for(u32 n=0;n<v_static.size();++n)kich_r2_light_snapshot::Record((light*)v_static[n]._get(),"registered","OBSERVED","LEVEL_LIGHT_DATABASE");
	}

	// set sun params
	if (sun_original && sun_adapted)
	{
		light* _sun_original = (light*)sun_original._get();
		light* _sun_adapted = (light*)sun_adapted._get();
		CEnvDescriptor& E = g_pGamePersistent->Environment().CurrentEnv;
		VERIFY(_valid(E.sun_dir));
#ifdef DEBUG
		if (E.sun_dir.y >= 0)
		{
			Log("sect_name", E.sect_name.c_str());
			Log("E.sun_dir", E.sun_dir);
			Log("E.wind_direction", E.wind_direction);
			Log("E.wind_velocity", E.wind_velocity);
			Log("E.sun_color", E.sun_color);
			Log("E.rain_color", E.rain_color);
			Log("E.rain_density", E.rain_density);
			Log("E.fog_distance", E.fog_distance);
			Log("E.fog_density", E.fog_density);
			Log("E.fog_color", E.fog_color);
			Log("E.far_plane", E.far_plane);
			Log("E.sky_rotation", E.sky_rotation);
			Log("E.sky_color", E.sky_color);
		}
#endif
		VERIFY2(E.sun_dir.y < 0, "Invalid sun direction settings in evironment-config");
		Fvector OD, OP, AD, AP;
		OD.set(E.sun_dir).normalize();
		OP.mad(Device.vCameraPosition, OD, -500.f);
		// KICH_CELESTIAL_SYNC: the shadow light must match the visible disc.
		AD.set(OD);

		// for some reason E.sun_dir can point-up
		int counter = 0;
		while (AD.magnitude() < 0.001 && counter < 10)
		{
			AD.add(E.sun_dir);
			counter++;
		}
		AD.normalize();
		AP.mad(Device.vCameraPosition, AD, -500.f);
		sun_original->set_rotation(OD, _sun_original->right);
		sun_original->set_position(OP);
		sun_original->set_color(E.sun_color.x, E.sun_color.y, E.sun_color.z);
		sun_original->set_range(600.f);
		sun_adapted->set_rotation(AD, _sun_adapted->right);
		sun_adapted->set_position(AP);
		sun_adapted->set_color(E.sun_color.x * ps_r2_sun_lumscale, E.sun_color.y * ps_r2_sun_lumscale,
							   E.sun_color.z * ps_r2_sun_lumscale);
		sun_adapted->set_range(600.f);
		if(RenderSnapshot_Detailed()){char source[64],adapted[64],direction[128];
			kich_r2_light_snapshot::Record(_sun_original,"environment","UPDATED","CURRENT_ENVIRONMENT_COLOR_DIRECTION");
			kich_r2_light_snapshot::Record(_sun_adapted,"environment","UPDATED","CURRENT_ENVIRONMENT_COLOR_TIMES_SUN_LUMSCALE");
			kich_r2_light_snapshot::NodeID(source,sizeof(source),_sun_original);kich_r2_light_snapshot::NodeID(adapted,sizeof(adapted),_sun_adapted);
			RenderSnapshot_Record("LIGHT",adapted,"environment_unscaled_light",source);kich_r2_light_snapshot::Float(adapted,"environment_color_scale",ps_r2_sun_lumscale);
			snprintf(direction,sizeof(direction),"%.9g,%.9g,%.9g",E.sun_dir.x,E.sun_dir.y,E.sun_dir.z);RenderSnapshot_Record("LIGHT",adapted,"environment_sun_direction",direction);
			RenderSnapshot_Record("LIGHT",adapted,"visible_disc_alignment","SHARED_ENVIRONMENT_DIRECTION; DISC_DRAW_NOT_MEASURED_HERE");
		}
	}

	// Clear selection
	package.clear();
}
