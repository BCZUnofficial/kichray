// Included once by r2_wet_surfaces.cpp. Do not add a Compile item to vcproj.
#include "..\xr_object.h"
#include "r2_wet_object_state.h"

bool kich_wet_geometry_pass = false;

namespace kich_objects
{
    static int enabled = 1;
    static float fill_time = 18.75f;
    static float dry_time = 180.f;
    static float gloss = (0.35f * 4.f / 3.f);
    static float dark = 0.16f;
    static int npc_drips = 1;
    static float drip_strength = 0.16f;
    static float drip_speed = 0.08f; // vertical metres per second
    static u32 stats_frame = u32(-1), dynamic_draws = 0, drip_draws = 0;
    static u32 roof_probes = 0, stencil_frame = u32(-1);

    static CObject* root_owner(CObject* object)
    {
        CObject* root = object;
        for (int i=0; root && root->H_Parent() && i<16; ++i) root=root->H_Parent();
        return root;
    }
    static void log_status();
    class CCC_Status : public IConsole_Command
    {
    public:
        CCC_Status() : IConsole_Command("r2_wet_shader_status") { bEmptyArgsHandled=true; }
        virtual void Execute(LPCSTR args) { log_status(); }
        virtual void Save(IWriter*) {}
    };

    static void register_commands()
    {
        static bool registered = false;
        if (registered || !Console) return;
        static kich_wet::CCC_WetInteger c0("r2_wet_objects", &enabled, 0, 1);
        static kich_wet::CCC_WetFloat c1("r2_wet_objects_fill_time", &fill_time, 1.f, 600.f);
        static kich_wet::CCC_WetFloat c2("r2_wet_objects_dry_time", &dry_time, 1.f, 1800.f);
        static kich_wet::CCC_WetFloat c3("r2_wet_objects_gloss", &gloss, 0.f, 1.f);
        static kich_wet::CCC_WetFloat c4("r2_wet_objects_dark", &dark, 0.f, 0.5f);
        static kich_wet::CCC_WetInteger c5("r2_wet_npc_drips", &npc_drips, 0, 1);
        static kich_wet::CCC_WetFloat c6("r2_wet_npc_drips_strength", &drip_strength, 0.f, 0.25f);
        static kich_wet::CCC_WetFloat c7("r2_wet_npc_drips_speed", &drip_speed, 0.01f, 0.12f);
        static CCC_Status status;
        Console->AddCommand(&c0);
        Console->AddCommand(&c1);
        Console->AddCommand(&c2);
        Console->AddCommand(&c3);
        Console->AddCommand(&c4);
        Console->AddCommand(&c5);
        Console->AddCommand(&c6);
        Console->AddCommand(&c7);
        Console->AddCommand(&status);
        registered = true;
    }

