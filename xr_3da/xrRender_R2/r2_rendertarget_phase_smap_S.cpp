#include "stdafx.h"
#include "../RenderSnapshot.h"
#include "../xrRender/R2LightSnapshot.h"

static void snapshot_smap_spot(light* L, bool transparent)
{
	if (!RenderSnapshot_Detailed())
		return;
	RenderSnapshot_Pass(transparent ? "LOCAL_SHADOW_TRANSPARENT" : "LOCAL_SHADOW");
	char light_id[64], phase_id[160], atlas[96], shadow_id[96];
	kich_r2_light_snapshot::SourceID(light_id,sizeof(light_id),L);
	snprintf(phase_id,sizeof(phase_id),"%s_%s/face=%u/atlas=%u,%u,%u/smap=%u",
		IRender_Light::OMNIPART==L->flags.type?"POINT_FACE":"SPOT",transparent?"TRANSPARENT":"OPAQUE",
		L->snapshot_face,L->X.S.posX,L->X.S.posY,L->X.S.size,u32(L->vis.smap_ID));
	RenderSnapshot_Light(light_id,"RENDERER_PROCESS_LIFETIME_SERIAL");RenderSnapshot_Phase(phase_id);
	kich_r2_light_snapshot::Record(L,"shadow_pass","SELECTED",transparent?"TRANSPARENT_CASTER_GRAPH":"OPAQUE_CASTER_GRAPH");
	kich_r2_light_snapshot::Shadow(L,"LOCAL",&L->X.S.combine._11);
	kich_r2_light_snapshot::ShadowID(shadow_id,sizeof(shadow_id),L,"LOCAL");
	kich_r2_light_snapshot::Matrix(shadow_id,"caster_view",&L->X.S.view._11);
	kich_r2_light_snapshot::Matrix(shadow_id,"caster_projection",&L->X.S.project._11);
	snprintf(atlas,sizeof(atlas),"%u,%u,%u,%u;z=0,1",L->X.S.posX,L->X.S.posY,L->X.S.size,L->X.S.size);
	kich_r2_light_snapshot::ShadowText(L,"LOCAL","viewport_atlas",atlas);
	kich_r2_light_snapshot::ShadowFloat(L,"LOCAL","map_size",float(RImplementation.o.smapsize));
	kich_r2_light_snapshot::ShadowText(L,"LOCAL","target_role",RImplementation.o.HW_smap?"HW_SHADOW_DEPTH":"SOFTWARE_SHADOW_COLOR_AND_ZB");
	kich_r2_light_snapshot::ShadowText(L,"LOCAL","format","SEE_DRAW_TARGET_RESOURCE; NOT_INFERRED_FROM_ROLE");
	kich_r2_light_snapshot::ShadowText(L,"LOCAL","slope_bias_filtering","SEE_ACTUAL_DRAW_STATES_AND_SAMPLER");
	kich_r2_light_snapshot::ShadowText(L,"LOCAL","candidate_source","r_dsgraph_render_subspace; sector/light frustum; category evidence in draw groups");
	kich_r2_light_snapshot::ShadowText(L,"LOCAL","receiver_intersections_and_pixels","NOT_MEASURED");
}

void CRenderTarget::phase_smap_spot_clear()
{
	/*
	if (RImplementation.b_HW_smap)		u_setrt	(rt_smap_surf, NULL, NULL, rt_smap_d_depth->pRT);
	else								u_setrt	(rt_smap_surf, NULL, NULL, rt_smap_d_ZB);
	CHK_DX								(HW.pDevice->Clear( 0L, NULL, D3DCLEAR_ZBUFFER,	0xffffffff,	1.0f, 0L));
	*/
}

void CRenderTarget::phase_smap_spot(light* L)
{
	snapshot_smap_spot(L, false);
	// Targets + viewport
	if (RImplementation.o.HW_smap)
		u_setrt(rt_smap_surf, NULL, NULL, rt_smap_depth->pRT);
	else
		u_setrt(rt_smap_surf, NULL, NULL, rt_smap_ZB);
	D3DVIEWPORT9 VP = {L->X.S.posX, L->X.S.posY, L->X.S.size, L->X.S.size, 0, 1};
	CHK_DX(HW.pDevice->SetViewport(&VP));

	// Misc		- draw only front-faces //back-faces
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
// no transparency
#pragma todo("can optimize for multi-lights covering more than say 50%...")
	if (RImplementation.o.HW_smap)
		RCache.set_ColorWriteEnable(FALSE);
	CHK_DX(HW.pDevice->Clear(0L, NULL, D3DCLEAR_ZBUFFER, 0xffffffff, 1.0f, 0L));
}

void CRenderTarget::phase_smap_spot_tsh(light* L)
{
	snapshot_smap_spot(L, true);
	VERIFY(RImplementation.o.Tshadows);
	RCache.set_ColorWriteEnable();
	if (IRender_Light::OMNIPART == L->flags.type)
	{
		// omni-part
		CHK_DX(HW.pDevice->Clear(0L, NULL, D3DCLEAR_TARGET, 0xffffffff, 1.0f, 0L));
	}
	else
	{
		// real-spot
		// Select color-mask
		ref_shader shader = L->s_spot;
		if (!shader)
			shader = s_accum_spot;
		RCache.set_Element(shader->E[SE_L_FILL]);

		// Fill vertex buffer
		Fvector2 p0, p1;
		u32 Offset;
		u32 C = color_rgba(255, 255, 255, 255);
		float _w = float(L->X.S.size);
		float _h = float(L->X.S.size);
		float d_Z = EPS_S;
		float d_W = 1.f;
		p0.set(.5f / _w, .5f / _h);
		p1.set((_w + .5f) / _w, (_h + .5f) / _h);

		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(EPS, float(_h + EPS), d_Z, d_W, C, p0.x, p1.y);
		pv++;
		pv->set(EPS, EPS, d_Z, d_W, C, p0.x, p0.y);
		pv++;
		pv->set(float(_w + EPS), float(_h + EPS), d_Z, d_W, C, p1.x, p1.y);
		pv++;
		pv->set(float(_w + EPS), EPS, d_Z, d_W, C, p1.x, p0.y);
		pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);
		RCache.set_Geometry(g_combine);

		// draw
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	}
}
