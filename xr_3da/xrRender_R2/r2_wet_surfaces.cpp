// KICH_WET_V3_5
// Clear Sky-style dynamic wet surfaces adapted to SoC DX9/R2.
// V3.0: V2.5 water response + accumulating/drying procedural puddles.
// This file is intentionally included by r2_rendertarget_phase_smap_D.cpp.
// Do NOT add it as a separate Compile item to xrRender_R2.vcproj.

#include "..\igame_persistent.h"
#include "..\environment.h"
#include "..\xr_ioconsole.h"
#include "..\xr_ioc_cmd.h"
#include "..\fbasicvisual.h"

namespace kich_wet
{
	static u32 diagnostic_frame = u32(-1);
	static u32 material_draws = 0, normal_draws = 0;
	static LPCSTR render_status = "not_called";
	static const u32 rain_smap_size = 512;
	static const LPCSTR rain_smap_surf_name = "$user$kich_rain_smap_surf";
	static const LPCSTR rain_smap_depth_name = "$user$kich_rain_smap_depth";

	static int enabled = 1;
    static int walls_enabled=1;
    static float wall_level=0.f, wall_strength=0.22f;
    static float wall_size_min=0.0055f, wall_size_max=0.0205f;
    static float wall_speed_min=0.01375f, wall_speed_max=0.203125f;
    static float wall_lifetime=3.6f;
    static u32 wall_draws=0, wall_frame=u32(-1);
    static const void* wall_level_owner=0;
    static ref_shader wall_shader;
	static float far_distance = 50.f;

	// Wider V2.2 ranges, with slightly wetter defaults.
	static float gloss_add = 1.25f;
	static float darkening = 0.22f;

	// Base wet micro-normal.
	static float normal_strength = 0.10f;
	static float normal_frequency = 13.0f;
	static float normal_speed = 3.0f;

	// More aggressive roof response.
	static float roof_boost = 3.0f;

	// Water animation controls.
	static float ripple_visibility = 0.40f;
	static float ripple_speed = 1.25f;
	static float flow_visibility = 0.18f;
	static float flow_speed = 3.0f;
	static float ripple_sharpness = 0.50f;
	static float flow_sharpness = 0.70f;
	static float bubble_visibility = 0.11f;
	static float bubble_speed = 0.30f;

	// V3 puddles.
	static int puddles_enabled = 1;
	static float puddle_level = 0.f;
	static float puddle_fill_time = 45.f;
	static float puddle_dry_time = 240.f;
	static float puddle_amount = 1.0f;
	static float puddle_gloss = 0.90f;
	static float puddle_dark = 0.10f;
	static float puddle_flatten = 0.82f;
	static float puddle_scale = 1.0f;
	static float puddle_rain_threshold = 0.05f;

	static float animation_time = 0.f;

	static bool commands_registered = false;
	static bool resources_ready = false;

	static ref_rt rain_smap_surf;
	static ref_rt rain_smap_depth;
	static ref_shader wet_dark_shader;
	static ref_shader wet_gloss_shader;
	static ref_shader wet_normal_shader;
	static ref_shader wet_normal_apply_shader;
	static ref_geom wet_geom;

	class CCC_WetInteger : public CCC_Integer
	{
	  public:
		CCC_WetInteger(LPCSTR N, int* V, int _min, int _max) : CCC_Integer(N, V, _min, _max) {}
		virtual void Save(IWriter* F) {}
	};

	class CCC_WetFloat : public CCC_Float
	{
	  public:
		CCC_WetFloat(LPCSTR N, float* V, float _min, float _max) : CCC_Float(N, V, _min, _max) {}
		virtual void Save(IWriter* F) {}
	};

