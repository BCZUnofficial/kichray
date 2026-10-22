#include "stdafx.h"
#include "../RenderSnapshot.h"

static void snapshot_smap_spot(light* L, bool transparent)
{
	if (!g_render_snapshot_active)
		return;
	RenderSnapshot_Pass(transparent ? "LOCAL_SHADOW_TRANSPARENT" : "LOCAL_SHADOW");
	char light_id[64], phase_id[128], description[384];
	// Each OMNIPART has its own live light; the descriptor records its cube-face direction.
	// Neither the pointer nor the atlas placement is a stable cross-run resource identity.
	sprintf_s(light_id, "r2.light@%p", static_cast<void*>(L));
	sprintf_s(phase_id, "%s_%s/atlas=%u,%u,%u/smap=%u",
		IRender_Light::OMNIPART == L->flags.type ? "OMNIPART" : "SPOT",
		transparent ? "TRANSPARENT" : "OPAQUE", L->X.S.posX, L->X.S.posY, L->X.S.size,
		u32(L->vis.smap_ID));
	RenderSnapshot_Light(light_id, "PROCESS_HANDLE");
	RenderSnapshot_Phase(phase_id);
	sprintf_s(description, "type=%u; position=%g,%g,%g; direction=%g,%g,%g; range=%g; cone=%g; static=%u; shadow=%u",
		u32(L->flags.type), L->position.x, L->position.y, L->position.z,
		L->direction.x, L->direction.y, L->direction.z, L->range, L->cone,
		u32(L->flags.bStatic), u32(L->flags.bShadow));
	RenderSnapshot_Text("LIGHT_DESCRIPTOR", light_id, description);
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
