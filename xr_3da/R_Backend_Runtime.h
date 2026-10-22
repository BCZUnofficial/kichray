#ifndef R_BACKEND_RUNTIMEH
#define R_BACKEND_RUNTIMEH
#pragma once

#include "sh_texture.h"
#include "sh_matrix.h"
#include "sh_constant.h"
#include "sh_rt.h"
#include "KichR4Capture.h"
#include "KichR4Native.h"
#include "RenderSnapshot.h"

IC void R_xforms::set_c_w(R_constant* C)
{
	c_w = C;
	RCache.set_c(C, m_w);
};
IC void R_xforms::set_c_v(R_constant* C)
{
	c_v = C;
	RCache.set_c(C, m_v);
};
IC void R_xforms::set_c_p(R_constant* C)
{
	c_p = C;
	RCache.set_c(C, m_p);
};
IC void R_xforms::set_c_wv(R_constant* C)
{
	c_wv = C;
	RCache.set_c(C, m_wv);
};
IC void R_xforms::set_c_vp(R_constant* C)
{
	c_vp = C;
	RCache.set_c(C, m_vp);
};
IC void R_xforms::set_c_wvp(R_constant* C)
{
	c_wvp = C;
	RCache.set_c(C, m_wvp);
};

IC void CBackend::set_xform(u32 ID, const Fmatrix& M)
{
	stat.xforms++;
	if(KichR4_NativeActive())return;
	CHK_DX(HW.pDevice->SetTransform((D3DTRANSFORMSTATETYPE)ID, (D3DMATRIX*)&M));
}
IC void CBackend::set_xform_world(const Fmatrix& M)
{
	xforms.set_W(M);
}
IC void CBackend::set_xform_view(const Fmatrix& M)
{
	xforms.set_V(M);
}
IC void CBackend::set_xform_project(const Fmatrix& M)
{
	xforms.set_P(M);
}
IC const Fmatrix& CBackend::get_xform_world()
{
	return xforms.get_W();
}
IC const Fmatrix& CBackend::get_xform_view()
{
	return xforms.get_V();
}
IC const Fmatrix& CBackend::get_xform_project()
{
	return xforms.get_P();
}

IC void CBackend::set_RT(IDirect3DSurface9* RT, u32 ID)
{
	if(KichR4_NativeActive())return;
	if (RT != pRT[ID])
	{
		PGO(Msg("PGO:setRT"));
		stat.target_rt++;
		pRT[ID] = RT;
		CHK_DX(HW.pDevice->SetRenderTarget(ID, RT));
	}
}

IC void CBackend::set_ZB(IDirect3DSurface9* ZB)
{
	if(KichR4_NativeActive())return;
	if (ZB != pZB)
	{
		PGO(Msg("PGO:setZB"));
		stat.target_zb++;
		pZB = ZB;
		CHK_DX(HW.pDevice->SetDepthStencilSurface(ZB));
	}
}

ICF void CBackend::set_States(IDirect3DStateBlock9* _state)
{
	if (state != _state)
	{
		PGO(Msg("PGO:state_block"));
#ifdef DEBUG
		stat.states++;
#endif
		state = _state;
		state->Apply();
	}
}

#ifdef _EDITOR
IC void CBackend::set_Matrices(SMatrixList* _M)
{
	if (M != _M)
	{
		M = _M;
		if (M)
		{
			for (u32 it = 0; it < M->size(); it++)
			{
				CMatrix* mat = &*((*M)[it]);
				if (mat && matrices[it] != mat)
				{
					matrices[it] = mat;
					mat->Calculate();
					set_xform(D3DTS_TEXTURE0 + it, mat->xform);
					stat.matrices++;
				}
			}
		}
	}
}
#endif

IC void CBackend::set_Constants(R_constant_table* C)
{
	// caching
	if (ctable == C)
		return;
	ctable = C;
	xforms.unmap();
	if (0 == C)
		return;

	PGO(Msg("PGO:c-table"));

	// process constant-loaders
	R_constant_table::c_table::iterator it = C->table.begin();
	R_constant_table::c_table::iterator end = C->table.end();
	for (; it != end; it++)
	{
		R_constant* C = &**it;
		if (C->handler)
			C->handler->setup(C);
	}
}