	static void register_commands()
	{
		if (commands_registered || !Console)
			return;

		static CCC_WetInteger cmd_enable("r2_wet_surfaces", &enabled, 0, 1);
		static CCC_WetFloat cmd_far("r2_wet_surfaces_far", &far_distance, 20.f, 120.f);

		// V2.2: expanded material ranges.
		static CCC_WetFloat cmd_gloss("r2_wet_surfaces_gloss", &gloss_add, 0.f, 1.25f);
		static CCC_WetFloat cmd_dark("r2_wet_surfaces_dark", &darkening, 0.f, 0.80f);

		static CCC_WetFloat cmd_normal(
			"r2_wet_surfaces_normal", &normal_strength, 0.f, 0.35f);
		static CCC_WetFloat cmd_normal_scale(
			"r2_wet_surfaces_normal_scale", &normal_frequency, 1.f, 24.f);
		static CCC_WetFloat cmd_normal_speed(
			"r2_wet_surfaces_normal_speed", &normal_speed, 0.f, 3.f);

		// V2.2: roof boost can now be pushed much harder.
		static CCC_WetFloat cmd_roof_boost(
			"r2_wet_surfaces_roof_boost", &roof_boost, 1.f, 3.f);

		// Flat-surface drop ripples.
		static CCC_WetFloat cmd_ripple(
			"r2_wet_surfaces_ripple", &ripple_visibility, 0.f, 0.40f);
		static CCC_WetFloat cmd_ripple_speed(
			"r2_wet_surfaces_ripple_speed", &ripple_speed, 0.f, 16.f);

		// Roof/runoff flow.
		static CCC_WetFloat cmd_flow(
			"r2_wet_surfaces_flow", &flow_visibility, 0.f, 0.40f);
		static CCC_WetFloat cmd_flow_speed(
			"r2_wet_surfaces_flow_speed", &flow_speed, 0.f, 8.f);
		static CCC_WetFloat cmd_ripple_sharpness(
			"r2_wet_surfaces_ripple_sharpness", &ripple_sharpness, 0.50f, 3.00f);
		static CCC_WetFloat cmd_flow_sharpness(
			"r2_wet_surfaces_flow_sharpness", &flow_sharpness, 0.50f, 3.00f);
		static CCC_WetFloat cmd_bubble(
			"r2_wet_surfaces_bubble", &bubble_visibility, 0.f, 0.40f);
		static CCC_WetFloat cmd_bubble_speed(
			"r2_wet_surfaces_bubble_speed", &bubble_speed, 0.f, 6.f);

		static CCC_WetInteger cmd_puddles(
			"r2_puddles", &puddles_enabled, 0, 1);
		static CCC_WetFloat cmd_puddle_level(
			"r2_puddles_level", &puddle_level, 0.f, 1.f);
		static CCC_WetFloat cmd_puddle_fill(
			"r2_puddles_fill_time", &puddle_fill_time, 5.f, 600.f);
		static CCC_WetFloat cmd_puddle_dry(
			"r2_puddles_dry_time", &puddle_dry_time, 10.f, 1800.f);
		static CCC_WetFloat cmd_puddle_amount(
			"r2_puddles_amount", &puddle_amount, 0.f, 2.f);
		static CCC_WetFloat cmd_puddle_gloss(
			"r2_puddles_gloss", &puddle_gloss, 0.f, 1.5f);
		static CCC_WetFloat cmd_puddle_dark(
			"r2_puddles_dark", &puddle_dark, 0.f, 0.5f);
		static CCC_WetFloat cmd_puddle_flatten(
			"r2_puddles_flatten", &puddle_flatten, 0.f, 1.f);
		static CCC_WetFloat cmd_puddle_scale(
			"r2_puddles_scale", &puddle_scale, 0.25f, 4.f);

		static CCC_WetInteger cw0("r2_wet_walls",&walls_enabled,0,1);
        static CCC_WetFloat cw1("r2_wet_walls_strength",&wall_strength,0.f,0.35f);
        static CCC_WetFloat cw2("r2_wet_walls_size_min",&wall_size_min,0.004f,0.03f);
        static CCC_WetFloat cw3("r2_wet_walls_size_max",&wall_size_max,0.004f,0.03f);
        static CCC_WetFloat cw4("r2_wet_walls_speed_min",&wall_speed_min,0.01f,0.22f);
        static CCC_WetFloat cw5("r2_wet_walls_speed_max",&wall_speed_max,0.01f,0.22f);
        static CCC_WetFloat cw6("r2_wet_walls_lifetime",&wall_lifetime,1.5f,4.f);
        Console->AddCommand(&cw0); Console->AddCommand(&cw1);
        Console->AddCommand(&cw2); Console->AddCommand(&cw3);
        Console->AddCommand(&cw4); Console->AddCommand(&cw5); Console->AddCommand(&cw6);
        Console->AddCommand(&cmd_enable);
		Console->AddCommand(&cmd_far);
		Console->AddCommand(&cmd_gloss);
		Console->AddCommand(&cmd_dark);
		Console->AddCommand(&cmd_normal);
		Console->AddCommand(&cmd_normal_scale);
		Console->AddCommand(&cmd_normal_speed);
		Console->AddCommand(&cmd_roof_boost);
		Console->AddCommand(&cmd_ripple);
		Console->AddCommand(&cmd_ripple_speed);
		Console->AddCommand(&cmd_flow);
		Console->AddCommand(&cmd_flow_speed);
		Console->AddCommand(&cmd_ripple_sharpness);
		Console->AddCommand(&cmd_flow_sharpness);
		Console->AddCommand(&cmd_bubble);
		Console->AddCommand(&cmd_bubble_speed);
		Console->AddCommand(&cmd_puddles);
		Console->AddCommand(&cmd_puddle_level);
		Console->AddCommand(&cmd_puddle_fill);
		Console->AddCommand(&cmd_puddle_dry);
		Console->AddCommand(&cmd_puddle_amount);
		Console->AddCommand(&cmd_puddle_gloss);
		Console->AddCommand(&cmd_puddle_dark);
		Console->AddCommand(&cmd_puddle_flatten);
		Console->AddCommand(&cmd_puddle_scale);

		commands_registered = true;
		Msg("* [KICH WET V3.0] console commands registered");
	}