    static float update(IRenderable* O)
    {
        if (!O || !g_pGameLevel || !g_pGamePersistent) return 0.f;
        CObject* object = dynamic_cast<CObject*>(O);
        CObject* carrier = root_owner(object);
        CObject* player = g_pGameLevel->CurrentEntity();
        // All player equipment uses the actor's state, not the item's timer.
        // Mirror it into the item so detachment starts at the same wetness.
        if (player && object != player && carrier == player)
        {
            const float shared = update(player);
            IRender_ObjectSpecific* item_ros = O->renderable_ROS();
            if (item_ros)
            {
                CROS_impl& item = *static_cast<CROS_impl*>(item_ros);
                item.kich_wet_level = shared;
                item.kich_wet_frame = Device.dwFrame;
                item.kich_wet_time = Device.dwTimeGlobal;
                item.kich_wet_probe = u32(-1); // recheck roof immediately after dropping
            }
            return shared;
        }
        IRender_ObjectSpecific* ros = O->renderable_ROS();
        if (!ros) return 0.f;
        CROS_impl& state = *static_cast<CROS_impl*>(ros);
        if (state.kich_wet_frame == Device.dwFrame) return state.kich_wet_level;
        const bool first = state.kich_wet_frame == u32(-1);
        state.kich_wet_frame = Device.dwFrame;
        const u32 now = Device.dwTimeGlobal;
        const float elapsed = float(now - state.kich_wet_time) * 0.001f;
        state.kich_wet_time = now;
        if (Device.Paused()) return state.kich_wet_level;

        const float rain = clampr(g_pGamePersistent->Environment().CurrentEnv.rain_density, 0.f, 1.f);
        // Inventory/attached items use the carrier's position for rain exposure,
        // but retain their own wetness, including after being dropped.
        IRenderable* probe = carrier ? static_cast<IRenderable*>(carrier) : O;

        if (rain > 0.001f && enabled &&
            (first || state.kich_wet_probe == u32(-1) || now - state.kich_wet_probe >= 250))
        {
            state.kich_wet_probe = now;
            ++roof_probes;
            Fvector position = probe->renderable.xform.c;
            if (probe->renderable.visual)
                probe->renderable.xform.transform_tiny(position, probe->renderable.visual->vis.sphere.P);
            position.y += 0.05f;
            Fvector up; up.set(0.f, 1.f, 0.f);
            // Same static-roof policy as the existing vertical rain shadow map.
            state.kich_wet_exposed = !g_pGameLevel->ObjectSpace.RayTest(
                position, up, 1000.f, collide::rqtStatic, 0, carrier);
        }
        if (rain <= 0.001f || !enabled) state.kich_wet_exposed = false;

        // Do not invent rain exposure for a long interval outside rendering.
        // Drying during globally dry weather can safely use the whole interval.
        const float dt = first ? 0.f : ((rain <= 0.001f) ? elapsed : _min(elapsed, 0.5f));
        state.kich_wet_level = kich_step_object_wetness(state.kich_wet_level,
            dt, rain, enabled && state.kich_wet_exposed, fill_time, dry_time);
        return state.kich_wet_level;
    }

