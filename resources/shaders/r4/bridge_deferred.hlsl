// G3c native deferred materials. DDS channels remain UNORM; no implicit sRGB decode.
cbuffer Frame : register(b0) {
    row_major float4x4 world,mvp,normal_matrix,inverse_vp,shadow[6];
    float4 camera,screen,sun_direction,sun_color,ambient,hemi,fog;
    float4 light_position,light_direction,light_color,alpha;
    float4 material,color_scale,color_bias,sun_scale_bias,tree_wave,tree_wind;
    float4 fog_params,fog_plane; // radial offset/scale, per-draw mode, valid plane
    row_major float4x4 receiver_inverse;
    float4 env_ambient,env_color; // R2 L_ambient (.w r2_gloss_factor), env_color (.w cubemap lerp)
    float4 sunmask_u,sunmask_v;   // R2 m_sunmask rows for world positions (P0-S1)
};
Texture2D image0:register(t0); Texture2D image1:register(t1);
Texture2D shadow0:register(t2); Texture2D shadow1:register(t3);
Texture2D shadow2:register(t4); Texture2D shadow3:register(t5);
Texture2D shadow4:register(t6); Texture2D shadow5:register(t7);
SamplerState surface:register(s0); SamplerComparisonState shadow_surface:register(s1);
SamplerState surface1:register(s2); SamplerState surface2:register(s3);
SamplerState surface3:register(s4); SamplerState surface4:register(s5);
SamplerState surface5:register(s6); SamplerState surface6:register(s7); SamplerState surface7:register(s8);
// Model-only slots preserve every point-shadow face and the spot projector.
Texture2D model_bump:register(t8); Texture2D model_bump_aux:register(t9);
SamplerState model_bump_surface:register(s9); SamplerState model_bump_aux_surface:register(s10);
// Terrain geometry shares t8/t9 with HUD maps in a mutually exclusive mode.
Texture2D terrain_lmap:register(t10); SamplerState terrain_lmap_surface:register(s11);
// Fullscreen-only linear clip W; D24 remains the coverage/stencil authority.
Texture2D receiver_depth_texture:register(t11);
// Material flags: bump=1, baked lightmap=2, hemi map=4, detail=8,
// vertex baked RGB=16, source sun=32, tree light scale/bias=64, emissive=256, authored wallmark=512,
// detail pseudo normal on both faces=32768.
struct StaticInput {float3 p:POSITION;float2 uv:TEXCOORD0;float4 n:NORMAL;
    float3 t:TEXCOORD1;float3 b:TEXCOORD2;float3 uv_sun:TEXCOORD3;float4 light:COLOR0;float2 tree:TEXCOORD4;};
struct ModelInput {float4 p:POSITION;float2 uv:TEXCOORD0;float4 c:COLOR0;float4 n:NORMAL;float3 w:TEXCOORD1;};
struct ScreenInput {float3 p:POSITION;float2 uv:TEXCOORD0;};
struct Varying {float4 p:SV_Position;float2 uv:TEXCOORD0;float4 c:COLOR0;float4 n:TEXCOORD1;float3 w:TEXCOORD2;
    float3 t:TEXCOORD3;float3 b:TEXCOORD4;float2 uv1:TEXCOORD5;float4 baked_sun:TEXCOORD6;};
// SoC/1.6 shared/wmark.h: normal offset plus view-dependent separation.
// Use its intended clamped distance weight (the donor computes w but lerps d).
float AuthoredWallmarkShift(float distance){return lerp(.003,.011,saturate(distance/100));}
float3 AuthoredWallmarkPosition(float3 position,float3 normal){
    float3 away=position-camera.xyz;float distance=length(away);
    away*=rsqrt(max(dot(away,away),1e-12));
    float4 near_point=mul(float4(0,0,0,1),inverse_vp),far_point=mul(float4(0,0,.5,1),inverse_vp);
    float3 forward=far_point.xyz/far_point.w-near_point.xyz/near_point.w;
    forward*=rsqrt(max(dot(forward,forward),1e-12));
    float3 view=forward+away;view*=rsqrt(max(dot(view,view),1e-12));
    normal*=rsqrt(max(dot(normal,normal),1e-12));
    return position+normal*.007-view*AuthoredWallmarkShift(distance);
}
Varying VSStatic(StaticInput v){Varying o=(Varying)0;o.uv=v.uv;o.c=1;
    float3 position=mul(float4(v.p,1),world).xyz;
    if(((uint)material.x&128)!=0){float f=2.8284271*frac(dot(float4(position,1),tree_wave))-1.4142136;
        position.xz+=tree_wind.xz*((position.y-world[3].y)*(f*f-1)*v.tree.y);}
    o.n=float4(mul(float4(v.n.xyz,0),normal_matrix).xyz,v.n.w);o.w=position;
    float3 projected=position;
    [branch] if(((uint)material.x&512)!=0)projected=AuthoredWallmarkPosition(position,o.n.xyz);
    o.p=mul(float4(projected,1),mvp);
    o.t=mul(float4(v.t,0),world).xyz;o.b=mul(float4(v.b,0),world).xyz;o.uv1=v.uv_sun.xy;
    o.baked_sun=float4(v.light.rgb,v.uv_sun.z);
    if(((uint)material.x&64)!=0){o.baked_sun.rgb=v.n.www*color_scale.rgb+color_bias.rgb;
        o.n.w=o.n.w*color_scale.w+color_bias.w;o.baked_sun.w=v.n.w*sun_scale_bias.x+sun_scale_bias.y;}
    return o;}
Varying VSModel(ModelInput v){Varying o=(Varying)0;o.p=v.p;o.uv=v.uv;o.c=v.c;o.n=v.n;o.w=v.w;o.baked_sun.w=1;return o;}
Varying VSModelShadow(ModelInput v){Varying o=VSModel(v);o.p=mul(float4(v.w,1),mvp);return o;}
// Header-first class-sized palette: allocation, reflection and shader capacity agree.
#ifndef KICH_R4_MODEL_BONE_CAPACITY
#define KICH_R4_MODEL_BONE_CAPACITY 256
#endif
cbuffer ModelPose : register(b1) {uint4 model_header;row_major float4x4 model_bones[KICH_R4_MODEL_BONE_CAPACITY];};
struct NativeModelInput {float3 p:POSITION;float2 uv:TEXCOORD0;float weight:BLENDWEIGHT0;uint2 bones:BLENDINDICES0;
    uint packed_normal:NORMAL0;uint packed_tangent:TANGENT0;uint packed_binormal:BINORMAL0;
    float4 uv_hemi_sun:TEXCOORD1;float4 light:COLOR0;uint attributes:TEXCOORD2;};
float3 ModelNormal(uint bits){
    if(bits==0)return 0;
    float3 n=float3(bits&1023,(bits>>10)&1023,(bits>>20)&1023)*(2.0/1023.0)-1;
    return dot(n,n)>1e-12?normalize(n):float3(0,0,0);
}
Varying VSModelGPU(NativeModelInput v){
    Varying o=(Varying)0;float3 p=v.p,n=ModelNormal(v.packed_normal),t=ModelNormal(v.packed_tangent),b=ModelNormal(v.packed_binormal);
    if(model_header.z!=0){
        float3 a=mul(float4(p,1),model_bones[v.bones.x]).xyz;
        float3 na=mul(float4(n,0),model_bones[v.bones.x]).xyz;
        float3 ta=mul(float4(t,0),model_bones[v.bones.x]).xyz;
        float3 ba=mul(float4(b,0),model_bones[v.bones.x]).xyz;
        // Equal-bone 2W vertices ignore their unused weight, matching xrSkin2W.
        if(v.bones.x==v.bones.y){p=a;n=na;t=ta;b=ba;}
        else{float w=v.weight;
            if(model_header.y==2)w=floor(saturate(w)*255+.5)/255;
            p=a*(1-w)+mul(float4(p,1),model_bones[v.bones.y]).xyz*w;
            n=na*(1-w)+mul(float4(n,0),model_bones[v.bones.y]).xyz*w;
            t=ta*(1-w)+mul(float4(t,0),model_bones[v.bones.y]).xyz*w;
            b=ba*(1-w)+mul(float4(b,0),model_bones[v.bones.y]).xyz*w;
        }
    }
    n=mul(float4(n,0),normal_matrix).xyz;
    o.n=float4(dot(n,n)>1e-12?normalize(n):float3(0,0,0),(v.attributes&16)!=0?v.uv_hemi_sun.z:1);
    o.t=mul(float4(t,0),world).xyz;o.b=mul(float4(b,0),world).xyz;o.uv1=v.uv_hemi_sun.xy;
    o.baked_sun=float4(v.light.rgb,(v.attributes&32)!=0?v.uv_hemi_sun.w:1);
    if(color_bias.w>0){o.n.w*=color_scale.w;o.baked_sun.w=((v.attributes&32)!=0?o.baked_sun.w:.5)*sun_scale_bias.z;}
    o.p=mul(float4(p,1),mvp);o.w=mul(float4(p,1),world).xyz;o.uv=v.uv;o.c=1;return o;
}
Varying VSScreen(ScreenInput v){Varying o=(Varying)0;o.p=float4(v.p,1);o.uv=v.uv;o.c=1;return o;}
// G3a: one copy of mesh vertices; per-instance 3x4 transform and sun/hemi.
struct DetailInput {float3 p:POSITION;float4 misc:TEXCOORD0;
    float4 m0:TEXCOORD1;float4 m1:TEXCOORD2;float4 m2:TEXCOORD3;float4 light:TEXCOORD4;};