	static bool init_resources()
	{
		if (resources_ready)
			return true;

		if (!RImplementation.o.HW_smap_PCF)
			return false;

		D3DFORMAT depth_format = (D3DFORMAT)RImplementation.o.HW_smap_FORMAT;
		D3DFORMAT color_format = D3DFMT_R5G6B5;
		if (RImplementation.o.nullrt)
			color_format = (D3DFORMAT)MAKEFOURCC('N', 'U', 'L', 'L');

		rain_smap_depth.create(rain_smap_depth_name, rain_smap_size, rain_smap_size, depth_format);
		rain_smap_surf.create(rain_smap_surf_name, rain_smap_size, rain_smap_size, color_format);

		wall_shader.create("kich_wet_walls");
		wet_dark_shader.create("kich_wet_surfaces_dark");
		wet_gloss_shader.create("kich_wet_surfaces_gloss");
		wet_normal_shader.create("kich_wet_surfaces_normal");
		wet_normal_apply_shader.create("kich_wet_surfaces_normal_apply");
		wet_geom.create(FVF::F_TL, RCache.Vertex.Buffer(), RCache.QuadIB);

		resources_ready = true;
		Msg("* [KICH WET V3.0] resources initialized: rain smap %ux%u", rain_smap_size, rain_smap_size);
		return true;
	}

	static CSector* find_outdoor_sector()
	{
		CSector* largest_sector = 0;
		float largest_volume = 0.f;

		for (u32 s = 0; s < RImplementation.Sectors.size(); ++s)
		{
			CSector* sector = (CSector*)RImplementation.Sectors[s];
			IRender_Visual* visual = sector->root();
			if (!visual)
				continue;

			const float volume = visual->vis.box.getvolume();
			if (volume > largest_volume)
			{
				largest_volume = volume;
				largest_sector = sector;
			}
		}

		return largest_sector;
	}

	static bool build_rain_xform(Fmatrix& rain_xform, Fvector& rain_cop)
	{
		CSector* outdoor = find_outdoor_sector();
		if (!outdoor)
			return false;

		const float rain_far = clampr(far_distance, 20.f, 120.f);
		const float extent = rain_far * 1.40f;
		const float source_height = 1000.f;

		Fvector center;
		center.mad(Device.vCameraPosition, Device.vCameraDirection, rain_far * 0.30f);

		const float world_texel = (extent * 2.f) / float(rain_smap_size);
		center.x = floorf(center.x / world_texel + .5f) * world_texel;
		center.z = floorf(center.z / world_texel + .5f) * world_texel;

		Fvector rain_pos, rain_dir, rain_up;
		rain_pos.set(center.x, Device.vCameraPosition.y + source_height, center.z);
		rain_dir.set(0.f, -1.f, 0.f);
		rain_up.set(0.f, 0.f, 1.f);

		Fmatrix view, project;
		view.build_camera_dir(rain_pos, rain_dir, rain_up);
		D3DXMatrixOrthoOffCenterLH((D3DXMATRIX*)&project,
			-extent, extent, -extent, extent, 0.1f, source_height * 2.f);
		rain_xform.mul(project, view);
		rain_cop.set(rain_pos);
		return true;
	}

