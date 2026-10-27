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
#include "KichR4LightingMath.h"

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
extern int ps_r2_sun_shadow_cull;
extern ENGINE_API int ps_r4_sun_shafts;
extern ENGINE_API float ps_r4_sun_shafts_intensity;
extern ENGINE_API int ps_r4_sun_shadow_size;
extern ENGINE_API int ps_r4_sun_far_size;
extern ENGINE_API int ps_r4_sun_far_interval;
extern ENGINE_API int ps_r4_sun_shadow_faces;
extern ENGINE_API int ps_r4_lamp_grass_shadows;
extern ENGINE_API int ps_r4_lamp_shadows;
extern ENGINE_API int ps_r4_shadow_filter;
extern ENGINE_API int ps_r4_sun_mask;
extern ENGINE_API float ps_r4_shadow_normal_bias;
extern ENGINE_API int ps_r4_shadow_terminator;
extern ENGINE_API int ps_r4_shadow_receiver;
extern ENGINE_API int ps_r4_npc_torch_shadows;
extern ENGINE_API float ps_r4_lamp_fade;
extern ENGINE_API int ps_r4_hdr_probe;
extern ENGINE_API int ps_r4_lamp_face_hold;
extern ENGINE_API int ps_r4_dds_async;

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
    // P0-S4: the sun moves continuously; only the shadow texel basis is held
    // (re-based past 3 deg) and the cascades shear onto the true direction.
    static float held_basis[3]={0,0,0};
    kich_r4_lighting::HoldSunDirection(held_basis,frame.sun_direction,.99862953f);
    memcpy(frame.sun_basis,held_basis,sizeof(held_basis));
    frame.sun_basis_valid=fabsf(held_basis[0])+fabsf(held_basis[1])+fabsf(held_basis[2])>0?1u:0u;
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
    // Contact cascade: the r2_sun_near slice of the camera frustum, centred
    // halfway along it; P0-S11 sized as R2's near map (SunContactSpan), which
    // depends on the projection only, so turning the camera never rescales it.
    // P0-S5 r4_sun_shadow_size, rounded to 1024/2048/4096.
    frame.sun_map_size=ps_r4_sun_shadow_size>=3072?4096u:ps_r4_sun_shadow_size>=1536?2048u:1024u;
    // P0-S7 r4_sun_far_size, rounded to 2048/4096/8192: the 512 m cascade only.
    frame.sun_far_size=ps_r4_sun_far_size>=6144?8192u:ps_r4_sun_far_size>=3072?4096u:2048u;
    // P0-S11 r4_sun_far_interval: render the 512 m cascade once every 1..4 frames.
    frame.sun_far_interval=static_cast<uint32_t>(ps_r4_sun_far_interval<1?1:ps_r4_sun_far_interval>4?4:ps_r4_sun_far_interval);
    frame.sun_contact_valid=0;
    if(ps_r2_sun_near>0 && Device.fFOV>0 && Device.fASPECT>0){
        Fvector center;center.mad(Device.vCameraPosition,Device.vCameraDirection,ps_r2_sun_near*.5f);
        frame.sun_contact[0]=center.x;frame.sun_contact[1]=center.y;frame.sun_contact[2]=center.z;
        frame.sun_contact[3]=kich_r4_lighting::SunContactSpan(ps_r2_sun_near,Device.fFOV,Device.fASPECT,frame.sun_map_size);
        frame.sun_contact_valid=_valid(center) && _valid(frame.sun_contact[3])?1u:0u;}
    frame.sun_shadow_cull=ps_r2_sun_shadow_cull;
    // P0-S11 r4_sun_shadow_faces: which caster faces the sun cascades store.
    frame.sun_shadow_faces=static_cast<uint32_t>(ps_r4_sun_shadow_faces<0?0:ps_r4_sun_shadow_faces>2?2:ps_r4_sun_shadow_faces);
    // P0-S5 R4's own r4_sun_shafts/_intensity; segments as R2 accum_direct_volumetric:
    // 0..r2_sun_near and near..min(180, far_plane).
    frame.sun_shafts[0]=float(ps_r4_sun_shafts);frame.sun_shafts[1]=ps_r4_sun_shafts_intensity;
    frame.sun_shafts[2]=ps_r2_sun_near;frame.sun_shafts[3]=_min(180.f,env.far_plane);
    // P0-S7 local light shadows: R2 r2_ls_squality for the per-light map size,
    // and R4 r4_lamp_grass_shadows (off: R2 never draws grass into those maps).
    frame.local_shadow_quality=ps_r2_ls_squality;
    frame.local_detail_shadows=ps_r4_lamp_grass_shadows?1u:0u;
    frame.local_shadows=ps_r4_lamp_shadows?1u:0u; // P0-S9 r4_lamp_shadows
    frame.npc_torch_shadows=ps_r4_npc_torch_shadows?1u:0u; // P0-S11 r4_npc_torch_shadows (the actor's torch keeps its shadow)
    // P0-S10 R4 shadow filter, sun reach mask and receiver lift (sun and lamps).
    // P0-S11 r4_shadow_terminator (cm): terminator lift of facets turned from the light.
    // The shader learns the faces the sun maps hold; r2_sun_shadow_cull 1/2
    // store front faces (two-sided or front), so the lift applies there.
    // r4_shadow_receiver 1: sun and lamps lean along the shading normal instead.
    frame.shadow_options=KichR4ShadowOptions(ps_r4_shadow_filter!=0,ps_r4_sun_mask!=0,ps_r4_shadow_normal_bias,ps_r4_shadow_terminator*.01f,
        ps_r2_sun_shadow_cull?0u:frame.sun_shadow_faces,ps_r4_shadow_receiver!=0);
    // P0-S10 r4_lamp_fade: shadowed lamps fade with screen size over the
    // r__geometry_lod window (KichR4LampLOD); 0 keeps them at full colour.
    KichR4LampFadeWindow(frame.lamp_fade,ps_r4_lamp_fade,Device.dwWidth,Device.dwHeight,Device.fFOV,ps_r__LOD,ps_r__GLOD_ssa_start,ps_r__GLOD_ssa_end);
    // P0-S11 r4_lamp_face_hold: HOM-occluded shadow faces stay this long (KichR4FaceAdmission.h).
    frame.face_hold_ms=ps_r4_lamp_face_hold>0?u32(ps_r4_lamp_face_hold):0u;
    frame.dds_async=ps_r4_dds_async?1u:0u; // P0-S11 r4_dds_async
    // P0-S11 r4_hdr_probe N: the next N engine frames are probed, one count each.
    static u32 probe_frame=u32(-1);
    if(ps_r4_hdr_probe>0 && probe_frame!=Device.dwFrame){probe_frame=Device.dwFrame;--ps_r4_hdr_probe;}
    frame.hdr_probe=probe_frame==Device.dwFrame?1u:0u;
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