IC void CBackend::set_Element(ShaderElement* S, u32 pass)
{
	if(!S || pass>=S->passes.size())return;
	SPass& P = *(S->passes[pass]);
	if(g_render_snapshot_active)RenderSnapshot_Shaders(P.vs?P.vs->cName.c_str():"NONE",P.ps?P.ps->cName.c_str():"NONE");
	set_States(P.state);
	set_PS(P.ps);
	set_VS(P.vs);
	set_Constants(P.constants);
	set_Textures(P.T);
	if(KichR4_NativeActive())KichR4_NativeElement(S);
#ifdef _EDITOR
	set_Matrices(P.M);
#endif
}

ICF void CBackend::set_Format(IDirect3DVertexDeclaration9* _decl)
{
	if (decl != _decl)
	{
		PGO(Msg("PGO:v_format:%x", _decl));
#ifdef DEBUG
		stat.decl++;
#endif
		decl = _decl;
		if(!KichR4_NativeActive())CHK_DX(HW.pDevice->SetVertexDeclaration(decl));
	}
}

ICF void CBackend::set_PS(IDirect3DPixelShader9* _ps, LPCSTR _n)
{
	if(g_render_snapshot_active)RenderSnapshot_ShaderStage(false,_n);
	if (ps != _ps)
	{
		PGO(Msg("PGO:Pshader:%x", _ps));
		stat.ps++;
		ps = _ps;
		if(!KichR4_NativeActive())CHK_DX(HW.pDevice->SetPixelShader(ps));
#ifdef DEBUG
		ps_name = _n;
#endif
	}
}

ICF void CBackend::set_VS(IDirect3DVertexShader9* _vs, LPCSTR _n)
{
	if(g_render_snapshot_active)RenderSnapshot_ShaderStage(true,_n);
	if (vs != _vs)
	{
		PGO(Msg("PGO:Vshader:%x", _vs));
		stat.vs++;
		vs = _vs;
		if(!KichR4_NativeActive())CHK_DX(HW.pDevice->SetVertexShader(vs));
#ifdef DEBUG
		vs_name = _n;
#endif
	}
}

ICF void CBackend::set_Vertices(IDirect3DVertexBuffer9* _vb, u32 _vb_stride)
{
	if ((vb != _vb) || (vb_stride != _vb_stride))
	{
		PGO(Msg("PGO:VB:%x,%d", _vb, _vb_stride));
#ifdef DEBUG
		stat.vb++;
#endif
		vb = _vb;
		vb_stride = _vb_stride;
		if(!KichR4_NativeActive())CHK_DX(HW.pDevice->SetStreamSource(0, vb, 0, vb_stride));
	}
}

ICF void CBackend::set_Indices(IDirect3DIndexBuffer9* _ib)
{
	if (ib != _ib)
	{
		PGO(Msg("PGO:IB:%x", _ib));
#ifdef DEBUG
		stat.ib++;
#endif
		ib = _ib;
		if(!KichR4_NativeActive())CHK_DX(HW.pDevice->SetIndices(ib));
	}
}

ICF void CBackend::Render(D3DPRIMITIVETYPE T, u32 baseV, u32 startV, u32 countV, u32 startI, u32 PC)
{
	if(KichR4_NativeActive()){stat.calls++;stat.verts+=countV;stat.polys+=PC;KichR4_NativeDraw(decl,vb,ib,vb_stride,T,baseV,startV,countV,startI,PC,true);return;}
#ifndef _EDITOR
	if (KichR4_CaptureActive())
		KichR4_CaptureUIDraw(decl, vb, ib, vb_stride, T, baseV, startV, countV, startI, PC, true);
#endif
	stat.calls++;
	stat.verts += countV;
	stat.polys += PC;
	constants.flush();
	const HRESULT draw_result=HW.pDevice->DrawIndexedPrimitive(T, baseV, startV, countV, startI, PC);
	if(g_render_snapshot_active)RenderSnapshot_D3D9Draw(vb,ib,vb_stride,T,countV,startI,PC,true,draw_result);
	CHK_DX(draw_result);
	PGO(Msg("PGO:DIP:%dv/%df", countV, PC));
}