	static bool render_rain_shadowmap(const Fmatrix& rain_xform, const Fvector& rain_cop)
	{
		CSector* outdoor = find_outdoor_sector();
		if (!outdoor)
			return false;

		CRenderTarget* target = RImplementation.Target;

		target->u_setrt(rain_smap_surf, NULL, NULL, rain_smap_depth->pRT);

		D3DVIEWPORT9 vp = {0, 0, rain_smap_size, rain_smap_size, 0.f, 1.f};
		CHK_DX(HW.pDevice->SetViewport(&vp));
		CHK_DX(HW.pDevice->Clear(0L, NULL, D3DCLEAR_ZBUFFER, 0xFFFFFFFF, 1.0f, 0L));

		RCache.set_Stencil(FALSE);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_ColorWriteEnable(FALSE);

		CFrustum cull_frustum;
		Fmatrix rain_xform_copy = rain_xform;
		cull_frustum.CreateFromMatrix(rain_xform_copy, FRUSTUM_P_ALL);

		const u32 old_phase = RImplementation.phase;
		RImplementation.HOM.Disable();
		RImplementation.phase = CRender::PHASE_SMAP;
		RImplementation.r_pmask(true, false);

		Fvector cop = rain_cop;
		RImplementation.r_dsgraph_render_subspace(outdoor, &cull_frustum, rain_xform_copy, cop, FALSE);

		RCache.set_xform_world(Fidentity);
		RCache.set_xform_view(Fidentity);
		RCache.set_xform_project(rain_xform_copy);
		RImplementation.r_dsgraph_render_graph(0);

		RImplementation.r_pmask(true, false);
		RImplementation.phase = old_phase;

		RCache.set_xform_world(Fidentity);
		RCache.set_xform_view(Device.mView);
		RCache.set_xform_project(Device.mProject);

		return true;
	}

	static void set_wet_constants(const Fmatrix& rain_xform, float rain_factor)
	{
		const float texel_offset = .5f / float(rain_smap_size);

		Fmatrix texel_adjust = {
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f + texel_offset, 0.5f + texel_offset, -0.00035f, 1.0f
		};

		Fmatrix inv_view;
		inv_view.invert(Device.mView);

		Fmatrix projected;
		projected.mul(texel_adjust, rain_xform);

		Fmatrix shadow;
		shadow.mul(projected, inv_view);

		Fvector world_up, view_up;
		world_up.set(0.f, 1.f, 0.f);
		Device.mView.transform_dir(view_up, world_up);
		view_up.normalize();

		Fvector world_x, world_z, view_x, view_z;
		world_x.set(1.f, 0.f, 0.f);
		world_z.set(0.f, 0.f, 1.f);
		Device.mView.transform_dir(view_x, world_x);
		Device.mView.transform_dir(view_z, world_z);
		view_x.normalize();
		view_z.normalize();

		RCache.set_c("m_wet_shadow", shadow);
		RCache.set_c("wet_wall",enabled && walls_enabled ? wall_level : 0.f,wall_strength,wall_lifetime,0.f);
        RCache.set_c("wet_wall_limits",_min(wall_size_min,wall_size_max),_max(wall_size_min,wall_size_max),
            _min(wall_speed_min,wall_speed_max),_max(wall_speed_min,wall_speed_max));
        RCache.set_c("wet_params", rain_factor, darkening, gloss_add, far_distance);
		RCache.set_c("wet_up_view", view_up.x, view_up.y, view_up.z, 0.f);
		RCache.set_c("wet_smap_texel",
			1.f / float(rain_smap_size), 1.f / float(rain_smap_size), 0.f, 0.f);
		RCache.set_c("wet_motion",
			animation_time, normal_frequency, normal_strength, roof_boost);
		RCache.set_c("wet_water",
			ripple_visibility, ripple_speed, flow_visibility, flow_speed);
		RCache.set_c("wet_timing",
			normal_speed, ripple_sharpness, flow_sharpness, 0.f);
		RCache.set_c("wet_extra",
			bubble_visibility, bubble_speed, 0.f, 0.f);
		RCache.set_c("wet_puddle",
			puddle_level, puddle_amount, puddle_gloss, puddle_dark);
		RCache.set_c("wet_puddle2",
			puddle_flatten, puddle_scale, float(puddles_enabled), 0.f);
		RCache.set_c("wet_world_x_view", view_x.x, view_x.y, view_x.z, 0.f);
		RCache.set_c("wet_world_z_view", view_z.x, view_z.y, view_z.z, 0.f);
		RCache.set_c("wet_camera_world",
			Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, 0.f);
	}