Varying VSDetail(DetailInput v){Varying o=(Varying)0;float4 local=float4(v.p,1);
    float3 p=float3(dot(v.m0,local),dot(v.m1,local),dot(v.m2,local));
    if(world[2].x>.5){float f=2.8284271*frac(dot(float4(p,1),world[0]))-1.4142136;
        float cyclic=f*f-1;float height=p.y-v.m1.w;
        p.xz+=world[1].xz*(height*cyclic)*v.misc.z;}
    // Sun shadow casters only (world[2].y metres, else 0): push away from the
    // sun along the ray. Depth-only, so tall grass keeps its shadow in place;
    // fallen leaves within a few cm of the ground no longer shade it. This is
    // R2's effective near bias (-0.00004 of a ~1100 m range), details only.
    p+=sun_direction.xyz*world[2].y;
    o.p=mul(float4(p,1),mvp);o.w=p;o.uv=v.misc.xy;o.c=1;
    o.n=float4(normalize(p-float3(v.m0.w,v.m1.w-.75,v.m2.w)),v.light.y);o.baked_sun.w=v.light.x;return o;}
void AlphaTest(float a){float r=alpha.x;int f=(int)alpha.y;
    bool keep=f==8 || (f==2 && a<r) || (f==3 && a==r) || (f==4 && a<=r) ||
        (f==5 && a>r) || (f==6 && a!=r) || (f==7 && a>=r);
    if(!keep)discard;
}
float4 TerrainWeights(Varying v){float4 mask=image1.Sample(surface1,v.uv);return mask/max(dot(mask,1),.0001);}
float4 TerrainChannels(Varying v,float4 mask){float2 tc=v.uv*material.yz;
    return shadow4.Sample(surface6,tc)*mask.r+shadow5.Sample(surface7,tc)*mask.g+
        model_bump.Sample(model_bump_surface,tc)*mask.b+model_bump_aux.Sample(model_bump_aux_surface,tc)*mask.a;}
float4 TerrainBump(Varying v){return TerrainChannels(v,TerrainWeights(v));}
float3 TerrainNormalSample(float4 channels,float total){return channels.wzy-.5*total;}
float4 BaseWithTerrain(Varying v,float4 mask){float4 c=image0.Sample(surface,v.uv)*v.c;
    if(((uint)material.x&8)!=0)c.rgb*=shadow3.Sample(surface5,v.uv*material.yz).rgb*2;
    if(((uint)material.x&4096)!=0){float2 tc=v.uv*material.yz;
        float3 detail=shadow0.Sample(surface2,tc).rgb*mask.r+shadow1.Sample(surface3,tc).rgb*mask.g+
            shadow2.Sample(surface4,tc).rgb*mask.b+shadow3.Sample(surface5,tc).rgb*mask.a;
        if(dot(mask,1)>0)c.rgb*=detail*2;}
    AlphaTest(c.a);return c;}
float4 Base(Varying v){float4 mask=0;
    [branch] if(((uint)material.x&4096)!=0)mask=TerrainWeights(v);
    return BaseWithTerrain(v,mask);}
float3 NormalWithTerrain(Varying v,bool front,float4 channels,float total){float3 n=v.n.xyz;
    if(dot(n,n)<.05)n=cross(ddy(v.w),ddx(v.w));
    n=dot(n,n)>.000001?normalize(n):float3(0,1,0);
    float3 t=v.t-n*dot(n,v.t);float handed=dot(cross(n,t),v.b);
    if(((uint)material.x&(1|8192))!=0 && dot(t,t)>1e-8 && abs(handed)>1e-8){
        t=normalize(t);float3 b=normalize(cross(n,t))*(handed<0?-1:1);
        // SoC R2 sload: bump stores gloss,x-normal in alpha,y in B,z in G;
        // bump# stores signed reconstruction error in RGB, height in alpha.
        float3 mapped;
        [branch] if(((uint)material.x&8192)!=0)mapped=TerrainNormalSample(channels,total);
        else{float4 Nu,NuE;
            [branch] if(((uint)material.x&1024)!=0){Nu=model_bump.Sample(model_bump_surface,v.uv);NuE=model_bump_aux.Sample(model_bump_aux_surface,v.uv);}
            else{Nu=image1.Sample(surface1,v.uv);NuE=shadow0.Sample(surface2,v.uv);}
            mapped=Nu.wzy+NuE.xyz-1;}
        mapped.z*=.5;
        float3 result=t*mapped.x+b*mapped.y+n*mapped.z;
        if(dot(result,result)>1e-8)n=normalize(result);
    }
    // Flip the complete mapped normal on the back face, preserving mirrored UV handedness.
    // Details (32768) keep their R2 pseudo normal on both faces: SoC R2 detail
    // shaders never flip it, and a flipped curled leaf edge read as a shadow.
    return front || ((uint)material.x&32768)!=0?n:-n;}
float3 Normal(Varying v,bool front){float4 channels=0;float total=0;
    [branch] if(((uint)material.x&8192)!=0){float4 weights=TerrainWeights(v);channels=TerrainChannels(v,weights);total=dot(weights,1);}
    return NormalWithTerrain(v,front,channels,total);}
// Octahedral normals free two GBuffer components without another render target.
// The 11-bit integer in .w is exactly representable by RGBA16F: 7 gloss bits,
// 4 material bits. The four SoC material slices clamp at class 3.
float2 NormalSign(float2 v){return float2(v.x>=0?1:-1,v.y>=0?1:-1);}
float2 EncodeNormal(float3 n){n/=max(dot(abs(n),float3(1,1,1)),1e-8);
    return n.z>=0?n.xy:(1-abs(n.yx))*NormalSign(n.xy);}
float3 DecodeNormal(float2 e){float3 n=float3(e,1-abs(e.x)-abs(e.y));
    if(n.z<0){n.xy=(1-abs(n.yx))*NormalSign(n.xy);}return normalize(n);}
float PackMaterial(float gloss,float m){return floor(saturate(gloss)*127+.5)*16+floor(clamp(m,0,3)*5+.5);}
float MaterialGloss(float packed){return floor(packed/16)/127;}
float MaterialClass(float packed){return fmod(packed,16)*.2;}
float GlossWithTerrain(Varying v,float4 channels,float total){
    // SoC sload.h: bump R is squared gloss, flat surfaces use def_gloss.
    float gloss=2.0/255;
    if(((uint)material.x&8192)!=0)return total>0?saturate(channels.r):gloss;
    if(((uint)material.x&1)!=0){float g;
        [branch] if(((uint)material.x&1024)!=0)g=model_bump.Sample(model_bump_surface,v.uv).r;
        else {g=image1.Sample(surface1,v.uv).r;}gloss=g*g;}
    if(((uint)material.x&9)==9)gloss*=shadow3.Sample(surface5,v.uv*material.yz).a*2;
    return saturate(gloss);
}
float Gloss(Varying v){float4 channels=0;float total=0;
    [branch] if(((uint)material.x&8192)!=0){float4 weights=TerrainWeights(v);channels=TerrainChannels(v,weights);total=dot(weights,1);}
    return GlossWithTerrain(v,channels,total);}
struct GeometryOutput {float4 albedo:SV_Target0;float4 normal:SV_Target1;float4 baked:SV_Target2;float receiver_depth:SV_Target3;};
GeometryOutput PSGeometry(Varying v,bool front:SV_IsFrontFace){GeometryOutput o;
    uint flags=(uint)material.x;float h=v.n.w,s=(flags&32)!=0?v.baked_sun.w:.5;float3 baked=0;
    if((flags&16)!=0)baked=v.baked_sun.rgb;
    // R1/XRLC baked RGB is diagnostic only. SoC R2 deferred (uber_deffer,
    // deffer_base_*) binds only s_hemi and vertex color.w; lightmap/vertex RGB
    // belongs to R1 lmape and would double the dynamic lights here.
    if((flags&2)!=0){float4 lm=shadow1.Sample(surface3,v.uv1);baked+=lm.rgb;s=lm.a;}
    if((flags&4)!=0){float4 lm=shadow2.Sample(surface4,v.uv1);h=dot(lm.rgb,float3(1.0/3,1.0/3,1.0/3));s=lm.a;}
    // Geometry shares each authored terrain sample across albedo, normal and
    // gloss explicitly; do not depend on cross-branch compiler CSE.
    float4 terrain_weights=0,terrain_channels=0;
    [branch] if((flags&(4096|8192))!=0)terrain_weights=TerrainWeights(v);
    [branch] if((flags&8192)!=0)terrain_channels=TerrainChannels(v,terrain_weights);
    float terrain_total=dot(terrain_weights,1);
    float4 base_color=BaseWithTerrain(v,terrain_weights);
    if((flags&2048)!=0)h=base_color.a;
    if((flags&16384)!=0)s=terrain_lmap.Sample(terrain_lmap_surface,v.uv).a;
    o.albedo=float4(base_color.rgb,saturate(s));
    float gloss=GlossWithTerrain(v,terrain_channels,terrain_total);
    o.normal=float4(EncodeNormal(NormalWithTerrain(v,front,terrain_channels,terrain_total)),saturate(h),PackMaterial(gloss,material.w));o.baked=0;
    // SoC E[4] accum_emissive[l] writes constant irradiance/specular. Fold it
    // into this base surface's accumulator once, retaining its depth/coverage.
    if((flags&256)!=0){o.baked.rgb+=ambient.www;o.baked.a+=hemi.w*gloss;}
    // Diagnostics keep separate components before accumulation, with no new MRT.
    if((int)screen.z==6)o.albedo.rgb=baked;
    if((int)screen.z==9){float3 n=v.n.xyz;if(dot(n,n)<.05)n=cross(ddy(v.w),ddx(v.w));
        n=dot(n,n)>1e-6?normalize(n):float3(0,1,0);o.albedo.rgb=(front?n:-n)*.5+.5;}
    if((int)screen.z==8)o.baked=0;
    // D3D11 PS SV_Position.w is interpolated clip W (not reciprocal W).
    o.receiver_depth=v.p.w;
    return o;}