    static void log_status()
    {
        Msg("* [WET STATUS V3.13] BEGIN frame=%u time_ms=%u paused=%d",Device.dwFrame,Device.dwTimeGlobal,int(Device.Paused()));
        Msg("* [WET STATUS] CPU configuration and submitted passes; not GPU pixel readback.");
        Msg("* [WET STATUS] world_frame=%u status=%s material_draws=%u normal_draws=%u resources=%d hw_pcf=%d",
            kich_wet::diagnostic_frame,kich_wet::render_status,kich_wet::material_draws,kich_wet::normal_draws,
            int(kich_wet::resources_ready),int(RImplementation.o.HW_smap_PCF));
        Msg("* [WET STATUS] rain=%.4f world_enabled=%d far=%.2f dark=%.3f gloss=%.3f rain_smap=%u",
            g_pGamePersistent ? g_pGamePersistent->Environment().CurrentEnv.rain_density : 0.f,
            kich_wet::enabled,kich_wet::far_distance,kich_wet::darkening,kich_wet::gloss_add,kich_wet::rain_smap_size);
        Msg("* [WET STATUS] micro_normal=%.3f frequency=%.3f speed=%.3f roof_boost=%.3f",
            kich_wet::normal_strength,kich_wet::normal_frequency,kich_wet::normal_speed,kich_wet::roof_boost);
        Msg("* [WET STATUS] rings=%.3f speed=%.3f sharpness=%.3f flow=%.3f speed=%.3f sharpness=%.3f bubbles=%.3f speed=%.3f",
            kich_wet::ripple_visibility,kich_wet::ripple_speed,kich_wet::ripple_sharpness,
            kich_wet::flow_visibility,kich_wet::flow_speed,kich_wet::flow_sharpness,kich_wet::bubble_visibility,kich_wet::bubble_speed);
        Msg("* [WET STATUS] puddles=%d level=%.5f amount=%.3f fill_s=%.2f dry_s=%.2f gloss=%.3f dark=%.3f flatten=%.3f scale=%.3f",
            kich_wet::puddles_enabled,kich_wet::puddle_level,kich_wet::puddle_amount,kich_wet::puddle_fill_time,kich_wet::puddle_dry_time,
            kich_wet::puddle_gloss,kich_wet::puddle_dark,kich_wet::puddle_flatten,kich_wet::puddle_scale);
        Msg("* [WET STATUS] expected_shader=v3.13-walls-bokeh/v3.5-mask world_stencil=1 objects=3 drainage=off near=1.2..1.8 far=18..26 normal_fade=12..20 gloss_fade=11..19");
        Msg("* [WET STATUS] objects=%d fill_s=%.2f dry_s=%.2f gloss=%.3f dark=%.3f stats_frame=%u draws=%u probes=%u stencil_restore_frame=%u",
            enabled,fill_time,dry_time,gloss,dark,stats_frame,dynamic_draws,roof_probes,stencil_frame);
        Msg("* [WET STATUS] npc_drips=%d strength=%.3f speed_m_s=%.3f start_wet=0.35 full_wet=0.85 eligible_draws=%u gravity=world_down shape=beads cycle_s=1.333 active_s=0.933 diameter_mm=5.2x7.8 density_scale=2 motion=continuous",
            npc_drips,drip_strength,drip_speed,drip_draws);
        Msg("* [WET WALLS V3.13] enabled=%d level=%.4f strength=%.3f width_m=%.4f..%.4f speed_m_s=%.4f..%.4f lifetime_s=%.2f draws=%u gravity=world_down exposure=rain_shadowmap",
            kich_wet::walls_enabled,kich_wet::wall_level,kich_wet::wall_strength,
            _min(kich_wet::wall_size_min,kich_wet::wall_size_max),_max(kich_wet::wall_size_min,kich_wet::wall_size_max),
            _min(kich_wet::wall_speed_min,kich_wet::wall_speed_max),_max(kich_wet::wall_speed_min,kich_wet::wall_speed_max),
            kich_wet::wall_lifetime,kich_wet::wall_draws);
        kich_lens::log_status();
        if (!g_pGameLevel) { Msg("* [WET STATUS] no level; END"); return; }
        CObject* player=g_pGameLevel->CurrentEntity();
        Msg("* [WET STATUS] player_id=%u shared_equipment=1 camera=(%.3f,%.3f,%.3f)",player ? u32(player->ID()) : u32(-1),
            Device.vCameraPosition.x,Device.vCameraPosition.y,Device.vCameraPosition.z);
        const u32 count=g_pGameLevel->Objects.o_count();
        u32 tracked=0,wet_count=0;
        for (u32 i=0;i<count;++i)
        {
            CObject* O=g_pGameLevel->Objects.o_get_by_iterator(i);
            if (!O || O->getDestroy() || !O->renderable.pROS) continue;
            CROS_impl& s=*static_cast<CROS_impl*>(O->renderable.pROS);
            if (s.kich_wet_frame==u32(-1)) continue;
            ++tracked; if (s.kich_wet_level>0.001f) ++wet_count;
            const bool shared=player && O!=player && root_owner(O)==player;
            const bool live=O!=player && O->renderable_WetDrips();
            CROS_impl* exposure=&s;
            if (shared && player->renderable.pROS) exposure=static_cast<CROS_impl*>(player->renderable.pROS);
            Msg("* [WET OBJECT] id=%u name=%s owner=%u shared_player=%d wet=%.5f rain_exposed=%d probe_ms=%u state_frame=%u live_npc=%d drip_eligible=%d pos=(%.2f,%.2f,%.2f)",
                u32(O->ID()),O->cName().c_str(),O->H_Parent()?u32(O->H_Parent()->ID()):u32(-1),int(shared),s.kich_wet_level,
                int(exposure->kich_wet_exposed),exposure->kich_wet_probe,s.kich_wet_frame,int(live),
                int(enabled && npc_drips && live && s.kich_wet_level>0.35f),O->renderable.xform.c.x,O->renderable.xform.c.y,O->renderable.xform.c.z);
        }
        Msg("* [WET STATUS] tracked=%u wet=%u online=%u END",tracked,wet_count,count);
        FlushLog();
    }
}