	static void build_fullscreen_quad(u32& offset)
	{
		const float w = float(Device.dwWidth);
		const float h = float(Device.dwHeight);
		Fvector2 p0, p1;
		p0.set(.5f / w, .5f / h);
		p1.set((w + .5f) / w, (h + .5f) / h);

		const u32 color = color_rgba(255, 255, 255, 255);
		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, wet_geom->vb_stride, offset);
		pv->set(EPS, h + EPS, EPS_S, 1.f, color, p0.x, p1.y); ++pv;
		pv->set(EPS, EPS, EPS_S, 1.f, color, p0.x, p0.y); ++pv;
		pv->set(w + EPS, h + EPS, EPS_S, 1.f, color, p1.x, p1.y); ++pv;
		pv->set(w + EPS, EPS, EPS_S, 1.f, color, p1.x, p0.y);
		RCache.Vertex.Unlock(4, wet_geom->vb_stride);
		RCache.set_Geometry(wet_geom);
	}

	static void draw_wet_normal(const Fmatrix& rain_xform, float rain_factor)
	{
		const bool any_normal_effect =
			normal_strength > EPS_L ||
			ripple_visibility > EPS_L ||
			flow_visibility > EPS_L ||
			bubble_visibility > EPS_L ||
			(puddles_enabled && puddle_level > EPS_L && puddle_flatten > EPS_L);

		if (!any_normal_effect)
			return;
		normal_draws = 2;

		CRenderTarget* target = RImplementation.Target;
		u32 offset;
		build_fullscreen_quad(offset);

		// Temporary patched normal -> accumulator.
		target->u_setrt(target->rt_Accumulator, 0, 0, HW.pBaseZB);
		RImplementation.rmNormal();
		RCache.set_Element(wet_normal_shader->E[0]);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x01, 0xff, 0x00);
		RCache.set_ColorWriteEnable();
		set_wet_constants(rain_xform, rain_factor);
		RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);

		// Patched normal -> actual normal buffer.
		target->u_setrt(target->rt_Normal, 0, 0, HW.pBaseZB);
		RImplementation.rmNormal();
		RCache.set_Element(wet_normal_apply_shader->E[0]);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x01, 0xff, 0x00);
		RCache.set_ColorWriteEnable();
		RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
	}

    static void draw_wet_walls(const Fmatrix& rain_xform,float rain_factor)
    {
        if (!enabled || !walls_enabled || wall_level<=0.001f || wall_strength<=0.f) return;
        CRenderTarget* target=RImplementation.Target;
        u32 offset; build_fullscreen_quad(offset);
        target->u_setrt(target->rt_Accumulator,0,0,HW.pBaseZB);
        RImplementation.rmNormal();
        RCache.set_Element(wall_shader->E[0]);
        RCache.set_CullMode(CULL_NONE);
        RCache.set_Stencil(TRUE,D3DCMP_EQUAL,0x01,0xff,0x00);
        RCache.set_ColorWriteEnable();
        set_wet_constants(rain_xform,rain_factor);
        RCache.Render(D3DPT_TRIANGLELIST,offset,0,4,0,2);
        target->u_setrt(target->rt_Normal,0,0,HW.pBaseZB);
        RCache.set_Element(wet_normal_apply_shader->E[0]);
        RCache.set_CullMode(CULL_NONE);
        RCache.set_Stencil(TRUE,D3DCMP_EQUAL,0x01,0xff,0x00);
        RCache.set_ColorWriteEnable();
        RCache.Render(D3DPT_TRIANGLELIST,offset,0,4,0,2);
        wall_draws=2;
    }

	static void draw_wet_material(const Fmatrix& rain_xform, float rain_factor)
	{
		material_draws = 2;
		CRenderTarget* target = RImplementation.Target;

		target->u_setrt(target->rt_Color, 0, 0, HW.pBaseZB);
		RImplementation.rmNormal();

		u32 offset;
		build_fullscreen_quad(offset);

		RCache.set_Element(wet_dark_shader->E[0]);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x01, 0xff, 0x00);
		RCache.set_ColorWriteEnable();
		set_wet_constants(rain_xform, rain_factor);
		RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);

		RCache.set_Element(wet_gloss_shader->E[0]);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x01, 0xff, 0x00);
		RCache.set_ColorWriteEnable();
		set_wet_constants(rain_xform, rain_factor);
		RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
	}

	static void update_puddles(float rain_factor)
	{
		if (!puddles_enabled)
		{
			puddle_level = 0.f;
			return;
		}

		// Clamp dt so a hitch / save-load spike cannot instantly fill or dry puddles.
		const float dt = clampr(Device.fTimeDelta, 0.f, 0.25f);
		const float rain = clampr(rain_factor, 0.f, 1.f);

		if (rain > puddle_rain_threshold)
		{
			const float fill = _max(puddle_fill_time, 1.f);
			const float drive = (rain - puddle_rain_threshold) /
				_max(1.f - puddle_rain_threshold, 0.001f);
			puddle_level += dt * drive / fill;
		}
		else
		{
			const float dry = _max(puddle_dry_time, 1.f);
			puddle_level -= dt / dry;
		}

		clamp(puddle_level, 0.f, 1.f);
	}
}

