#include "stdafx.h"
#include "KichR4Game.h"
#include "KichR4Native.h"
#include "IGame_Level.h"
#include "IGame_Persistent.h"
#include "XR_IOConsole.h"
#include "Environment.h"
#include "ResourceManager.h"
#include "xrRender/xrRender_console.h"
#include "CameraManager.h"
#include "xr_object.h"
#include "KichR4RecordControl.h"

/* Match FTreeVisual_setup::calculate and its R2 lighting correction. This
 * EXE-only copy is shared by geometry and shadow passes for the whole frame. */
static void KichR4_GameTreeFrame(KichR4GameFrame& input)
{
    const float angle=PI_MUL_2*Device.fTimeGlobal/ps_r__Tree_w_rot;
    const float x=_sin(angle),z=_cos(angle),length=_sqrt(x*x+z*z);
    const float amplitude=length>0?ps_r__Tree_w_amp/length:0;
    input.tree_wind[0]=x*amplitude;input.tree_wind[1]=0;
    input.tree_wind[2]=z*amplitude;input.tree_wind[3]=0;
    input.tree_wave[0]=ps_r__Tree_Wave.x/PI_MUL_2;
    input.tree_wave[1]=ps_r__Tree_Wave.y/PI_MUL_2;
    input.tree_wave[2]=ps_r__Tree_Wave.z/PI_MUL_2;
    input.tree_wave[3]=Device.fTimeGlobal*ps_r__Tree_w_speed/PI_MUL_2;
    input.tree_sbc=ps_r__Tree_SBC*1.3333f;
    input.geometry_lod[0]=ps_r__LOD;input.geometry_lod[1]=ps_r__GLOD_ssa_start;
    input.geometry_lod[2]=ps_r__GLOD_ssa_end;input.geometry_lod[3]=Device.fFOV;
}

extern ENGINE_API BOOL r2_sun_static;
extern float ps_r2_gloss_factor;

/* R2 accum_direct clouds xform, without its view->world prefix: the R4 sun
 * shader passes world positions. R2 advances w_shift by 0.003*dt in each of
 * the near and far sun passes, i.e. twice per dynamic-sun frame. */
static void KichR4_SunMaskFrame(KichR4LightingFrame& frame,const CEnvDescriptorMixer& env)
{
    static float w_shift=0;static u32 advanced_frame=u32(-1);
    frame.sunmask_valid=0;memset(frame.sunmask,0,sizeof(frame.sunmask));
    if(frame.sun_mode!=KICH_R4_SUN_DYNAMIC)return;
    if(advanced_frame!=Device.dwFrame){advanced_frame=Device.dwFrame;w_shift+=2*0.003f*Device.fTimeDelta;}
    // Light_DB sun_adapted direction.
    Fvector direction;direction.set(env.sun_dir);int counter=0;
    while(direction.magnitude()<0.001f && counter<10){direction.add(env.sun_dir);++counter;}
    if(direction.magnitude()<0.001f)return;
    direction.normalize();
    Fmatrix m_xform,clouds;Fvector normal;normal.setHP(env.wind_direction,0);
    Fvector position;position.set(0,0,0);
    m_xform.build_camera_dir(position,direction,normal);
    Fvector localnormal;m_xform.transform_dir(localnormal,normal);localnormal.normalize();
    clouds.set(m_xform);
    m_xform.scale(0.002f,0.002f,1.f);clouds.mulA_44(m_xform);
    m_xform.translate(localnormal.mul(w_shift));clouds.mulA_44(m_xform);
    // HLSL float3x4 rows of R2 mul(m_sunmask,P): tc = (row0.P, row1.P).
    const float rows[8]={clouds._11,clouds._21,clouds._31,clouds._41,clouds._12,clouds._22,clouds._32,clouds._42};
    for(unsigned i=0;i<8;++i)if(!_valid(rows[i]))return;
    memcpy(frame.sunmask,rows,sizeof(rows));frame.sunmask_valid=1;
}