ICF void CBackend::Render(D3DPRIMITIVETYPE T, u32 startV, u32 PC)
{
	if(KichR4_NativeActive()){stat.calls++;stat.polys+=PC;KichR4_NativeDraw(decl,vb,NULL,vb_stride,T,0,startV,0,0,PC,false);return;}
#ifndef _EDITOR
	if (KichR4_CaptureActive())
		KichR4_CaptureUIDraw(decl, vb, NULL, vb_stride, T, 0, startV, 0, 0, PC, false);
#endif
	stat.calls++;
	stat.verts += 3 * PC;
	stat.polys += PC;
	constants.flush();
	const HRESULT draw_result=HW.pDevice->DrawPrimitive(T, startV, PC);
	if(g_render_snapshot_active)RenderSnapshot_D3D9Draw(vb,NULL,vb_stride,T,0,0,PC,false,draw_result);
	CHK_DX(draw_result);
	PGO(Msg("PGO:DIP:%dv/%df", 3 * PC, PC));
}

ICF void CBackend::set_Shader(Shader* S, u32 pass)
{
	set_Element(S->E[0], pass);
}

IC void CBackend::set_Geometry(SGeometry* _geom)
{
	set_Format(_geom->dcl._get()->dcl);
	set_Vertices(_geom->vb, _geom->vb_stride);
	set_Indices(_geom->ib);
}

IC void CBackend::set_Scissor(Irect* R)
{
	if(KichR4_NativeActive()){KichR4_NativeScissor(R?reinterpret_cast<int*>(R):NULL);return;}
	if (R)
	{
		CHK_DX(HW.pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE));
		RECT* clip = (RECT*)R;
		CHK_DX(HW.pDevice->SetScissorRect(clip));
	}
	else
	{
		CHK_DX(HW.pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE));
	}
}

IC void CBackend::set_Stencil(u32 _enable, u32 _func, u32 _ref, u32 _mask, u32 _writemask, u32 _fail, u32 _pass,
							  u32 _zfail)
{
	if(KichR4_NativeActive())return;
	// Simple filter
	if (stencil_enable != _enable)
	{
		stencil_enable = _enable;
		CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILENABLE, _enable));
	}
	if (!stencil_enable)
		return;
	if (stencil_func != _func)
	{
		stencil_func = _func;
		CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILFUNC, _func));
	}
	if (stencil_ref != _ref)
	{
		stencil_ref = _ref;
		CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILREF, _ref));
	}
	if (stencil_mask != _mask)
	{
		stencil_mask = _mask;
		CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILMASK, _mask));
	}
	if (stencil_writemask != _writemask)
	{
		stencil_writemask = _writemask;
		CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILWRITEMASK, _writemask));
	}
	if (stencil_fail != _fail)
	{
		stencil_fail = _fail;
		CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILFAIL, _fail));
	}
	if (stencil_pass != _pass)
	{
		stencil_pass = _pass;
		CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILPASS, _pass));
	}
	if (stencil_zfail != _zfail)
	{
		stencil_zfail = _zfail;
		CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILZFAIL, _zfail));
	}
}
IC void CBackend::set_ColorWriteEnable(u32 _mask)
{
	if(KichR4_NativeActive()){colorwrite_mask=_mask;KichR4_NativeSetState(D3DRS_COLORWRITEENABLE,_mask);return;}
	if (colorwrite_mask != _mask)
	{
		colorwrite_mask = _mask;
		CHK_DX(HW.pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, _mask));
		CHK_DX(HW.pDevice->SetRenderState(D3DRS_COLORWRITEENABLE1, _mask));
		CHK_DX(HW.pDevice->SetRenderState(D3DRS_COLORWRITEENABLE2, _mask));
		CHK_DX(HW.pDevice->SetRenderState(D3DRS_COLORWRITEENABLE3, _mask));
	}
}
ICF void CBackend::set_CullMode(u32 _mode)
{
	if(KichR4_NativeActive()){cull_mode=_mode;KichR4_NativeSetState(D3DRS_CULLMODE,_mode);return;}
	if (cull_mode != _mode)
	{
		cull_mode = _mode;
		CHK_DX(HW.pDevice->SetRenderState(D3DRS_CULLMODE, _mode));
	}
}

#endif
