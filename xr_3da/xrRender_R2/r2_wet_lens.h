// Included once by r2_wet_surfaces.cpp, never a separate Compile item.
#include "..\xr_object.h"
namespace kich_lens
{
    static int enabled=1;
    static float amount=(1.f/1.3f), strength=0.9f, drive=0.f;
    static float blur=1.f;
    // Birth-density history: retain each cohort until its 3.125 s life ends.
    static float birth[16]={0}, clock_s=0.f;
    static u32 last_time=u32(-1), last_frame=u32(-1);
    static int history_tick=0;
    static const void* history_level=0;
    static void clear_history()
    {
        for (int i=0;i<16;++i) birth[i]=0.f;
        clock_s=0.f; history_tick=0; last_time=u32(-1);
    }
    static LPCSTR status="not_called";
    static u32 draws=0;
    static bool exposed=false, ready=false;
    static u32 probe_time=u32(-1), frame=u32(-1), width=0, height=0;
    static Fvector probe_position;
    static ref_rt temp;
    static ref_shader refract, copy;
    static ref_geom geom;
    static void commands()
    {
        static bool registered=false;
        if (registered || !Console) return;
        static kich_wet::CCC_WetInteger c0("r2_wet_lens",&enabled,0,1);
        static kich_wet::CCC_WetFloat c1("r2_wet_lens_amount",&amount,0.f,2.f);
        static kich_wet::CCC_WetFloat c2("r2_wet_lens_strength",&strength,0.f,2.f);
        static kich_wet::CCC_WetFloat c3("r2_wet_lens_blur",&blur,0.f,2.f);
        Console->AddCommand(&c3);
        Console->AddCommand(&c0); Console->AddCommand(&c1); Console->AddCommand(&c2);
        registered=true;
    }
    static void log_status()
    {
        Msg("* [WET LENS DETAIL] status=%s draws=%u blur=%.3f",status,draws,blur);
        Msg("* [WET LENS HISTORY] clock=%.3f current_birth=%.4f oldest_birth=%.4f lifetime_s=3.125 sampling_s=0.25",clock_s,birth[0],birth[15]);
        Msg("* [WET LENS V3.11] enabled=%d amount=%.3f strength=%.3f drive=%.4f exposed=%d camera_y=%.4f frame=%u ready=%d size=%ux%u",
            enabled,amount,strength,drive,int(exposed),Device.vCameraDirection.y,frame,int(ready),width,height);
    }
}
void kich_wet_lens_render(bool active)
{
    using namespace kich_lens;
    commands();
    frame=Device.dwFrame;
    drive=0.f; draws=0; status="disabled_menu_or_no_level";
    if (!active || !enabled || !g_pGameLevel || !g_pGamePersistent || amount<=0.f || strength<=0.f)
    { exposed=false; probe_time=u32(-1); clear_history(); return; }
    const u32 now=Device.dwTimeGlobal;
    if (history_level!=g_pGameLevel || (last_frame!=u32(-1) && Device.dwFrame-last_frame>1))
        clear_history();
    history_level=g_pGameLevel; last_frame=Device.dwFrame;
    const float dt=(last_time==u32(-1) || Device.Paused()) ? 0.f : float(now-last_time)*0.001f;
    last_time=now;
    clock_s+=dt;
    const float rain=clampr(g_pGamePersistent->Environment().CurrentEnv.rain_density,0.f,1.f);
    const float angle=clampr((Device.vCameraDirection.y+0.25f)/1.25f,0.f,1.f);
    if (rain>0.001f && angle>0.f)
    {
        if (probe_time==u32(-1) || now-probe_time>=200 || probe_position.distance_to_sqr(Device.vCameraPosition)>0.25f)
        {
            probe_position=Device.vCameraPosition; probe_time=now;
            Fvector up; up.set(0,1,0);
            exposed=!g_pGameLevel->ObjectSpace.RayTest(probe_position,up,1000.f,collide::rqtStatic,0,g_pGameLevel->CurrentEntity());
        }
        drive=exposed ? clampr(sqrtf(rain)*angle*angle*amount,0.f,1.f) : 0.f;
    }
    else { exposed=false; probe_time=u32(-1); }
    const int tick=int(floorf(clock_s*4.f));
    const int steps=tick-history_tick;
    if (steps>0)
    {
        for (int i=15;i>=0;--i) birth[i]=(i>=steps) ? birth[i-steps] : 0.f;
        history_tick=tick;
        birth[0]=drive;
    }
    // Birth rate is sampled every 250 ms; never rewrite living cohorts.
    float remaining=0.f;
    for (int i=0;i<16;++i) remaining=_max(remaining,birth[i]);
    status="no_new_or_remaining_drops";
    if (remaining<=0.001f) return;
    if (!ready)
    {
        refract.create("kich_wet_lens"); copy.create("kich_wet_lens_copy");
        geom.create(FVF::F_TL,RCache.Vertex.Buffer(),RCache.QuadIB);
        ready=true;
    }
    if (width!=Device.dwWidth || height!=Device.dwHeight)
    {
        temp.destroy();
        width=Device.dwWidth; height=Device.dwHeight;
        temp.create("$user$kich_wet_lens",width,height,D3DFMT_A8R8G8B8);
    }
    CRenderTarget* target=RImplementation.Target;
    const float w=float(width), h=float(height);
    u32 offset;
    FVF::TL* pv=(FVF::TL*)RCache.Vertex.Lock(4,geom->vb_stride,offset);
    const u32 color=color_rgba(255,255,255,255);
    const float u0=.5f/w,v0=.5f/h,u1=(w+.5f)/w,v1=(h+.5f)/h;
    pv->set(EPS,h+EPS,EPS_S,1.f,color,u0,v1); ++pv;
    pv->set(EPS,EPS,EPS_S,1.f,color,u0,v0); ++pv;
    pv->set(w+EPS,h+EPS,EPS_S,1.f,color,u1,v1); ++pv;
    pv->set(w+EPS,EPS,EPS_S,1.f,color,u1,v0);
    RCache.Vertex.Unlock(4,geom->vb_stride);
    target->u_setrt(temp,0,0,HW.pBaseZB);
    RCache.set_Geometry(geom);
    RCache.set_Element(refract->E[0]);
    RCache.set_CullMode(CULL_NONE); RCache.set_Stencil(FALSE); RCache.set_ColorWriteEnable();
    RCache.set_c("kich_lens_params",drive,strength,clock_s,w/h);
    RCache.set_c("kich_lens_birth0",birth[0],birth[1],birth[2],birth[3]);
    RCache.set_c("kich_lens_birth1",birth[4],birth[5],birth[6],birth[7]);
    RCache.set_c("kich_lens_birth2",birth[8],birth[9],birth[10],birth[11]);
    RCache.set_c("kich_lens_birth3",birth[12],birth[13],birth[14],birth[15]);
    RCache.set_c("kich_lens_pixel",1.f/w,1.f/h,blur,0);
    RCache.Render(D3DPT_TRIANGLELIST,offset,0,4,0,2);
    target->u_setrt(target->rt_Generic_0,0,0,HW.pBaseZB);
    RCache.set_Element(copy->E[0]);
    RCache.Render(D3DPT_TRIANGLELIST,offset,0,4,0,2);
    draws=2; status=drive>0.001f ? "spawning_and_rendering" : "remaining_drops_fading";
}