void KichR4_CopyEngineEnvironment(KichR4LightingFrame& frame)
{
    if(!g_pGamePersistent)return;
    CEnvDescriptorMixer& env=g_pGamePersistent->Environment().CurrentEnv;
    KichR4CopyEnvironment(frame,env);
    KichR4CopyR2Hemisphere(frame,env,ps_r2_sun_lumscale_amb,ps_r2_sun_lumscale_hemi);
    /* phase_combine binds sky_r_textures_env[0/1], not the sky textures. */
    for(unsigned i=0;i<2;++i){frame.env_textures[i].clear();
        if(env.sky_r_textures_env.size()<=i || !env.sky_r_textures_env[i].second._get())continue;
        CTexture* texture=env.sky_r_textures_env[i].second._get();
        const char* native=texture->pSurface?KichR4_NativeTextureName(texture->pSurface):NULL;
        if(native)frame.env_textures[i]=native;else if(texture->cName.c_str())frame.env_textures[i]=texture->cName.c_str();}
    for(unsigned i=0;i<3;++i)frame.sun_color[i]*=ps_r2_sun_lumscale;
    frame.gloss_factor=ps_r2_gloss_factor;
    /* renderer_r2a is the R2 static-sun mode; R1 has no R2 sun policy. */
    frame.sun_mode=KichR4SunMode(frame.sun_color,ps_r2_gloss_factor,ps_r2_ls_flags.test(R2FLAG_SUN)!=0,
        psDeviceFlags.test(rsR2) && r2_sun_static);
    KichR4_SunMaskFrame(frame,env);
    const float bloom[8]={ps_r2_ls_bloom_threshold,ps_r2_ls_bloom_kernel_g,ps_r2_ls_bloom_kernel_b,ps_r2_ls_bloom_kernel_scale,
        float(ps_r2_bloom_mode),ps_r2_bloom_strength,ps_r2_bloom_knee,ps_r2_ls_flags.test(R2FLAG_FASTBLOOM)?1.f:0.f};
    memcpy(frame.bloom,bloom,sizeof(bloom));
    const float exposure[5]={ps_r2_ls_flags.test(R2FLAG_TONEMAP)?ps_r2_tonemap_amount:0.f,ps_r2_tonemap_middlegray,
        ps_r2_tonemap_low_lum,ps_r2_tonemap_adaptation,Device.fTimeDelta};
    memcpy(frame.exposure,exposure,sizeof(exposure));
}

static void KichR4_GameObservationFrame(KichR4GameFrame& input)
{
    if(!kich_r4_record::Enabled())return;
    KichR4GameObservation& observation=input.observation;
    memcpy(observation.camera_direction,&Device.vCameraDirection.x,12);
    observation.detail_radius=ps_r__Detail_radius;
    observation.paused=Device.Paused();
    observation.console_visible=Console && Console->bVisible;
    if(g_pGameLevel && g_pGameLevel->bReady){
        // cefDemo covers record/play camera effectors in Release as well as
        // Debug. Never infer flight mode from distance to the controlled object.
        observation.demo_active=g_pGameLevel->Cameras().GetCamEffector(cefDemo)!=NULL;
        const CObject* controlled=g_pGameLevel->CurrentEntity();
        if(controlled){observation.current_entity_valid=true;observation.current_entity_id=controlled->ID();
            memcpy(observation.current_entity_position,&controlled->Position().x,12);}
        const CObject* viewed=g_pGameLevel->CurrentViewEntity();
        if(viewed){observation.view_entity_valid=true;observation.view_entity_id=viewed->ID();
            memcpy(observation.view_entity_position,&viewed->Position().x,12);}
    }
    observation.valid=true;
}