void kich_wet_surfaces_render()
{
	kich_wet::diagnostic_frame = Device.dwFrame;
	kich_wet::material_draws = kich_wet::normal_draws = kich_wet::wall_draws = 0;
	kich_wet::render_status = "no_environment";
	kich_wet::register_commands();

	if (!g_pGamePersistent)
		return;

	const float rain_factor = g_pGamePersistent->Environment().CurrentEnv.rain_density;
	kich_wet::update_puddles(rain_factor);
    if (kich_wet::wall_level_owner!=g_pGameLevel)
    { kich_wet::wall_level=0.f; kich_wet::wall_level_owner=g_pGameLevel; }
    if (kich_wet::wall_frame!=Device.dwFrame)
    {
        kich_wet::wall_frame=Device.dwFrame;
        const float dt=Device.Paused() ? 0.f : _min(Device.fTimeDelta,0.5f);
        const float rain=clampr(rain_factor,0.f,1.f);
        kich_wet::wall_level=clampr(kich_wet::wall_level+dt*
            ((kich_wet::enabled && kich_wet::walls_enabled && rain>0.001f) ? rain/(12.5f/1.5f) : -1.f/120.f),0.f,1.f);
    }

	const float effective_rain = kich_wet::enabled ? rain_factor : 0.f;
	const bool rain_active = effective_rain > EPS_L;
	const bool puddles_active =
		kich_wet::puddles_enabled && kich_wet::puddle_level > 0.001f;

	// No rain and no stored water: nothing to render.
	kich_wet::render_status = "dry_no_world_passes";
	if (!rain_active && !puddles_active && !(kich_wet::enabled && kich_wet::walls_enabled && kich_wet::wall_level>0.001f))
		return;

	kich_wet::render_status = "rain_resources_unavailable_or_no_hw_pcf";
	if (!kich_wet::init_resources())
		return;

	kich_wet::animation_time += Device.fTimeDelta;
	if (kich_wet::animation_time > 10000.f)
		kich_wet::animation_time -= 10000.f;

	Fmatrix rain_xform;
	Fvector rain_cop;
	kich_wet::render_status = "no_outdoor_sector_or_rain_transform";
	if (!kich_wet::build_rain_xform(rain_xform, rain_cop))
		return;

	// Keep the exposure map alive while puddles are drying.
	// This prevents persisted water from appearing beneath roofs/interiors.
	kich_wet::render_status = "rain_shadowmap_not_rendered";
	if (!kich_wet::render_rain_shadowmap(rain_xform, rain_cop))
		return;

	// V3.5: every mask reads the original normal. The material passes only
	// modify rt_Color; patch rt_Normal after both have finished.
	kich_wet::draw_wet_material(rain_xform, effective_rain);
	kich_wet::draw_wet_normal(rain_xform, effective_rain);
    kich_wet::draw_wet_walls(rain_xform,effective_rain);
	kich_wet::render_status = "world_passes_submitted";
}

#include "r2_wet_lens.h"
#include "r2_wet_objects.h"