void PSShadow(Varying v){if((int)alpha.y!=8)Base(v);}
float FogAmount(float distance){return fog_params.y>0?saturate(distance*fog_params.y+fog_params.x):0;}
float FogTransmittance(float3 world_position){return fog_params.z>0 && fog_params.w>0?saturate(dot(float4(world_position,1),fog_plane)):1;}
float3 ForwardFog(float3 color,float transmittance){
    // Additive base effects attenuate without adding a fog-colored quad.
    return fog_params.z>1.5?color*transmittance:lerp(fog.rgb,color,transmittance);
}
// P0-S8 static lamp shadow cache: copies a cached depth map (t0) texel for
// texel into the bound shadow map (depth func ALWAYS, no colour target).
void PSDepthCopy(Varying v,out float depth:SV_Depth){depth=image0.Load(int3(int2(v.p.xy),0)).r;}
float4 PSPlain(Varying v):SV_Target {float4 c=Base(v);
    // Non-emissive UI/copy/glow keeps the authored value. Blended self-light
    // folds its known R2 emissive response once; opaque emission uses GBuffer.
    if(ambient.w>0)c.rgb=c.rgb*ambient.w+hemi.w*Gloss(v);
    c.rgb=ForwardFog(c.rgb,FogTransmittance(v.w));
    return c;}
// Dynamic wallmarks use vertex alpha as AGE, not opacity, in MUL_2X+fade.
// SoC BLENDDIFFUSEALPHA / X-Ray 1.6 stub_default_ma: a fresh neutral-gray
// texel remains 0.5 so DESTCOLOR+SRCCOLOR leaves receiver albedo unchanged.
float WallmarkChannel(float texel,float vertex,float age,int mode){
    if(mode==4)return lerp(texel,vertex,age);
    if(mode==3)return texel;
    if(mode==2)return saturate(texel*vertex*4);
    if(mode==1)return saturate(texel*vertex*2);
    return texel*vertex;
}
float WallmarkAlpha(float texel,float age,int mode){return mode>=1 && mode<=3?texel:texel*age;}
float4 PSWallmark(Varying v):SV_Target {
    float4 texel=image0.Sample(surface,v.uv);int mode=(int)alpha.w;
    float4 c=float4(WallmarkChannel(texel.r,v.c.r,v.c.a,mode),
        WallmarkChannel(texel.g,v.c.g,v.c.a,mode),WallmarkChannel(texel.b,v.c.b,v.c.a,mode),
        WallmarkAlpha(texel.a,v.c.a,mode));
    AlphaTest(c.a);return c;
}
// Source-only sky mask: integer pixel loads cannot generate a diagonal from
// far-plane depth interpolation. The same depth predicate is used by combine.
float4 PSCelestial(Varying v,uint primitive_id:SV_PrimitiveID):SV_Target {
    int mode=(int)screen.z;
    if(mode!=11 && mode!=12 && image1.Load(int3(int2(v.p.xy),0)).r<1)discard;
    if(mode==12)return (primitive_id&1)==0?float4(1,0,0,1):float4(0,1,0,1);
    return Base(v);
}
float3 RelativePosition(float2 pixel,float depth){float4 p=float4(pixel.x*2/screen.x-1,1-pixel.y*2/screen.y,depth,1);
    p=mul(p,inverse_vp);return p.xyz/p.w;}
float3 WorldPosition(float2 pixel,float depth){return RelativePosition(pixel,depth)+camera.xyz;}
float3 ReceiverPosition(float2 pixel,float linear_depth,float depth){
    if(receiver_inverse[3][3]==0)return RelativePosition(pixel,depth);
    float2 ndc=float2(pixel.x*2/screen.x-1,1-pixel.y*2/screen.y);
    return mul(float4(ndc*linear_depth,linear_depth,1),receiver_inverse).xyz;
}
float3 LoadReceiverPosition(float2 pixel,float depth){
    return ReceiverPosition(pixel,receiver_depth_texture.Load(int3(int2(pixel),0)).r,depth);
}
// Derivatives are captured before depth rejection, alpha discard, attenuation
// and cube-face selection. Taking ddx/ddy inside those branches is undefined.
bool ShadowPlaneStable(float distance_squared,float step_squared,float plane_error_squared){
    // A large screen-pixel step is normal on continuous grazing receivers.
    // Reject noncoplanarity instead, allowing depth/float reconstruction noise.
    return isfinite(distance_squared) && isfinite(step_squared) && isfinite(plane_error_squared) &&
        plane_error_squared<=max(4e-6,max(distance_squared*2.5e-7,step_squared*1e-6));
}
float ShadowPlaneErrorSquared(float3 dx,float3 dy,float3 mixed){
    float3 plane=cross(dx,dy);float length_squared=dot(plane,plane),residual=dot(plane,mixed);
    return length_squared>1e-30?residual*residual/length_squared:1e30;
}
void ShadowDerivatives(float3 p,out float3 dx,out float3 dy){
    // p is camera-relative. Fine derivatives preserve the four positions of
    // this quad; the mixed difference lies in the same plane for any continuous
    // planar receiver, including perspective foreshortening near the horizon.
    dx=ddx_fine(p);dy=ddy_fine(p);float error_squared=ShadowPlaneErrorSquared(dx,dy,ddx_fine(dy));
    if(!all(isfinite(dx)) || !all(isfinite(dy)) ||
        !ShadowPlaneStable(dot(p,p),max(dot(dx,dx),dot(dy,dy)),error_squared)){dx=0;dy=0;}
}
// P0-S9 receiver plane from neighbouring pixels (deferred sun/local passes).
// ddx_fine differences pair pixels by 2x2 quads: a quad astride a mesh edge
// fails ShadowPlaneStable and its pixels fall back to one comparison (or the
// normal plane), so every road/terrain triangle edge drew a thin line and the
// moonlit crest (|N.L|<=.2, no normal fallback) a stepped band. Here each pixel
// takes, per axis, the two neighbours on the side that continues its own
// surface: in perspective 1/z is linear on a plane, so the side with the
// smaller second difference of 1/z lies in the pixel's plane. Both sides bent
// (1-2 pixel features, silhouettes) keep the quad result.
float ReceiverInverseDepth(int2 at){
    if(any(at<0) || any(at>=int2(screen.xy)))return -1;
    float z=receiver_depth_texture.Load(int3(at,0)).r;return z>0?1/z:-1;}
float ShadowSideCurvature(float c,float near1,float near2){return near1>0 && near2>0?abs(near2-2*near1+c):1e30;}
void ShadowDerivativesLoaded(float2 pixel,float3 p,inout float3 dx,inout float3 dy){
    if(receiver_inverse[3][3]==0)return;
    int2 at=int2(pixel);float c=ReceiverInverseDepth(at);if(!(c>0))return;
    float left=ShadowSideCurvature(c,ReceiverInverseDepth(at-int2(1,0)),ReceiverInverseDepth(at-int2(2,0)));
    float right=ShadowSideCurvature(c,ReceiverInverseDepth(at+int2(1,0)),ReceiverInverseDepth(at+int2(2,0)));
    float up=ShadowSideCurvature(c,ReceiverInverseDepth(at-int2(0,1)),ReceiverInverseDepth(at-int2(0,2)));
    float down=ShadowSideCurvature(c,ReceiverInverseDepth(at+int2(0,1)),ReceiverInverseDepth(at+int2(0,2)));
    // 1% of 1/z: planes give ~1e-6 (float), depth jumps far more.
    float tolerance=c*.01;
    if(min(left,right)>tolerance || min(up,down)>tolerance)return;
    int2 sx=left<right?int2(-1,0):int2(1,0),sy=up<down?int2(0,-1):int2(0,1);
    float2 nx=float2(at+sx)+.5,ny=float2(at+sy)+.5;
    float3 ex=LoadReceiverPosition(nx,image1.Load(int3(at+sx,0)).r)-p,ey=LoadReceiverPosition(ny,image1.Load(int3(at+sy,0)).r)-p;
    if(!all(isfinite(ex)) || !all(isfinite(ey)))return;
    dx=sx.x>0?ex:-ex;dy=sy.y>0?ey:-ey;}
