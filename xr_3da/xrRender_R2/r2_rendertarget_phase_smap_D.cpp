#include "stdafx.h"
#include "../RenderSnapshot.h"

extern int ps_r2_sun_shadow_cull;

static void snapshot_smap_direct(light* L, u32 sub_phase, bool transparent)
{
	if (!g_render_snapshot_active)
		return;
	RenderSnapshot_Pass(transparent ? "SUN_SHADOW_TRANSPARENT" : "SUN_SHADOW");
	char light_id[64], phase_id[96], description[384];
	// This address identifies the live engine light only; it is not a persistent asset ID.
	sprintf_s(light_id, "r2.light@%p", static_cast<void*>(L));
	sprintf_s(phase_id, "SUN_%s_%s/subphase=%u",
		SE_SUN_NEAR == sub_phase ? "NEAR" : (SE_SUN_FAR == sub_phase ? "FAR" : "UNKNOWN"),
		transparent ? "TRANSPARENT" : "OPAQUE", sub_phase);
	RenderSnapshot_Light(light_id, "PROCESS_HANDLE");
	RenderSnapshot_Phase(phase_id);
	sprintf_s(description, "type=%u; position=%g,%g,%g; direction=%g,%g,%g; range=%g; cone=%g; static=%u; shadow=%u",
		u32(L->flags.type), L->position.x, L->position.y, L->position.z,
		L->direction.x, L->direction.y, L->direction.z, L->range, L->cone,
		u32(L->flags.bStatic), u32(L->flags.bShadow));
	RenderSnapshot_Text("LIGHT_DESCRIPTOR", light_id, description);
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
