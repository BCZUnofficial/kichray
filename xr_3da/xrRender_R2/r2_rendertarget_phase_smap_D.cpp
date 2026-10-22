#include "stdafx.h"
#include "../RenderSnapshot.h"
#include "../xrRender/R2LightSnapshot.h"

extern int ps_r2_sun_shadow_cull;

static void snapshot_smap_direct(light* L, u32 sub_phase, bool transparent)
{
	if (!RenderSnapshot_Detailed())
		return;
	RenderSnapshot_Pass(transparent ? "SUN_SHADOW_TRANSPARENT" : "SUN_SHADOW");
	char light_id[64],phase_id[96],viewport[96];
	const char* cascade=SE_SUN_NEAR==sub_phase?"SUN_NEAR":"SUN_FAR";
	kich_r2_light_snapshot::SourceID(light_id,sizeof(light_id),L);
	snprintf(phase_id,sizeof(phase_id),"%s_%s/subphase=%u",cascade,transparent?"TRANSPARENT":"OPAQUE",sub_phase);
	RenderSnapshot_Light(light_id,"RENDERER_PROCESS_LIFETIME_SERIAL");RenderSnapshot_Phase(phase_id);
	kich_r2_light_snapshot::Record(L,"shadow_pass","SELECTED",phase_id);
	kich_r2_light_snapshot::Shadow(L,cascade,&L->X.D.combine._11);
	snprintf(viewport,sizeof(viewport),"0,0,%u,%u;z=0,1",u32(RImplementation.o.smapsize),u32(RImplementation.o.smapsize));
	kich_r2_light_snapshot::ShadowText(L,cascade,"target_extent",viewport);
	kich_r2_light_snapshot::ShadowText(L,cascade,"viewport","SEE_ACTUAL_DRAW_STATE; TARGET_EXTENT_IS_NOT_VIEWPORT_PROOF");
	kich_r2_light_snapshot::ShadowText(L,cascade,"format","SEE_DRAW_TARGET_RESOURCE; NOT_INFERRED_FROM_ROLE");
	kich_r2_light_snapshot::ShadowText(L,cascade,"candidate_source","render_sun/near caster volume; category evidence in draw groups");
	kich_r2_light_snapshot::ShadowText(L,cascade,"slope_bias_filtering","SEE_ACTUAL_DRAW_STATES_AND_SAMPLER");
	kich_r2_light_snapshot::ShadowText(L,cascade,"intersection_tests_and_visible_shadow_pixels","NOT_MEASURED");
}

void CRenderTarget::phase_smap_direct(light* L, u32 sub_phase)
{
	snapshot_smap_direct(L, sub_phase, false);
	// Targets
	if (RImplementation.o.HW_smap)
		u_setrt(rt_smap_surf, NULL, NULL, rt_smap_depth->pRT);
	else
		u_setrt(rt_smap_surf, NULL, NULL, rt_smap_ZB);

    // KICH_SUN_FRESH_SMAP_V6: the volume pass samples the map over the
    // ray segment, not just surface receivers.
    const bool volumeActive = ps_r2_sun_shafts > 0 &&
        ps_r2_sun_shafts_intensity >= 0.0001f && RImplementation.o.fp16_blend;
	if (SE_SUN_NEAR == sub_phase && !volumeActive)
	{
		// optimized clear
		D3DRECT R;
		R.x1 = L->X.D.minX;
		R.x2 = L->X.D.maxX;
		R.y1 = L->X.D.minY;
		R.y2 = L->X.D.maxY;
		CHK_DX(HW.pDevice->Clear(1L, &R, D3DCLEAR_ZBUFFER, 0xFFFFFFFF, 1.0f, 0L));
	}
	else
	{
		// full-clear
		CHK_DX(HW.pDevice->Clear(0L, NULL, D3DCLEAR_ZBUFFER, 0xFFFFFFFF, 1.0f, 0L));
	}

	// Stencil	- disable
	RCache.set_Stencil(FALSE);

    // KICH_SUN_CULL_DIAG_V7: change only caster face selection.
    u32 shadowCull = (sub_phase == SE_SUN_NEAR || !RImplementation.o.HW_smap)
        ? CULL_CCW : CULL_CW;
    if (ps_r2_sun_shadow_cull == 1)
        shadowCull = CULL_NONE;
    else if (ps_r2_sun_shadow_cull == 2)
        shadowCull = CULL_CCW;
    RCache.set_CullMode(shadowCull);
    if(RenderSnapshot_Detailed()){const char* cascade=SE_SUN_NEAR==sub_phase?"SUN_NEAR":"SUN_FAR";char rectangle[96];
        snprintf(rectangle,sizeof(rectangle),"%d,%d,%d,%d",L->X.D.minX,L->X.D.minY,L->X.D.maxX,L->X.D.maxY);
        kich_r2_light_snapshot::ShadowFloat(L,cascade,"caster_cull_applied",float(shadowCull));
        kich_r2_light_snapshot::ShadowText(L,cascade,"depth_clear_region",SE_SUN_NEAR==sub_phase&&!volumeActive?rectangle:"FULL_TARGET");
        kich_r2_light_snapshot::ShadowText(L,cascade,"stencil_producer","DISABLED_FOR_SHADOW_CASTER_PASS; LIGHT_ACCUMULATION_WRITES_ITS_OWN_MASK");
        kich_r2_light_snapshot::ShadowText(L,cascade,"sunfilter_selected",RImplementation.o.sunfilter?"1":"0");
    }

    // A marker for each cascade when the selected mode changes.
    static int loggedMode[2] = {-1, -1};
    const int cascade = (sub_phase == SE_SUN_NEAR) ? 0 : 1;
    if (loggedMode[cascade] != ps_r2_sun_shadow_cull)
    {
        loggedMode[cascade] = ps_r2_sun_shadow_cull;
        Msg("* [KICH SUN V7] cascade=%s mode=%d cull=%u hw=%d",
            cascade == 0 ? "near" : "far", ps_r2_sun_shadow_cull,
            shadowCull, int(RImplementation.o.HW_smap));
    }
	if (RImplementation.o.HW_smap)
		RCache.set_ColorWriteEnable(FALSE);
	else
		RCache.set_ColorWriteEnable();
}

void CRenderTarget::phase_smap_direct_tsh(light* L, u32 sub_phase)
{
	snapshot_smap_direct(L, sub_phase, true);
	VERIFY(RImplementation.o.Tshadows);
	u32 _clr = 0xffffffff; // color_rgba(127,127,12,12);
	RCache.set_ColorWriteEnable();
	CHK_DX(HW.pDevice->Clear(0L, NULL, D3DCLEAR_TARGET, _clr, 1.0f, 0L));
}

// Kept out of the vcproj intentionally: it is compiled as part of this
// translation unit, so the old VS project file does not need to be edited.
#include "r2_wet_surfaces.cpp"