bool ShadowGradient(float3 dx,float3 dy,out float2 gradient){
    gradient=0;
    float determinant=dx.x*dy.y-dx.y*dy.x;
    float scale=max(dx.x*dx.x+dx.y*dx.y,dy.x*dy.x+dy.y*dy.y);
    if(abs(determinant)<=max(scale*1e-5,1e-20))return false;
    gradient=float2(dx.z*dy.y-dy.z*dx.y,dx.x*dy.z-dy.x*dx.z)/determinant;
    return all(isfinite(gradient));
}
float ShadowTent(float offset){
    float a=abs(offset),edge=max(1.5-a,0);
    return a<.5?.75-a*a:.5*edge*edge;
}
float3 ShadowDerivative(float4 projected,float4 derivative){
    float3 d=(derivative.xyz-(projected.xyz/projected.w)*derivative.w)/projected.w;
    return float3(d.x*.5,-d.y*.5,d.z);
}
float PCF(Texture2D map,float4 projected,float4 dx,float4 dy){if(projected.w<=0)return 0;
    float3 q=projected.xyz/projected.w;float2 uv=float2(q.x*.5+.5,.5-q.y*.5);
    if(any(uv<0) || any(uv>1) || q.z<0 || q.z>1)return 0;
    float2 gradient;
    // Without a receiver plane, do not extrapolate its depth across a 3x3
    // footprint. One comparison preserves real blockers and bounds the fallback
    // to the containing texel instead of introducing a broad false shadow.
    if(!ShadowGradient(ShadowDerivative(projected,dx),ShadowDerivative(projected,dy),gradient)){
        float2 fallback_uv=(floor(uv*screen.w)+.5)/screen.w;
        fallback_uv.x=min(fallback_uv.x,1-.5/screen.w);fallback_uv.y=min(fallback_uv.y,1-.5/screen.w);
        return map.SampleCmpLevelZero(shadow_surface,fallback_uv,q.z);
    }
    float2 center=floor(uv*screen.w)+.5;float result=0,weight=0;
    // Nine POINT comparisons at actual texel centres. Each comparison uses
    // the receiver depth at that centre, not the depth of the original pixel.
    // Continuous quadratic tent weights avoid texel snapping in the result.
    // Unlike hardware bilinear comparison, this needs no additional slope
    // bias to cover four hidden neighbours; close blockers remain intact.
    [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){
        float2 sample_uv=(center+float2(x,y))/screen.w;
        if(any(sample_uv<0) || any(sample_uv>1))continue;
        float2 offset=sample_uv-uv;
        float w=ShadowTent(offset.x*screen.w)*ShadowTent(offset.y*screen.w);
        float compare_depth=saturate(q.z+dot(gradient,offset));
        result+=w*map.SampleCmpLevelZero(shadow_surface,sample_uv,compare_depth);weight+=w;
    }
    return weight>0?result/weight:0;
}
// P0-S6: one PCFWide texel, the plane-corrected point comparison at its
// centre against the depth Gather returned (LESS_EQUAL, as shadow_surface).
void ShadowWideTexel(float stored,float2 centre,float2 texel,float size,float depth,float2 gradient,float2 slack,inout float result,inout float weight){
    float2 offset=centre-texel;float w=ShadowTent(offset.x*.5)*ShadowTent(offset.y*.5);
    if(w<=0 || any(centre<0) || any(centre>size))return;
    result+=w*(saturate(depth+dot(gradient,offset/size)-abs(dot(slack,offset)))<=stored?1:0);weight+=w;
}
// P0-S8 crease allowance. The receiver plane is exact for one facet, but the
// filter footprint crosses mesh edges: beyond a concave edge the next facet
// lies above the extended plane and shadows it. With the light at angle T to
// the normal the depth slope is tan T; a facet turned by B (3 degrees here)
// steepens it by tan(T+B)-tan T, that fraction of |gradient| along the
// gradient (across it the change is cos T times smaller, left out). The error
// grows with the distance from the receiver, so the allowance does too and is
// zero at the receiver: contact shadows stay, 2 cm blockers soften at the rim.
// Without it the moon at 13 degrees made 1-2 degree road and terrain edges
// 35-55% dark ("night stripes"); at 31 degrees the same edges stayed below 7%.
// P0-S9 returns (allowance, gradient scale). A face almost parallel to the
// light rose metres within the +-3 texel footprint and looked past a roof
// 2.5 m above it (lit triangles on NPC bodies indoors, SUN_PCF_VISIBILITY).
// The plane is held to a rise per texel (ShadowRiseLimit) and never past 87
// degrees (tan 19.1). Held faces self-shadow ~60% (measured), so the limit is
// per map: 0.6 m in the contact/128 m cascades and lamps, where a roof can be
// 2.5 m away (84 deg on 2048/128 m, 87 on 4096); 2 m in the 512 m cascade,
// which carries distant terrain under a low sun (86.4 deg on 4096/512 m).
// Faces beyond the limit get |N.L| < .1 of the light.
float ShadowRiseLimit(float rise,float texel){return min(19.1,rise/max(texel,1e-5));}
float2 ShadowCrease(float3 dx,float3 dy,float3 towards,float limit){
    float3 n=cross(dx,dy);float squared=dot(n,n);
    if(squared<=1e-20)return float2(0,1);
    float c=saturate(abs(dot(n,towards))*rsqrt(squared)),s=sqrt(saturate(1-c*c));
    float t=s/max(c,1e-4),b=.0524; // tan 3 degrees
    float scale=min(1,limit/max(t,1e-4));t=min(t,limit);
    // (tan(T+B)-tan T)/tan T; past 87 degrees the cap holds.
    float denominator=t*(1-t*b);
    return float2(denominator>0?min(b*(1+t*t)/denominator,.5):.5,scale);
}
// P0-S5 sun PCF: the same plane-corrected point comparisons at texel centres,
// with the quadratic B-spline (ShadowTent) stretched to two texels. The 6x6
// centres cover its +-3 texel support exactly, so the result stays continuous.
// At grazing sun the map's stair steps fall below the filter width; with the
// 4096 maps the blur equals the old 2048 one while the stairs are halved.
// P0-S6: the 36 centres are nine 2x2 quads read by DX11 Gather (x,y,z,w =
// lower-left, lower-right, upper-right, upper-left) and compared per texel:
// 9 fetches instead of 36, and a small program.
// P0-S7: map = shadow0..5 (t2..t7): sun near/far/contact are 0/1/5, a lamp's
// cube faces 0..5 (P0-S7 lamps use it too). A literal index folds to one map;
// a lamp's per-pixel face shares one filter and branches only the fetches.
// The size is the map's own (GetDimensions): the 512 m cascade has its own
// resolution (r4_sun_far_size), and a culled lamp face binds a 16x16 lit map.
float ShadowMapSize(uint map){uint width=1,height=1;
    [branch] switch(map){case 0:shadow0.GetDimensions(width,height);break;case 1:shadow1.GetDimensions(width,height);break;
        case 2:shadow2.GetDimensions(width,height);break;case 3:shadow3.GetDimensions(width,height);break;
        case 4:shadow4.GetDimensions(width,height);break;default:shadow5.GetDimensions(width,height);break;}
    return width;}
float4 ShadowGather(uint map,float2 uv){float4 stored=0;
    [branch] switch(map){case 0:stored=shadow0.Gather(surface,uv);break;case 1:stored=shadow1.Gather(surface,uv);break;
        case 2:stored=shadow2.Gather(surface,uv);break;case 3:stored=shadow3.Gather(surface,uv);break;
        case 4:stored=shadow4.Gather(surface,uv);break;default:stored=shadow5.Gather(surface,uv);break;}
    return stored;}
float ShadowCompare(uint map,float2 uv,float depth){float lit=0;
    [branch] switch(map){case 0:lit=shadow0.SampleCmpLevelZero(shadow_surface,uv,depth);break;case 1:lit=shadow1.SampleCmpLevelZero(shadow_surface,uv,depth);break;
        case 2:lit=shadow2.SampleCmpLevelZero(shadow_surface,uv,depth);break;case 3:lit=shadow3.SampleCmpLevelZero(shadow_surface,uv,depth);break;
        case 4:lit=shadow4.SampleCmpLevelZero(shadow_surface,uv,depth);break;default:lit=shadow5.SampleCmpLevelZero(shadow_surface,uv,depth);break;}
    return lit;}
// P0-S9: depth outside [0,1] is clamped, not rejected. A receiver beyond the
// map's far bound is behind every stored caster (shadowed by any of them, lit
// over cleared texels); one before its near bound is in front of them all.
// Cascade selection decides which map covers a pixel, never this function.
float PCFWide(uint map,float4 projected,float4 dx,float4 dy,float2 crease){if(projected.w<=0)return 0;
    float3 q=projected.xyz/projected.w;float2 uv=float2(q.x*.5+.5,.5-q.y*.5);
    if(any(uv<0) || any(uv>1))return 0;
    float size=ShadowMapSize(map);
    float2 gradient;
    if(!ShadowGradient(ShadowDerivative(projected,dx),ShadowDerivative(projected,dy),gradient)){
        float2 fallback_uv=(floor(uv*size)+.5)/size;
        fallback_uv.x=min(fallback_uv.x,1-.5/size);fallback_uv.y=min(fallback_uv.y,1-.5/size);
        return ShadowCompare(map,fallback_uv,saturate(q.z));
    }
    float2 texel=uv*size,first=floor(texel-2.5)+.5;float result=0,weight=0;
    gradient*=crease.y;float2 slack=crease.x*gradient/size;
    [unroll]for(int y=0;y<6;y+=2)[unroll]for(int x=0;x<6;x+=2){
        float2 corner=first+float2(x,y);float4 stored=ShadowGather(map,(corner+.5)/size);
        ShadowWideTexel(stored.x,corner+float2(0,1),texel,size,q.z,gradient,slack,result,weight);
        ShadowWideTexel(stored.y,corner+float2(1,1),texel,size,q.z,gradient,slack,result,weight);
        ShadowWideTexel(stored.z,corner+float2(1,0),texel,size,q.z,gradient,slack,result,weight);
        ShadowWideTexel(stored.w,corner,texel,size,q.z,gradient,slack,result,weight);
    }
    return weight>0?result/weight:0;
}
float3 ShadowNumericalOffset(float3 p,float3 dx,float3 dy,float3 towards){
    // Receiver-plane extrapolation magnifies floating point error near grazing
    // incidence. Use the geometric plane, never the mapped shading normal.
    // The allowance follows coordinate precision and incidence, capped at 2 mm;
    // it cannot turn a close 2 cm blocker into a lit receiver.
    float3 n=cross(dx,dy);float squared=dot(n,n);
    if(squared<=1e-20)return p;
    n*=rsqrt(squared);float facing=dot(n,towards);if(facing<0)n=-n;
    float coordinate=max(max(max(abs(p.x),abs(p.y)),abs(p.z)),length(p-camera.xyz));
    float allowance=min(.002,16*1.192092896e-7*coordinate/max(abs(facing),.02));
    return p+n*allowance;
}
// World size of one texel of the sun cascade shadow[cascade] (orthographic:
// the projected x of a world unit vector is 2/span) stored in map.
float SunTexel(uint cascade,uint map){
    float3 x=float3(mul(float4(1,0,0,0),shadow[cascade]).x,mul(float4(0,1,0,0),shadow[cascade]).x,mul(float4(0,0,1,0),shadow[cascade]).x);
    return 2/max(length(x),1e-6)/ShadowMapSize(map);}