void KichR4_GamePrepareEngineFrame()
{
#if !defined(DEDICATED_SERVER) && !defined(_EDITOR)
    if(!KichR4_GameWindowed())return;
    KichR4GameFrame input={};
    input.window=Device.m_hWnd;input.width=Device.dwWidth;input.height=Device.dwHeight;
    input.engine_frame=Device.dwFrame;input.time=Device.fTimeGlobal;KichR4_GameTreeFrame(input);
    KichR4_GameObservationFrame(input);
    /* Fmatrix::mul(project,view) stores the row-vector view*projection matrix.
     * Both old DX9 and the SM5 shader use D3D clip depth [0,w]; no transpose. */
    memcpy(input.view_projection,Device.mFullTransform.m,sizeof(input.view_projection));
    memcpy(input.camera_projection,Device.mProject.m,sizeof(input.camera_projection));
    input.position[0]=Device.vCameraPosition.x;input.position[1]=Device.vCameraPosition.y;input.position[2]=Device.vCameraPosition.z;
    input.active=Device.b_is_Active!=FALSE;
    const bool menu=g_pGamePersistent && g_pGamePersistent->m_pMainMenu && g_pGamePersistent->m_pMainMenu->IsActive();
    input.allowed=g_pGameLevel && g_pGameLevel->bReady && g_pGameLevel->CurrentViewEntity() &&
        !Device.dwPrecacheFrame && g_loading_events.empty() && !Device.Paused() && !menu && !(Console && Console->bVisible);
    input.toggle_down=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
    input.debug_down=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
    KichR4_CopyEngineEnvironment(input.lighting);
    if(!KichR4_CopyFrozenLighting(input.engine_frame,input.position,input.view_projection,input.lighting))
        KichR4_CaptureLighting(input.position,input.lighting,ps_r2_ls_flags.test(R2FLAG_R1LIGHTS)!=0,input.view_projection);
    KichR4_GamePrepare(input);
#endif
}

void KichR4_NativePostprocess(float*);
void KichR4_NativePresentEngineFrame(bool loading,bool precaching)
{
    KichR4GameFrame input={};input.window=Device.m_hWnd;input.width=Device.dwWidth;input.height=Device.dwHeight;input.engine_frame=Device.dwFrame;input.time=Device.fTimeGlobal;KichR4_GameTreeFrame(input);
    KichR4_GameObservationFrame(input);
    CopyMemory(input.view_projection,Device.mFullTransform.m,64);CopyMemory(input.camera_projection,Device.mProject.m,64);
    CopyMemory(input.position,&Device.vCameraPosition.x,12);input.active=Device.b_is_Active!=FALSE;
    input.prepare_ms=static_cast<float>(KichR4_NativeQueueTime());input.time=Device.fTimeGlobal;KichR4_NativePostprocess(input.postprocess);input.allowed=!loading && g_pGameLevel && g_pGameLevel->bReady && !(g_pGamePersistent && g_pGamePersistent->OnRenderPPUI_query());input.debug_down=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
    input.warmup=loading && precaching && g_pGameLevel && g_pGameLevel->bReady && g_pGameLevel->CurrentViewEntity() &&
        !(g_pGamePersistent && g_pGamePersistent->OnRenderPPUI_query());
    const int path=input.warmup?3:loading?0:input.allowed?2:1;static int previous_path=-1;
    if(path!=previous_path){previous_path=path;Msg("[R4 native B5] output path=%s level_ready=%u precache=%u loading_events=%u",
        input.warmup?"precache/world+loading_UI":loading?"loading/UI":input.allowed?"game":"menu/UI",g_pGameLevel && g_pGameLevel->bReady?1u:0u,Device.dwPrecacheFrame,static_cast<u32>(g_loading_events.size()));}
    input.lighting.valid=1;input.lighting.sun_direction[1]=-1;input.lighting.ambient[0]=input.lighting.ambient[1]=input.lighting.ambient[2]=.2f;
    KichR4_CopyEngineEnvironment(input.lighting);
    if(input.allowed || input.warmup){
        if(!KichR4_CopyFrozenLighting(input.engine_frame,input.position,input.view_projection,input.lighting))
            KichR4_FreezeLighting(input.engine_frame,Device.dwTimeGlobal,input.position,input.lighting,ps_r2_ls_flags.test(R2FLAG_R1LIGHTS)!=0,input.view_projection);
    }else KichR4_CaptureLighting(input.position,input.lighting,ps_r2_ls_flags.test(R2FLAG_R1LIGHTS)!=0,input.view_projection);
    R_ASSERT2(KichR4_NativePresent(input),"R4 native frame failed; see log. Native mode has no DX9 frame to fall back to.");
}
