#include "stdafx.h"
#include "KichR4Capture.h"
#include "KichR4Native.h"
#include "KichR4CaptureInput.h"
#include <stdio.h>
#if !defined(_EDITOR) && !defined(DEDICATED_SERVER)
#include "SkeletonCustom.h"
namespace {
using kich_r4_capture::Style;
bool CheckEngine(bool value,const char* why){if(!value)KichR4_CaptureFail(why);return value;}
struct Metadata {
    bool valid;Metadata():valid(true){}
    uint32_t Get(D3DRENDERSTATETYPE key){DWORD value=0;if(FAILED(HW.pDevice->GetRenderState(key,&value)))valid=false;return value;}
};
bool Snapshot(bool ui,Style& s){if(KichR4_NativeActive())return KichR4_NativeStyle(s);Metadata m;
    s.blend=m.Get(D3DRS_ALPHABLENDENABLE)!=0;s.src=m.Get(D3DRS_SRCBLEND);s.dst=m.Get(D3DRS_DESTBLEND);
    if(!CheckEngine(s.src>=1 && s.src<=10 && s.dst>=1 && s.dst<=10,"unsupported blend factors"))return false;
    if(!CheckEngine(!s.blend || m.Get(D3DRS_BLENDOP)==D3DBLENDOP_ADD,"unsupported blend operation"))return false;
    s.alpha_func=m.Get(D3DRS_ALPHATESTENABLE)?m.Get(D3DRS_ALPHAFUNC):8;s.alpha_ref=float(m.Get(D3DRS_ALPHAREF)&255)/255.f;
    const uint32_t cull=m.Get(D3DRS_CULLMODE);s.cull=cull==D3DCULL_CW?KICH_R4_CULL_FRONT:cull==D3DCULL_CCW?KICH_R4_CULL_BACK:KICH_R4_CULL_NONE;
    /* The new bridge has a single base-color model pass and its own depth. */
    if(!ui){s.blend=0;s.depth=1;s.write=1;}else{s.depth=0;s.write=0;s.cull=KICH_R4_CULL_NONE;s.clamp=1;}
    D3DVIEWPORT9 v={};if(!CheckEngine(SUCCEEDED(HW.pDevice->GetViewport(&v)),"DX9 viewport snapshot"))return false;
    if(!CheckEngine(v.X==0 && v.Y==0 && v.Width==Device.dwWidth && v.Height==Device.dwHeight,"offscreen capture viewport"))return false;
    s.min_z=v.MinZ;s.max_z=v.MaxZ;
    if(ui && m.Get(D3DRS_SCISSORTESTENABLE)){RECT r={};if(!CheckEngine(SUCCEEDED(HW.pDevice->GetScissorRect(&r)),"DX9 scissor snapshot"))return false;
        s.scissor=true;s.rect[0]=r.left>0?r.left:0;s.rect[1]=r.top>0?r.top:0;
        if(s.rect[0]>static_cast<LONG>(Device.dwWidth))s.rect[0]=Device.dwWidth;
        if(s.rect[1]>static_cast<LONG>(Device.dwHeight))s.rect[1]=Device.dwHeight;
        s.rect[2]=r.right<static_cast<LONG>(Device.dwWidth)?r.right:static_cast<LONG>(Device.dwWidth);
        s.rect[3]=r.bottom<static_cast<LONG>(Device.dwHeight)?r.bottom:static_cast<LONG>(Device.dwHeight);
        if(s.rect[2]<s.rect[0]){s.rect[2]=s.rect[0];}if(s.rect[3]<s.rect[1]){s.rect[3]=s.rect[1];}}
    return CheckEngine(m.valid,"DX9 render state snapshot");
}
}
#endif
void KichR4_CaptureModel(void* visual,void* parent,uint32_t vertices,uint32_t first,uint32_t triangles)
{
    KichR4_CaptureModel(visual,parent,vertices,first,triangles,KICH_R4_SKIN_UNKNOWN);
}
void KichR4_CaptureModel(void* visual,void* parent,uint32_t vertices,uint32_t first,uint32_t triangles,uint32_t skin_mode)
{
#if !defined(_EDITOR) && !defined(DEDICATED_SERVER)
    uint32_t count=0;if(!KichR4_CaptureActive() || !KichR4_CaptureModelNeeded(visual,count))return;
    if(memcmp(RCache.get_xform_view().m,Device.mView.m,sizeof(Fmatrix)))return;
    kich_r4_capture::ModelInput input={};input.visual=visual;input.count=vertices;input.first=first;input.triangles=triangles;
    input.skin_mode=skin_mode;
    if(!Snapshot(false,input.style))return;
    if(input.style.max_z>=.1f && memcmp(RCache.get_xform_project().m,Device.mProject.m,sizeof(Fmatrix)))return;
    memcpy(input.world,RCache.get_xform_world().m,sizeof(input.world));Fmatrix vp;vp.mul(RCache.get_xform_project(),RCache.get_xform_view());memcpy(input.vp,vp.m,sizeof(input.vp));
    float bones[256][16];
    if(count){CKinematics* skeleton=static_cast<CKinematics*>(parent);
        const uint32_t actual=skeleton?skeleton->LL_BoneCount():0;
        if(!skeleton || count>256 || count>actual){char detail[160];snprintf(detail,sizeof(detail),"skeleton bone count mismatch: required=%u actual=%u",count,actual);
            KichR4_CaptureModelFail(visual,detail);return;}
        for(uint32_t i=0;i<count;++i)memcpy(bones[i],skeleton->LL_GetTransform_R(static_cast<u16>(i)).m,sizeof(bones[i]));
        input.bones=&bones[0][0];input.bone_count=count;}
    KichR4_CaptureSubmitModel(input);
#else
    (void)visual;(void)parent;(void)vertices;(void)first;(void)triangles;(void)skin_mode;
#endif
}
void KichR4_CaptureUIDraw(void* declaration,void* vb,void* ib,uint32_t stride,uint32_t topology,uint32_t base,
    uint32_t start,uint32_t vertex_count,uint32_t first,uint32_t primitives,bool indexed)
{
#if !defined(_EDITOR) && !defined(DEDICATED_SERVER)
    if(!KichR4_CaptureUIActive() || !primitives)return;
    if(!CheckEngine(declaration!=NULL,"HUD vertex declaration missing"))return;
    D3DVERTEXELEMENT9 elements[65]={};UINT n=65;
    if(!CheckEngine(SUCCEEDED(static_cast<IDirect3DVertexDeclaration9*>(declaration)->GetDeclaration(elements,&n)),"HUD vertex declaration snapshot"))return;
    if(!CheckEngine(n && n<=65,"HUD vertex declaration size"))return;
    kich_r4_capture::UIInput input={};input.vb=vb;input.ib=ib;input.stride=stride;input.topology=topology;input.base=base;
    input.start=start;input.vertex_count=vertex_count;input.first=first;input.primitives=primitives;input.indexed=indexed;
    input.position=input.color=input.uv=UINT32_MAX;
    for(UINT i=0;i<n && elements[i].Stream!=255;++i){const D3DVERTEXELEMENT9& e=elements[i];
        if(!CheckEngine(!e.Stream && !e.Method,"HUD vertex stream/method"))return;
        if(!e.UsageIndex && e.Usage==D3DDECLUSAGE_POSITIONT){if(!CheckEngine(e.Type==D3DDECLTYPE_FLOAT4,"HUD position format"))return;input.position=e.Offset;}
        else if(!e.UsageIndex && e.Usage==D3DDECLUSAGE_COLOR){if(!CheckEngine(e.Type==D3DDECLTYPE_D3DCOLOR,"HUD color format"))return;input.color=e.Offset;}
        else if(!e.UsageIndex && e.Usage==D3DDECLUSAGE_TEXCOORD){if(!CheckEngine(e.Type==D3DDECLTYPE_FLOAT2,"HUD UV format"))return;input.uv=e.Offset;}}
    if(!Snapshot(true,input.style))return;
    if(input.uv!=UINT32_MAX){CTexture* texture=RCache.get_ActiveTexture(0);if(texture){
        if(!CheckEngine(!texture->isUser() && !texture->pAVI && !texture->pTheora && texture->seqDATA.empty(),"HUD runtime/video/sequence texture requires legacy output"))return;
        input.texture=texture->cName.c_str();}}
    KichR4_CaptureSubmitUI(input);
#else
    (void)declaration;(void)vb;(void)ib;(void)stride;(void)topology;(void)base;(void)start;(void)vertex_count;(void)first;(void)primitives;(void)indexed;
#endif
}