// Weight of a cascade whose last 10% fades to the next one (edge in [0,1)).
float ShadowCascadeWeight(float edge){return 1-saturate((edge-.9)*10);}
// P0-S2: shadow[2] (t7) is a texel-snapped contact cascade over the stable
// r2_sun_near sphere; shadow[2][3][3]==1 marks it valid (ortho matrices), zero
// keeps the 128/512 m pair only. Same offsets; P0-S5 sun maps use PCFWide.
// P0-S5 coverage: beyond the 512 m cascade there is no caster information.
// PCF returns full shadow outside a map, which would black out distant
// terrain; the last 10% of the far cascade fades to lit instead, across its
// XY edges and both ends of its depth range alike (|2z-1| is the depth edge).
// P0-S6: the nested E1.9 blends (contact -> 128 m -> 512 m -> lit) are the
// same weights summed, so each map is filtered at most once per pixel and
// appears once in the program (nested calls inlined PCFWide ten times and
// made the startup compile of PSSun take over a minute).
float SunVisibility(float3 p,float3 dx,float3 dy){
    if(sun_color.w<=0 || sun_color.w>1.5)return 0;
    // Precision allowance only: plane correction handles the filter footprint.
    p=ShadowNumericalOffset(p,dx,dy,-sun_direction.xyz)-sun_direction.xyz*.01;
    float4 contact_position=mul(float4(p,1),shadow[2]),near_position=mul(float4(p,1),shadow[0]),far_position=mul(float4(p,1),shadow[1]);
    float rest=1,contact_weight=0,near_weight=0,far_weight=0;
    // P0-S9: the contact cascade owns its XY square at any depth (PCFWide
    // clamps depth). Its depth window is centred on the r2_sun_near sphere, not
    // on the camera like the 128/512 m pair, so a depth test here used to hand
    // pixels ~0.5 km along the sun ray to the 128 m map, from which the cascade
    // hole (StaticFramePlan::AddSunHole, XY only) removes the contact casters.
    [branch] if(shadow[2][3][3]>.5){float3 q=contact_position.xyz/contact_position.w;float edge=max(abs(q.x),abs(q.y));
        if(edge<1)contact_weight=ShadowCascadeWeight(edge);}
    rest-=contact_weight;
    {float3 q=near_position.xyz/near_position.w;float edge=max(abs(q.x),abs(q.y));
        if(q.z>=0 && q.z<=1 && edge<1)near_weight=rest*ShadowCascadeWeight(edge);}
    rest-=near_weight;
    {float3 q=far_position.xyz/far_position.w;float edge=max(max(abs(q.x),abs(q.y)),abs(q.z*2-1));
        if(edge<1)far_weight=rest*ShadowCascadeWeight(edge);}
    float result=rest-far_weight;
    [branch] if(contact_weight>0)result+=contact_weight*PCFWide(5,contact_position,mul(float4(dx,0),shadow[2]),mul(float4(dy,0),shadow[2]),ShadowCrease(dx,dy,-sun_direction.xyz,ShadowRiseLimit(.6,SunTexel(2,5))));
    [branch] if(near_weight>0)result+=near_weight*PCFWide(0,near_position,mul(float4(dx,0),shadow[0]),mul(float4(dy,0),shadow[0]),ShadowCrease(dx,dy,-sun_direction.xyz,ShadowRiseLimit(.6,SunTexel(0,0))));
    [branch] if(far_weight>0)result+=far_weight*PCFWide(1,far_position,mul(float4(dx,0),shadow[1]),mul(float4(dy,0),shadow[1]),ShadowCrease(dx,dy,-sun_direction.xyz,ShadowRiseLimit(2,SunTexel(1,1))));
    return result;
}
// P0-S1 R2 shadow.h sunmask(): sunmask.dds alpha through m_sunmask, 1/500 m
// tiles along the sun drifting with the wind; both R2 sun passes multiply the
// shadow by it. t6 is white when no mask. frac() keeps WRAP under the global
// clamp of fullscreen passes; gradients come from the receiver plane, so no
// derivative is taken inside lighting branches.
float SunMask(float3 p,float3 dx,float3 dy){
    float4 position=float4(p,1);
    float2 tc=float2(dot(position,sunmask_u),dot(position,sunmask_v));
    float2 gx=float2(dot(dx,sunmask_u.xyz),dot(dx,sunmask_v.xyz)),gy=float2(dot(dy,sunmask_u.xyz),dot(dy,sunmask_v.xyz));
    return shadow4.SampleGrad(surface6,frac(tc),gx,gy).a;
}
// sun_color.w 2: R2 USE_R2_STATIC_SUN, baked occlusion*2 instead of sun maps
// (no sunmask there, as in R2 combine).
float SunDirect(float3 p,float3 dx,float3 dy,float static_sun){float result=static_sun*2;
    [branch] if(sun_color.w<=1.5)result=SunVisibility(p,dx,dy)*SunMask(p,dx,dy);
    return result;
}
// P0-L3: SoC r2_rendertarget.cpp s_material, generated on the CPU by the same
// code (KichR4MaterialLut.h): 128 (L.N) x 256 (H.N) x 4 slices, 8-bit, forced
// last texel. The four slices are stacked rows slice*256+y; R diffuse, A specular.
Texture2D material_lut:register(t10);SamplerState material_lut_surface:register(s11);
float2 MaterialLutSlice(float2 texel,float slice){
    float4 t=material_lut.SampleLevel(material_lut_surface,(texel+float2(.5,.5+slice*256))/float2(128,1024),0);
    return float2(t.r,t.a);
}
// R2 tex3D(s_material,(L.N,H.N,(mtl+.5)/4)) with r_Sampler_clw: LINEAR, U/V
// CLAMP, W WRAP. Texel-centre space keeps the half-texel offset of R2; U/V are
// clamped inside each slice, and W filtering is the lerp of two wrapped slices.
float2 MaterialResponse(float ld,float lh,float m){
    float2 texel=clamp(float2(ld*128,lh*256)-.5,float2(0,0),float2(127,255));
    float slice=floor(m),f=m-slice;slice-=4*floor(slice*.25);
    float2 result=MaterialLutSlice(texel,slice);
    [branch] if(f>0)result=lerp(result,MaterialLutSlice(texel,slice<3?slice+1:0),f);
    return result;
}
// R2 u_diffuse2s, including its r2_gloss_factor scale.
float SpecularIntensity(float3 color){float value=max(0,dot(color,float3(1.0/3,1.0/3,1.0/3)));
    return env_ambient.w*(value<1?pow(value,2.0/3):value);}
float4 DirectLight(float3 p,float3 n,float3 direction,float3 color,float gloss,float m){
    float3 view=camera.xyz-p;view/=max(length(view),1e-8);
    float3 half_vector=direction+view;half_vector/=max(length(half_vector),1e-8);
    float2 response=MaterialResponse(dot(n,direction),dot(n,half_vector),m);
    return float4(color*response.x,SpecularIntensity(color)*response.y*gloss);
}
// Forward surfaces only: R2 forward shaders use L_ambient+L_hemi_color, not
// hmodel. Source hemi already encodes visibility; no extra world-up term.
float3 ForwardIndirect(float h,float m){return ambient.rgb+hemi.rgb*MaterialResponse(saturate(h),0,m).x;}
// P0-S3 fallback receiver plane. Where the 2x2 screen quad is not coplanar
// (fence edges, thin boards, foliage) ShadowDerivatives returns zero and PCF
// would make one unfiltered comparison: hard texel squares that flicker as the
// quad pattern changes. The shading normal then defines the plane for the same
// 3x3 plane-corrected filter; 1 cm more toward the sun absorbs the bump-normal
// tilt. Grazing (|N.L|<=.2) keeps the single comparison: there an extrapolated
// plane would leak light, and direct light is nearly zero anyway.
void ShadowNormalPlane(float3 n,float3 towards,inout float3 p,inout float3 dx,inout float3 dy){
    float facing=abs(dot(n,towards));
    [branch] if(dot(dx,dx)+dot(dy,dy)==0 && facing>.2 && dot(n,n)>.5){
        float3 axis=abs(n.y)<.99?float3(0,1,0):float3(1,0,0);
        float3 t=normalize(cross(n,axis));
        dx=t*.01;dy=cross(n,t)*.01;p+=towards*.01;}
}
float SunShadowing(float3 p,float3 n,float3 dx,float3 dy,float static_sun){
    ShadowNormalPlane(n,-sun_direction.xyz,p,dx,dy);return SunDirect(p,dx,dy,static_sun);}
