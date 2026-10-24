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
// vertex baked RGB=16, source sun=32, tree light scale/bias=64, emissive=256, authored wallmark=512.
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
    return front?n:-n;}
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
float SunVisibility(float3 p,float3 dx,float3 dy){
    if(sun_color.w<=0 || sun_color.w>1.5)return 0;
    // Precision allowance only: plane correction handles the filter footprint.
    p=ShadowNumericalOffset(p,dx,dy,-sun_direction.xyz)-sun_direction.xyz*.01;
    float4 near_position=mul(float4(p,1),shadow[0]);
    float3 q=near_position.xyz/near_position.w;
    float edge=max(abs(q.x),abs(q.y));
    [branch] if(q.z<0 || q.z>1 || edge>=1)return PCF(shadow1,mul(float4(p,1),shadow[1]),mul(float4(dx,0),shadow[1]),mul(float4(dy,0),shadow[1]));
    float near_visibility=PCF(shadow0,near_position,mul(float4(dx,0),shadow[0]),mul(float4(dy,0),shadow[0]));
    [branch] if(edge<=.9)return near_visibility;
    float far_visibility=PCF(shadow1,mul(float4(p,1),shadow[1]),mul(float4(dx,0),shadow[1]),mul(float4(dy,0),shadow[1]));
    return lerp(near_visibility,far_visibility,saturate((edge-.9)*10));
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
float4 SunLighting(float3 p,float3 n,float gloss,float m,float3 dx,float3 dy,float static_sun){
    // R2/1.6 use baked/ROS sun occlusion only in static-sun mode, where the
    // dynamic sun pass is disabled. Here the current shadow maps own visibility;
    // a baked zero must not suppress sunlight and moving shadows on a receiver.
    return DirectLight(p,n,-sun_direction.xyz,sun_color.rgb,gloss,m)*SunDirect(p,dx,dy,static_sun);
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
    float3 towards=light_position.xyz-p;
    p=ShadowNumericalOffset(p,dx,dy,towards/max(length(towards),.003));
    p+=towards*(.003/max(length(towards),.003));
    float4 projected=mul(float4(p,1),shadow[face]);
    float4 sx=mul(float4(dx,0),shadow[face]),sy=mul(float4(dy,0),shadow[face]);
    if(face==0)return PCF(shadow0,projected,sx,sy);if(face==1)return PCF(shadow1,projected,sx,sy);
    if(face==2)return PCF(shadow2,projected,sx,sy);if(face==3)return PCF(shadow3,projected,sx,sy);
    if(face==4)return PCF(shadow4,projected,sx,sy);return PCF(shadow5,projected,sx,sy);}
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
    return DirectLight(p,n,direction,light_color.rgb,gloss,m)*(attenuation*LocalVisibility(p,dx,dy))*profile;}
float4 PSSun(Varying v):SV_Target {int2 at=int2(v.p.xy);float depth=image1.Load(int3(at,0)).r;
    float3 relative=LoadReceiverPosition(v.p.xy,depth),p=relative+camera.xyz,dx,dy;ShadowDerivatives(relative,dx,dy);
    if(depth>=1)return 0;float4 n=image0.Load(int3(at,0));
    // Albedo alpha (t4) is source sun occlusion: R2 static sun only, never dynamic.
    float static_sun=sun_color.w>1.5?shadow2.Load(int3(at,0)).a:0;
    if((int)screen.z==8)return float4(SunDirect(p,dx,dy,static_sun).xxx,0);
    // Indirect light is R2 hmodel in combine, applied once per receiver.
    return SunLighting(p,DecodeNormal(n.xy),MaterialGloss(n.w),MaterialClass(n.w),dx,dy,static_sun);}
float4 PSLocal(Varying v):SV_Target {int2 at=int2(v.p.xy);float depth=image1.Load(int3(at,0)).r;
    float3 relative=LoadReceiverPosition(v.p.xy,depth),p=relative+camera.xyz,dx,dy;ShadowDerivatives(relative,dx,dy);
    if(depth>=1)return 0;float4 n=image0.Load(int3(at,0));
    return LocalLight(p,DecodeNormal(n.xy),MaterialGloss(n.w),MaterialClass(n.w),dx,dy);}
float4 PSForwardSun(Varying v,bool front:SV_IsFrontFace):SV_Target {float3 dx,dy;ShadowDerivatives(v.w-camera.xyz,dx,dy);float4 c=Base(v);
    float4 light=SunLighting(v.w,Normal(v,front),Gloss(v),material.w,dx,dy,.5)+float4(ForwardIndirect(v.n.w,material.w),0);
    return float4(ForwardFog(c.rgb*light.rgb+light.aaa,FogTransmittance(v.w)),c.a);}
float4 PSForwardLocal(Varying v,bool front:SV_IsFrontFace):SV_Target {float3 dx,dy;ShadowDerivatives(v.w-camera.xyz,dx,dy);float4 c=Base(v);
    float4 light=LocalLight(v.w,Normal(v,front),Gloss(v),material.w,dx,dy);
    return float4((c.rgb*light.rgb+light.aaa)*(alpha.z>0?c.a:1)*FogTransmittance(v.w),0);}
// combine: image0 albedo, image1 light; shadow0 normal; shadow1 depth.
float4 PSCombine(Varying v):SV_Target {int2 at=int2(v.p.xy);float depth=shadow1.Load(int3(at,0)).r;
    if(depth>=1){if(alpha.w>19.5)discard;return float4(fog.rgb,1);}float4 a=image0.Load(int3(at,0)),n=shadow0.Load(int3(at,0));
    float4 light=image1.Load(int3(at,0));int mode=(int)screen.z;
    if(mode==1)return float4(a.rgb,1);if(mode==2)return float4(DecodeNormal(n.xy)*.5+.5,1);
    if(mode==3)return float4(saturate(length(LoadReceiverPosition(v.p.xy,depth))/camera.w).xxx,1);
    if(mode==4 || mode==8)return float4(light.rgb+light.aaa,1);if(mode==5)return float4(n.zzz,1);
    if(mode==6 || mode==9)return float4(a.rgb,1);if(mode==7)return float4(a.aaa,1);
    float3 relative=LoadReceiverPosition(v.p.xy,depth),hdiffuse,hspecular;
    // combine_1.ps: D*(L.rgb+hdiffuse, L.w) + hspecular; RGB env specular is
    // never folded into the scalar accumulator alpha.
    Hemisphere(hdiffuse,hspecular,MaterialClass(n.w),n.z,MaterialGloss(n.w),relative,DecodeNormal(n.xy));
    float amount=FogAmount(length(relative));
    return float4(lerp(a.rgb*(light.rgb+hdiffuse)+light.aaa+hspecular,fog.rgb,amount),amount*amount);}

// Native environment entries use the engine's two weather cubemaps.
TextureCube sky0:register(t0);TextureCube sky1:register(t1);
float4 PSSky(Varying v):SV_Target {float3 d=v.n.xyz;return float4(lerp(sky0.Sample(surface,d).rgb,sky1.Sample(surface1,d).rgb,v.c.a)*v.c.rgb*2,1);}
float4 PSClouds(Varying v):SV_Target {return float4((image0.Sample(surface,v.uv).rgb+image1.Sample(surface1,v.n.xy).rgb)*v.c.rgb,v.c.a);}
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
    return float4(saturate(c),1);
}