void kich_wet_object_apply(IRenderable* O)
{
    kich_objects::register_commands();
    const float level = kich_objects::update(O);
    RCache.set_c("kich_object_wet", kich_objects::enabled ? level : 0.f,
        kich_objects::gloss, kich_objects::dark, 0.f);
    const bool living_npc = O && O->renderable_WetDrips() && g_pGameLevel &&
        O != static_cast<IRenderable*>(g_pGameLevel->CurrentEntity());
    const bool drips = living_npc && kich_objects::enabled && kich_objects::npc_drips;
    RCache.set_c("kich_npc_drips", drips ? kich_objects::drip_strength : 0.f,
        float(Device.dwTimeGlobal) * 0.001f, kich_objects::drip_speed, 0.f);
    if (drips)
    {
        Fvector up, x, z, origin, axis;
        axis.set(0,1,0); Device.mView.transform_dir(up,axis);
        axis.set(1,0,0); Device.mView.transform_dir(x,axis);
        axis.set(0,0,1); Device.mView.transform_dir(z,axis);
        Device.mView.transform_tiny(origin, O->renderable.xform.c);
        RCache.set_c("kich_drip_up", up.x,up.y,up.z,0);
        RCache.set_c("kich_drip_x", x.x,x.y,x.z,0);
        RCache.set_c("kich_drip_z", z.x,z.y,z.z,0);
        RCache.set_c("kich_drip_origin", origin.x,origin.y,origin.z,0);
    }
    if (kich_wet_geometry_pass)
    {
        if (O) ++kich_objects::dynamic_draws;
        if (drips && level > 0.35f) ++kich_objects::drip_draws;
        // Bit 0 remains the usual geometry bit. Bit 1 is temporary and is
        // cleared before lights run; no extra G-buffer channel is required.
        RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, O ? 0x03 : 0x01, 0xff, 0xff,
            D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
    }
}

void kich_wet_objects_tick()
{
    static u32 frame = u32(-1);
    if (frame == Device.dwFrame || !g_pGameLevel) return;
    frame = Device.dwFrame;
    kich_objects::stats_frame = frame;
    kich_objects::dynamic_draws = kich_objects::drip_draws = kich_objects::roof_probes = 0;
    kich_objects::register_commands();
    CObject* player = g_pGameLevel->CurrentEntity();
    if (player) kich_objects::update(player);
    // Iterate both active and sleeping online objects, independent of camera.
    // Stored inventory objects are excluded; an actually drawn HUD item is
    // updated by apply_object using its carrier's position.
    const u32 count = g_pGameLevel->Objects.o_count();
    for (u32 i = 0; i < count; ++i)
    {
        CObject* O = g_pGameLevel->Objects.o_get_by_iterator(i);
        if (!O || O->getDestroy() || !O->renderable.visual) continue;
        if (O->H_Parent() && kich_objects::root_owner(O) != player) continue;
        kich_objects::update(O);
    }
}

void kich_wet_objects_finish()
{
    kich_objects::stencil_frame = Device.dwFrame;
    // Always execute, even with no rain / puddles or unsupported rain PCF.
    // Only bits above bit 0 are cleared; sky stencil stays zero.
    static ref_shader shader;
    static ref_geom geometry;
    static bool ready = false;
    if (!ready)
    {
        shader.create("kich_wet_objects_stencil");
        geometry.create(FVF::F_TL, RCache.Vertex.Buffer(), RCache.QuadIB);
        ready = true;
    }
    CRenderTarget* target = RImplementation.Target;
    target->u_setrt(target->rt_Normal, 0, 0, HW.pBaseZB);
    RImplementation.rmNormal();
    RCache.set_Element(shader->E[0]);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_ColorWriteEnable(FALSE);
    RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x01, 0x01, 0xfe,
        D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
    u32 offset;
    const float w = float(Device.dwWidth), h = float(Device.dwHeight);
    const u32 color = color_rgba(255,255,255,255);
    FVF::TL* v = (FVF::TL*)RCache.Vertex.Lock(4, geometry->vb_stride, offset);
    v->set(EPS,h+EPS,EPS_S,1.f,color,0.f,1.f); ++v;
    v->set(EPS,EPS,EPS_S,1.f,color,0.f,0.f); ++v;
    v->set(w+EPS,h+EPS,EPS_S,1.f,color,1.f,1.f); ++v;
    v->set(w+EPS,EPS,EPS_S,1.f,color,1.f,0.f);
    RCache.Vertex.Unlock(4,geometry->vb_stride);
    RCache.set_Geometry(geometry);
    RCache.Render(D3DPT_TRIANGLELIST,offset,0,4,0,2);
    RCache.set_ColorWriteEnable();
    RCache.set_Stencil(TRUE,D3DCMP_LESSEQUAL,0x01,0xff,0x00);
}