float4 SunLighting(float3 p,float3 n,float gloss,float m,float3 dx,float3 dy,float static_sun){
    // R2/1.6 use baked/ROS sun occlusion only in static-sun mode, where the
    // dynamic sun pass is disabled. Here the current shadow maps own visibility;
    // a baked zero must not suppress sunlight and moving shadows on a receiver.
    return DirectLight(p,n,-sun_direction.xyz,sun_color.rgb,gloss,m)*SunShadowing(p,n,dx,dy,static_sun);
}
// Forward static sun has no G-buffer occlusion; .5*2 leaves it unoccluded.
float3 SunLight(float3 p,float3 n,float h,float3 dx,float3 dy){return ForwardIndirect(h,material.w)+SunLighting(p,n,0,material.w,dx,dy,.5).rgb;}
// SoC R2 hmodel.h, combine_1.ps only. R4 normals and camera-relative positions
// are already world space, so m_v2w is identity. Squared env is R2 "contrast".
TextureCube env0:register(t4);TextureCube env1:register(t5);
float3 EnvironmentSample(float3 d){return env_color.rgb*lerp(env0.Sample(surface4,d).rgb,env1.Sample(surface5,d).rgb,env_color.w);}
void Hemisphere(out float3 diffuse,out float3 specular,float m,float h,float s,float3 relative,float3 n){
    float3 v2point=relative*rsqrt(max(dot(relative,relative),1e-12));
    float3 reflected=reflect(v2point,n);
    float2 light=MaterialResponse(h,.5+.5*dot(reflected,v2point),m);
    float3 env_d=EnvironmentSample(n);diffuse=env_d*env_d*light.x+env_ambient.rgb;
    reflected.y=reflected.y*2-1; // R2 fake remapping
    float3 env_s=EnvironmentSample(reflected);specular=env_s*env_s*light.y*s;
}
uint CubeFace(float3 d){float3 a=abs(d);if(a.x>=a.y && a.x>=a.z)return d.x>=0?0:1;
    if(a.y>=a.z)return d.y>=0?2:3;return d.z>=0?4:5;}
float LocalVisibility(float3 p,float3 dx,float3 dy){if(alpha.w<.5)return 1;uint face=light_color.w>1.5?0:CubeFace(p-light_position.xyz);
    // A fixed projected-depth subtraction scales to meters near the far plane.
    // Move the receiver by only 3 mm instead, preserving nearby blockers.
    // A 90-degree cube face (or a narrower spot) spans 2*distance over its size.
    float3 towards=light_position.xyz-p;float2 crease=ShadowCrease(dx,dy,towards/max(length(towards),.003),ShadowRiseLimit(.6,2*length(towards)/ShadowMapSize(face)));
    p=ShadowNumericalOffset(p,dx,dy,towards/max(length(towards),.003));
    p+=towards*(.003/max(length(towards),.003));
    float4 projected=mul(float4(p,1),shadow[face]);
    float4 sx=mul(float4(dx,0),shadow[face]),sy=mul(float4(dy,0),shadow[face]);
    // P0-S7: lamps use the sun's wide plane-corrected filter (each face map's
    // own size; culled faces bind a 16x16 lit map).
    return PCFWide(face,projected,sx,sy,crease);}
// R2 accum_point uses 1/(range*range); only accum_spot shortens to range*.95.
float LocalAttenuation(float distance_squared,float range,bool spot){float radius=max(spot?range*.95:range,.0001);
    return saturate(1-distance_squared/(radius*radius));}
// SPOT-only input alias: t3/shadow[1] is the independent projector. Point
// lights retain t3 as shadow face 1. The texture's alpha modulates specular.
float4 LocalProjector(float3 p){float4 projected=mul(float4(p,1),shadow[1]);
    if(projected.w<=.00001)return 0;
    float2 uv=projected.xy/projected.w*float2(.5,-.5)+.5;
    return shadow1.Sample(surface3,uv);}
float4 LocalLight(float3 p,float3 n,float gloss,float m,float3 dx,float3 dy){float3 delta=light_position.xyz-p;float distance=length(delta);
    float3 direction=delta/max(distance,.0001);float attenuation=LocalAttenuation(dot(delta,delta),light_position.w,light_color.w>1.5);
    float4 profile=1;
    if(light_color.w>1.5){float angle=dot(-direction,light_direction.xyz);
        if(sun_direction.w>.5){if(angle<light_direction.w)return 0;profile=LocalProjector(p);}
        else attenuation*=saturate((angle-light_direction.w)/max(.001,(1-light_direction.w)*.2));}
    if(attenuation<=0)return 0;
    // P0-S7: fence edges and thin boards get the G-buffer normal plane, as the sun.
    float3 shadow_p=p,shadow_dx=dx,shadow_dy=dy;ShadowNormalPlane(n,direction,shadow_p,shadow_dx,shadow_dy);
    return DirectLight(p,n,direction,light_color.rgb,gloss,m)*(attenuation*LocalVisibility(shadow_p,shadow_dx,shadow_dy))*profile;}
float3 CameraForward(){float4 n=mul(float4(0,0,0,1),inverse_vp),f=mul(float4(0,0,.5,1),inverse_vp);
    float3 d=f.xyz/f.w-n.xyz/n.w;return d*rsqrt(max(dot(d,d),1e-12));}
// P0-S4 KichRay R2 accum_sun_far SUN_FARPLANE: dynamic sunlight fades to 1/3
// by 180 m of view depth, f=(z/180)^2; it scales light, never fills a shadow.
// R2 applies it from r2_sun_near on (1.3% step there); here it is continuous.
float SunFarFade(float3 relative){float f=saturate(dot(relative,CameraForward())/180);
    return sun_color.w>.5 && sun_color.w<1.5?lerp(1,.333,f*f):1;}
float4 PSSun(Varying v):SV_Target {int2 at=int2(v.p.xy);float depth=image1.Load(int3(at,0)).r;
    float3 relative=LoadReceiverPosition(v.p.xy,depth),p=relative+camera.xyz,dx,dy;ShadowDerivatives(relative,dx,dy);
    if(depth>=1)return 0;float4 n=image0.Load(int3(at,0));ShadowDerivativesLoaded(v.p.xy,relative,dx,dy);
    // Albedo alpha (t4) is source sun occlusion: R2 static sun only, never dynamic.
    float static_sun=sun_color.w>1.5?shadow2.Load(int3(at,0)).a:0;
    float3 normal=DecodeNormal(n.xy);float visibility=SunShadowing(p,normal,dx,dy,static_sun);
    if((int)screen.z==8)return float4(visibility.xxx,0);
    // Indirect light is R2 hmodel in combine, applied once per receiver.
    return DirectLight(p,normal,-sun_direction.xyz,sun_color.rgb,MaterialGloss(n.w),MaterialClass(n.w))*visibility*SunFarFade(relative);}
float4 PSLocal(Varying v):SV_Target {int2 at=int2(v.p.xy);float depth=image1.Load(int3(at,0)).r;
    float3 relative=LoadReceiverPosition(v.p.xy,depth),p=relative+camera.xyz,dx,dy;ShadowDerivatives(relative,dx,dy);
    if(depth>=1)return 0;float4 n=image0.Load(int3(at,0));ShadowDerivativesLoaded(v.p.xy,relative,dx,dy);
    return LocalLight(p,DecodeNormal(n.xy),MaterialGloss(n.w),MaterialClass(n.w),dx,dy);}
float4 PSForwardSun(Varying v,bool front:SV_IsFrontFace):SV_Target {float3 dx,dy;ShadowDerivatives(v.w-camera.xyz,dx,dy);float4 c=Base(v);
    float4 light=SunLighting(v.w,Normal(v,front),Gloss(v),material.w,dx,dy,.5)+float4(ForwardIndirect(v.n.w,material.w),0);
    return float4(ForwardFog(c.rgb*light.rgb+light.aaa,FogTransmittance(v.w)),c.a);}
float4 PSForwardLocal(Varying v,bool front:SV_IsFrontFace):SV_Target {float3 dx,dy;ShadowDerivatives(v.w-camera.xyz,dx,dy);float4 c=Base(v);
    float4 light=LocalLight(v.w,Normal(v,front),Gloss(v),material.w,dx,dy);
    return float4((c.rgb*light.rgb+light.aaa)*(alpha.z>0?c.a:1)*FogTransmittance(v.w),0);}
// P0-C2 R2 common.h tonemap(): low=rgb*scale (generic0), high=low/def_hdr
// (generic1, the bloom source). Exposure scale is 1 with r2_tonemap off.
struct ToneOutput {float4 low:SV_Target0;float4 high:SV_Target1;};
// P0-C1 s_tonemap: the 1x1 R32F exposure result at t7 when light_color.w is
// set (native sky/combine/clouds); other producers keep scale 1.
float Exposure(){return light_color.w>.5?shadow5.Load(int3(0,0,0)).r:1;}
ToneOutput Tonemap(float4 c,float scale){ToneOutput o;c.rgb*=scale;o.low=c;o.high=float4(c.rgb/8,c.a);return o;}
// combine: image0 albedo, image1 light; shadow0 normal; shadow1 depth.
float4 CombineColor(Varying v){int2 at=int2(v.p.xy);float depth=shadow1.Load(int3(at,0)).r;
    if(depth>=1 && alpha.w>19.5)discard;
    float4 result=float4(fog.rgb,1);
    [branch] if(depth<1){float4 a=image0.Load(int3(at,0)),n=shadow0.Load(int3(at,0));
        float4 light=image1.Load(int3(at,0));int mode=(int)screen.z;
        if(mode==1 || mode==6 || mode==9)result=float4(a.rgb,1);
        else if(mode==2)result=float4(DecodeNormal(n.xy)*.5+.5,1);
        else if(mode==3)result=float4(saturate(length(LoadReceiverPosition(v.p.xy,depth))/camera.w).xxx,1);
        else if(mode==4 || mode==8)result=float4(light.rgb+light.aaa,1);
        else if(mode==5)result=float4(n.zzz,1);
        else if(mode==7)result=float4(a.aaa,1);
        else{float3 relative=LoadReceiverPosition(v.p.xy,depth),hdiffuse,hspecular;
            // combine_1.ps: D*(L.rgb+hdiffuse, L.w) + hspecular; RGB env specular is
            // never folded into the scalar accumulator alpha.
            Hemisphere(hdiffuse,hspecular,MaterialClass(n.w),n.z,MaterialGloss(n.w),relative,DecodeNormal(n.xy));
            float amount=FogAmount(length(relative));
            result=float4(lerp(a.rgb*(light.rgb+hdiffuse)+light.aaa+hspecular,fog.rgb,amount),amount*amount);}}
    return result;}
// P0-S5: triangular +-1 LSB dither (two interleaved-gradient-noise terms)
// before 8-bit targets, so smooth dark gradients do not quantize into bands.
float InterleavedNoise(float2 p){return frac(52.9829189*frac(dot(p,float2(.06711056,.00583715))));}
float3 Dither(float3 c,float2 pixel){return c+(InterleavedNoise(pixel)+InterleavedNoise(pixel+float2(47,17))-1)/255;}
// Both targets share combine_1's fog alpha blend over the sky (R2 MRT).
// Out parameters: FXC cannot return a struct from a shader that discards.
void PSCombine(Varying v,out float4 low:SV_Target0,out float4 high:SV_Target1){
    float4 c=CombineColor(v);ToneOutput o=Tonemap(c,Exposure());low=o.low;high=o.high;
    if((int)screen.z==0)low.rgb=Dither(low.rgb,v.p.xy);}

// Native environment entries use the engine's two weather cubemaps.
TextureCube sky0:register(t0);TextureCube sky1:register(t1);
// P0-S5 DX11 debanding. Weather sky/cloud art is DXT (5:6:5 endpoints per 4x4
// block); a dark night sky multiplied by the adapted exposure turns those
// steps into visible terraces. Smooth regions (every tap within 0.04 of the
// centre) average 9 taps over ~1.5 blocks; cloud edges keep the centre. The
// 8-bit targets then receive triangular +-1 LSB dither (Dither()).
float3 SkyTexel(float3 d,float w){return lerp(sky0.Sample(surface,d).rgb,sky1.Sample(surface1,d).rgb,w);}
float3 SkyDebanded(float3 d,float w,float2 pixel){
    d*=rsqrt(max(dot(d,d),1e-12));float3 center=SkyTexel(d,w),sum=center;
    uint size,height;sky0.GetDimensions(size,height);float radius=6*1.5707963/max(float(size),1);
    float3 t=normalize(cross(abs(d.y)<.99?float3(0,1,0):float3(1,0,0),d)),b=cross(d,t);
    float angle=InterleavedNoise(pixel)*6.2831853,difference=0;
    [unroll]for(int i=0;i<8;++i){float r=radius*sqrt((i+.5)/8),a=angle+i*2.3999632;
        float3 s=SkyTexel(d+(t*cos(a)+b*sin(a))*r,w),e=abs(s-center);
        difference=max(difference,max(e.r,max(e.g,e.b)));sum+=s;}
    return difference<=.04?sum/9:center;}
float3 TextureDebanded(Texture2D map,SamplerState state,float2 uv,float2 pixel){
    float3 center=map.Sample(state,uv).rgb,sum=center;uint w,h;map.GetDimensions(w,h);
    float2 radius=6/max(float2(w,h),1);float angle=InterleavedNoise(pixel)*6.2831853,difference=0;
    [unroll]for(int i=0;i<8;++i){float r=sqrt((i+.5)/8),a=angle+i*2.3999632;
        float3 s=map.Sample(state,uv+float2(cos(a),sin(a))*radius*r).rgb,e=abs(s-center);
        difference=max(difference,max(e.r,max(e.g,e.b)));sum+=s;}
    return difference<=.04?sum/9:center;}
// R2 sky2.ps: tonemap(low, high, sky, scale*2) into generic0/generic1.
ToneOutput PSSky(Varying v){ToneOutput o=Tonemap(float4(SkyDebanded(v.n.xyz,v.c.a,v.p.xy)*v.c.rgb*2,1),Exposure());
    o.low.rgb=Dither(o.low.rgb,v.p.xy);o.high.rgb=Dither(o.high.rgb,v.p.xy+float2(13,29));return o;}
// R2 clouds.ps: tonemap(rgb, hi, mix, scale) into generic0 only.
float4 PSClouds(Varying v):SV_Target {
    float3 c=(TextureDebanded(image0,surface,v.uv,v.p.xy)+TextureDebanded(image1,surface1,v.n.xy,v.p.xy+float2(31,7)))*v.c.rgb*Exposure();
    return float4(Dither(c,v.p.xy),v.c.a);}
float4 PSWater(Varying v,bool front:SV_IsFrontFace):SV_Target {
    float3 dx,dy;ShadowDerivatives(v.w-camera.xyz,dx,dy);
    float t=light_position.w;float2 flow=v.uv+float2(t*.015,t*.01);
    float4 water=image0.Sample(surface,flow);float2 wave=(water.rg-.5)*.012;
    float2 screen_uv=v.p.xy/screen.xy;float3 behind=image1.Sample(surface1,saturate(screen_uv+wave)).rgb;
    float3 n=Normal(v,front);float fresnel=pow(1-saturate(dot(n,normalize(camera.xyz-v.w))),5);
    float transmittance=FogTransmittance(v.w);
    float3 lit=ForwardFog(water.rgb*SunLight(v.w,n,saturate(v.n.w),dx,dy),transmittance);
    float3 reflected=ForwardFog(hemi.rgb,transmittance);
    // The sampled opaque background already contains combine fog. Fog only
    // the water's own light/reflection, then use R2 soft-water distance fade.
    return float4(lerp(lerp(behind,lit,.35),reflected,fresnel*.5),.8*transmittance*transmittance);
}
float4 PSDistortion(Varying v):SV_Target {float4 mask=Base(v);bool menu=alpha.w>29.5 && alpha.w<30.5;
    // Match R2 distort.ps: the menu mask is alpha-blended over neutral 0.5.
    float2 offset=(mask.rg-.5)*(menu?.05:.015)*mask.a;
    return float4(image1.Sample(surface1,saturate(v.p.xy/max(screen.xy,1)+offset)).rgb,menu?1:mask.a);
}
// G3b mapDistort equivalent. The source texture is a displacement field, not
// a flame image: vertex RGB modulates strength only (X-Ray 1.6 particle_distort).
float4 PSParticleDistortionMask(Varying v):SV_Target {
    float4 field=image0.Sample(surface,v.uv);
    return float4(field.rgb,saturate(field.a*dot(v.c.rgb,float3(.33,.33,.33))));
}
float4 PSDistortionComposite(Varying v):SV_Target {
    // RGBA16F preserves neutral .5 exactly; the source engine used 127/255 in RGBA8.
    float2 displacement=(image1.Sample(surface1,v.uv).rg-.5)*.05;
    return float4(image0.Sample(surface,saturate(v.uv+displacement)).rgb,1);
}

// P0-C2 R2 phase_bloom. Bloom targets are 256^2. D3D9 pixel centres are
// integer, so R2 texcoords are a+x/256; floor(SV_Position) reproduces them.
// kich_graphics_math.h, KichRay r2_bloom_mode 1 soft-knee alpha.
float KichBloomAlpha(float brightness,float threshold,float knee_ratio){
    float knee=max(threshold*knee_ratio,.0001);
    float soft=min(max(brightness-threshold+knee,0),2*knee);soft=soft*soft/(4*knee);
    return max(max(brightness-threshold,soft),0);
}
// bloom_build.ps (+ kich_bloom_build.ps): four bilinear taps of generic1 spaced
// (w/2)/256 screen pixels, rgb=sum/2, a=dot(rgb,1)-threshold; RGBA8 clamps.
// light_position: threshold, KichRay mode, strength, knee ratio.
float4 PSBloomBuild(Varying v):SV_Target {
    float2 a=floor(v.p.xy)/256+.5/screen.xy;const float one=1.0/512;
    float3 s0=image0.SampleLevel(surface,a,0).rgb,s1=image0.SampleLevel(surface,a+float2(one,0),0).rgb;
    float3 s2=image0.SampleLevel(surface,a+float2(0,one),0).rgb,s3=image0.SampleLevel(surface,a+float2(one,one),0).rgb;
    float3 avg=(s0+s1+s2+s3)/2;float4 result=float4(avg,dot(avg,1)-light_position.x);
    [branch] if(light_position.y>.5){result.a=KichBloomAlpha(dot(result.rgb,1),light_position.x,light_position.w);result.rgb*=light_position.z;}
    return result;
}
// bloom_filter.ps: separable 2*7+1 taps, each bilinear across two texels at
// +-(2k-.5) texels. light_position/light_direction: R2 weight[0]/weight[1]
// (CalcGauss_wave); light_color.xy: filter axis.
float4 PSBloomFilter(Varying v):SV_Target {
    float2 center=(floor(v.p.xy)+.5)/256,axis=light_color.xy/256;
    float weights[7]={light_position.x,light_position.y,light_position.z,light_position.w,light_direction.x,light_direction.y,light_direction.z};
    float4 accum=light_direction.w*image0.SampleLevel(surface,center,0);
    [unroll] for(int k=1;k<=7;++k){float2 o=axis*(2*k-.5);
        accum+=weights[k-1]*(image0.SampleLevel(surface,center-o,0)+image0.SampleLevel(surface,center+o,0));}
    return accum;
}
// bloom_filter_f.ps (r2_ls_bloom_fast): four diagonal taps at +-kernel_b texels, sum/2.
float4 PSBloomFilterFast(Varying v):SV_Target {
    float2 center=(floor(v.p.xy)+.5)/256;float d=light_position.x/256;
    return (image0.SampleLevel(surface,center+float2(-d,-d),0)+image0.SampleLevel(surface,center+float2(d,d),0)+
        image0.SampleLevel(surface,center+float2(d,-d),0)+image0.SampleLevel(surface,center+float2(-d,d),0))/2;
}
// P0-C1 R2 phase_luminance. bloom_luminance_1.ps, 256^2 -> 64^2: four
// bilinear 2x2 averages per texel at (4x+{1,3})/256, luminance*def_hdr.
float4 PSLuminance1(Varying v):SV_Target {
    float2 a=floor(v.p.xy)*4/256;const float3 weights=float3(.3,.48,.22)*8;
    return float4(dot(image0.SampleLevel(surface,a+float2(1,1)/256,0).rgb,weights),dot(image0.SampleLevel(surface,a+float2(3,1)/256,0).rgb,weights),
        dot(image0.SampleLevel(surface,a+float2(1,3)/256,0).rgb,weights),dot(image0.SampleLevel(surface,a+float2(3,3)/256,0).rgb,weights));
}
// bloom_luminance_2/3.ps: 16 bilinear samples of a source block, each the mean
// of four components; xyzw are the means of samples 0-3, 4-7, 8-11, 12-15.
float4 LuminanceBlock(float2 pixel,float source){
    float2 origin=floor(pixel)*8;float4 final=0;
    [unroll] for(int k=0;k<16;++k){float2 offset=float2((k*2+1)%8,(k/4)*2+1);
        float value=dot(image0.SampleLevel(surface,(origin+offset)/source,0),.25);
        final[k/4]+=value*.25;}
    return final;
}
float4 PSLuminance2(Varying v):SV_Target {return LuminanceBlock(v.p.xy,64);}
// 8^2 -> 1x1: scale = MiddleGray.x/(lum*MiddleGray.y+MiddleGray.z), lerped
// with last frame's scale (image1) by the adaptation factor. R2's
// clamp(rvalue,1/8,2) discards its result, so no clamp is applied.
float4 PSLuminance3(Varying v):SV_Target {
    float result=dot(LuminanceBlock(v.p.xy,8),.25);
    float scale=light_position.x/(result*light_position.y+light_position.z);
    return lerp(image1.Load(int3(0,0,0)).r,scale,light_position.w).xxxx;
}
// P0-S4: R2 renders HUD models into the G-buffer, so combine writes them into
// generic1 (high = low/8) and they occlude the sky in the bloom source. R4
// draws the HUD after combine; copy its pixels (image1 HUD-only depth < 1)
// from the final colour into high_color before the bloom build.
float4 PSHudBloomSource(Varying v):SV_Target {int2 at=int2(v.p.xy);
    if(image1.Load(int3(at,0)).r>=1)discard;
    float4 c=image0.Load(int3(at,0));return float4(c.rgb/8,c.a);
}
// combine_2_naa.ps combine_bloom: low + bloom.rgb*bloom.a, bloom sampled at the pixel centre.
float4 PSBloomComposite(Varying v):SV_Target {
    float4 bloom=image1.SampleLevel(surface1,v.p.xy/screen.xy,0);
    return float4(image0.Load(int3(int2(v.p.xy),0)).rgb+bloom.rgb*bloom.a,1);
}

float3 PostprocessNoise(float3 color,float3 noise,float amount){return lerp(color,color*noise*2,amount);}
float4 PSPostprocess(Varying v):SV_Target {
    float2 split=light_position.yz*.5;float2 uv=v.uv;
    float3 c=(image0.Sample(surface,saturate(uv+split)).rgb+image0.Sample(surface,saturate(uv-split)).rgb)*.5;
    if(light_position.x>0){float2 step=light_position.x*3/screen.xy;c=(c*4+image0.Sample(surface,saturate(uv+float2(step.x,0))).rgb+
        image0.Sample(surface,saturate(uv-float2(step.x,0))).rgb+image0.Sample(surface,saturate(uv+float2(0,step.y))).rgb+image0.Sample(surface,saturate(uv-float2(0,step.y))).rgb)/8;}
    float3 gray=dot(c,hemi.rgb).xxx;c=lerp(c,gray,saturate(alpha.w));
    // R2/1.6 texture noise modulates the scene before base/add color; black
    // stays black. Noise repeats while the independent scene sampler clamps.
    [branch] if(light_position.w>0){float3 noise=image1.Sample(surface1,light_direction.xy+v.uv*light_direction.zw).rgb;
        c=PostprocessNoise(c,noise,light_position.w);}
    c=c*sun_color.rgb*2+ambient.rgb;
    float gamma=max(.01,alpha.x),brightness=alpha.y*.5,contrast=alpha.z*.5;
    c=((contrast+.5)*pow(saturate(c),1/gamma)+(brightness-.5)*.5-contrast*.5+.25)*fog.rgb;
    return float4(saturate(Dither(c,v.p.xy)),1);
}

// P0-S5 DX11 volumetric sun shafts (r4_sun_shafts 0..3, r4_sun_shafts_intensity).
// The scattering model is KichRay R2 accum_volumetric_sun.ps: two view-depth
// segments (0..r2_sun_near, r2_sun_near..min(180 m, far plane)), extinction
// weights exp(-0.006 d) per bin, (cos)^4 phase, x0.45. DX11 differences: every
// full-size pixel marches its own path, so thin foliage in front of the sky
// keeps its shorter path (R2 upsamples a half-size target and leaves dark
// holes); shadow-space positions advance by one affine step per cascade; each
// sample takes the finest cascade containing it (contact t7, 128 m t2, 512 m
// t3); the R2 cloud sunmask (t6) breaks the beams; a depth-aware separable
// blur removes the per-pixel jitter pattern. A volume point is not a surface:
// one comparison, no receiver bias, and no map means no contribution.
float ShaftCompare(Texture2D map,float4 q,inout bool done){float result=0;
    [branch] if(!done && all(abs(q.xy)<1) && q.z>=0 && q.z<=1){result=map.SampleCmpLevelZero(shadow_surface,float2(q.x*.5+.5,.5-q.y*.5),q.z);done=true;}
    return result;}
float ShaftSample(float4 contact,float4 near_q,float4 far_q,bool use_contact){bool done=false;float result=0;
    [branch] if(use_contact)result=ShaftCompare(shadow5,contact,done);
    result+=ShaftCompare(shadow0,near_q,done);
    result+=ShaftCompare(shadow1,far_q,done);
    return result;}
float ShaftSegment(float3 ray,float cos_z,float scene,float near_z,float far_z,float samples,float jitter){
    float start=max(.05,near_z/cos_z),end=min(min(scene,180),far_z/cos_z),span=max(0,end-start);
    float stride=span/samples,transmission=exp(-.006*start),step_transmission=exp(-.006*stride),visibility=0;
    [branch] if(span>.001){
        float4 origin=float4(camera.xyz+ray*start,1),delta=float4(ray*stride,0);
        float4 c0=mul(origin,shadow[2]),d0=mul(delta,shadow[2]),c1=mul(origin,shadow[0]),d1=mul(delta,shadow[0]);
        float4 c2=mul(origin,shadow[1]),d2=mul(delta,shadow[1]);
        float2 m0=float2(dot(origin,sunmask_u),dot(origin,sunmask_v)),md=float2(dot(delta,sunmask_u),dot(delta,sunmask_v));
        bool use_contact=shadow[2][3][3]>.5;
        [loop]for(int i=0;i<16;++i){[branch] if(i<samples){
            float t=float(i)+frac(jitter+float(i)*.61803398875);
            float lit=ShaftSample(c0+d0*t,c1+d1*t,c2+d2*t,use_contact)*shadow4.SampleLevel(surface6,frac(m0+md*t),0).a;
            visibility+=lit*transmission*(1-step_transmission);transmission*=step_transmission;}}}
    return visibility;}
// Full size, RGBA16F. light_position: samples per segment, intensity,
// r2_sun_near, far limit. image0 is the D24 depth. Alpha keeps the path
// length/180 for the blur.
float4 PSSunShafts(Varying v):SV_Target {
    int2 at=int2(v.p.xy);float2 pixel=float2(at)+.5;float depth=image0.Load(int3(at,0)).r;
    float3 ray=RelativePosition(pixel,.5);ray*=rsqrt(max(dot(ray,ray),1e-12));
    float scene=180;
    [branch] if(depth<1)scene=min(length(LoadReceiverPosition(pixel,depth)),180);
    float cos_z=max(dot(ray,CameraForward()),.01);
    float alignment=saturate((dot(ray,-sun_direction.xyz)+.05)/1.05),phase=alignment*alignment*alignment*alignment;
    float samples=clamp(light_position.x,1,16);
    float jitter=frac(52.9829189*frac(dot(float2(at),float2(.06711056,.00583715))));
    float visibility=0;
    [branch] if(phase>.0001)visibility=ShaftSegment(ray,cos_z,scene,0,light_position.z,samples,jitter)+
        ShaftSegment(ray,cos_z,scene,light_position.z,light_position.w,samples,jitter);
    return float4(max(sun_color.rgb,0)*visibility*phase*light_position.y*.45,scene/180);
}
// Depth-aware separable blur: 9 taps, Gaussian sigma 3 px along
// light_position.xy; a tap whose path length differs (tree vs sky) is
// rejected, so beams never bleed across silhouettes.
float4 PSSunShaftsBlur(Varying v):SV_Target {int2 at=int2(v.p.xy),limit=int2(screen.xy)-1,axis=int2(light_position.xy);
    float4 center=image0.Load(int3(at,0));float d0=center.a*180,tolerance=max(.05*d0,.5);
    float3 sum=center.rgb;float weight=1;
    [unroll]for(int i=1;i<=4;++i){float g=exp(-float(i*i)/18.0);
        [unroll]for(int s=-1;s<=1;s+=2){float4 t=image0.Load(int3(clamp(at+axis*(i*s),int2(0,0),limit),0));
            float r=(t.a*180-d0)/tolerance,w=g*exp(-r*r);sum+=t.rgb*w;weight+=w;}}
    return float4(sum/weight,center.a);
}
// R2 phase_combine_sun_shafts: additive into the low colour after combine,
// before clouds and forward; not part of the bloom source.
float4 PSSunShaftsComposite(Varying v):SV_Target {return float4(image0.Load(int3(int2(v.p.xy),0)).rgb,0);}