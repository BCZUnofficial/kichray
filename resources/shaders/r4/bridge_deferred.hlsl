// G3c native deferred materials. DDS channels remain UNORM; no implicit sRGB decode.
cbuffer Frame : register(b0) {
    row_major float4x4 world,mvp,normal_matrix,inverse_vp,shadow[6];
    float4 camera,screen,sun_direction,sun_color,ambient,hemi,fog;
    float4 light_position,light_direction,light_color,alpha;
    float4 material,color_scale,color_bias,sun_scale_bias,tree_wave,tree_wind;
};
Texture2D image0:register(t0); Texture2D image1:register(t1);
Texture2D shadow0:register(t2); Texture2D shadow1:register(t3);
Texture2D shadow2:register(t4); Texture2D shadow3:register(t5);
Texture2D shadow4:register(t6); Texture2D shadow5:register(t7);
SamplerState surface:register(s0); SamplerComparisonState shadow_surface:register(s1);
// Material flags: bump=1, baked lightmap=2, hemi map=4, detail=8,
// vertex baked RGB=16, source sun=32, tree light scale/bias=64.
struct StaticInput {float3 p:POSITION;float2 uv:TEXCOORD0;float4 n:NORMAL;
    float3 t:TEXCOORD1;float3 b:TEXCOORD2;float3 uv_sun:TEXCOORD3;float4 light:COLOR0;float2 tree:TEXCOORD4;};
struct ModelInput {float4 p:POSITION;float2 uv:TEXCOORD0;float4 c:COLOR0;float4 n:NORMAL;float3 w:TEXCOORD1;};
struct ScreenInput {float3 p:POSITION;float2 uv:TEXCOORD0;};
struct Varying {float4 p:SV_Position;float2 uv:TEXCOORD0;float4 c:COLOR0;float4 n:TEXCOORD1;float3 w:TEXCOORD2;
    float3 t:TEXCOORD3;float3 b:TEXCOORD4;float2 uv1:TEXCOORD5;float4 baked_sun:TEXCOORD6;};
Varying VSStatic(StaticInput v){Varying o=(Varying)0;o.uv=v.uv;o.c=1;
    float3 position=mul(float4(v.p,1),world).xyz;
    if(((uint)material.x&128)!=0){float f=2.8284271*frac(dot(float4(position,1),tree_wave))-1.4142136;
        position.xz+=tree_wind.xz*((position.y-world[3].y)*(f*f-1)*v.tree.y);}
    o.p=mul(float4(position,1),mvp);
    o.n=float4(mul(float4(v.n.xyz,0),normal_matrix).xyz,v.n.w);o.w=position;
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
float4 Base(Varying v){float4 c=image0.Sample(surface,v.uv)*v.c;
    if(((uint)material.x&8)!=0)c.rgb*=shadow3.Sample(surface,v.uv*material.yz).rgb*2;
    if(alpha.w>9.5 && alpha.w<10.5){float4 mask=shadow0.Sample(surface,v.uv);mask/=max(dot(mask,1),.0001);
        float2 tc=v.uv*50;float3 detail=shadow1.Sample(surface,tc).rgb*mask.r+shadow2.Sample(surface,tc).rgb*mask.g+
            shadow3.Sample(surface,tc).rgb*mask.b+shadow4.Sample(surface,tc).rgb*mask.a;c.rgb*=detail*2;}
    float a=c.a,r=alpha.x;int f=(int)alpha.y;
    bool keep=f==8 || (f==2 && a<r) || (f==3 && a==r) || (f==4 && a<=r) ||
        (f==5 && a>r) || (f==6 && a!=r) || (f==7 && a>=r);
    if(!keep)discard;return c;}
float3 Normal(Varying v,bool front){float3 n=v.n.xyz;
    if(dot(n,n)<.05)n=cross(ddy(v.w),ddx(v.w));
    n=dot(n,n)>.000001?normalize(n):float3(0,1,0);
    float3 t=v.t-n*dot(n,v.t);float handed=dot(cross(n,t),v.b);
    if(((uint)material.x&1)!=0 && dot(t,t)>1e-8 && abs(handed)>1e-8){
        t=normalize(t);float3 b=normalize(cross(n,t))*(handed<0?-1:1);
        // SoC R2 sload: bump stores gloss,x-normal in alpha,y in B,z in G;
        // bump# stores signed reconstruction error in RGB, height in alpha.
        float4 Nu=image1.Sample(surface,v.uv),NuE=shadow0.Sample(surface,v.uv);
        float3 mapped=Nu.wzy+NuE.xyz-1;mapped.z*=.5;
        float3 result=t*mapped.x+b*mapped.y+n*mapped.z;
        if(dot(result,result)>1e-8)n=normalize(result);
    }
    // Flip the complete mapped normal on the back face, preserving mirrored UV handedness.
    return front?n:-n;}
struct GeometryOutput {float4 albedo:SV_Target0;float4 normal:SV_Target1;float4 baked:SV_Target2;};
GeometryOutput PSGeometry(Varying v,bool front:SV_IsFrontFace){GeometryOutput o;
    uint flags=(uint)material.x;float h=v.n.w,s=(flags&32)!=0?v.baked_sun.w:.5;float3 baked=0;
    if((flags&16)!=0)baked=v.baked_sun.rgb;
    // Compatibility irradiance: keep R1/XRLC lmap0 RGB as a separate raw
    // additive baked term. This is not R2 material-LUT/specular parity.
    if((flags&2)!=0){float4 lm=shadow1.Sample(surface,v.uv1);baked+=lm.rgb;s=lm.a;}
    if((flags&4)!=0){float4 lm=shadow2.Sample(surface,v.uv1);h=dot(lm.rgb,float3(1.0/3,1.0/3,1.0/3));s=lm.a;}
    o.albedo=float4(Base(v).rgb,saturate(s));o.normal=float4(Normal(v,front),saturate(h));o.baked=float4(baked,0);
    // Diagnostics keep separate components before accumulation, with no new MRT.
    if((int)screen.z==6)o.albedo.rgb=baked;
    if((int)screen.z==9){float3 n=v.n.xyz;if(dot(n,n)<.05)n=cross(ddy(v.w),ddx(v.w));
        n=dot(n,n)>1e-6?normalize(n):float3(0,1,0);o.albedo.rgb=(front?n:-n)*.5+.5;}
    if((int)screen.z==8)o.baked=0;
    return o;}
void PSShadow(Varying v){Base(v);}
float4 PSPlain(Varying v):SV_Target {return Base(v);}
// Source-only sky mask: integer pixel loads cannot generate a diagonal from
// far-plane depth interpolation. The same depth predicate is used by combine.
float4 PSCelestial(Varying v,uint primitive_id:SV_PrimitiveID):SV_Target {
    int mode=(int)screen.z;
    if(mode!=11 && mode!=12 && image1.Load(int3(int2(v.p.xy),0)).r<1)discard;
    if(mode==12)return (primitive_id&1)==0?float4(1,0,0,1):float4(0,1,0,1);
    return Base(v);
}
float3 WorldPosition(float2 pixel,float depth){float4 p=float4(pixel.x*2/screen.x-1,1-pixel.y*2/screen.y,depth,1);
    p=mul(p,inverse_vp);return p.xyz/p.w;}
float PCF(Texture2D map,float4 projected){if(projected.w<=0)return 0;
    float3 q=projected.xyz/projected.w;float2 uv=float2(q.x*.5+.5,.5-q.y*.5);
    if(any(uv<0) || any(uv>1) || q.z<0 || q.z>1)return 0;
    float result=0;
    [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x)
        result+=map.SampleCmpLevelZero(shadow_surface,uv+float2(x,y)/screen.w,q.z-.0015);
    return result/9;
}
float SunVisibility(float3 p){return sun_color.w>0?PCF(shadow0,mul(float4(p,1),shadow[0])):0;}
float3 SunLight(float3 p,float3 n,float h,float sun_mask){float sky=saturate(n.y*.5+.5)*h;
    float3 base=ambient.rgb+hemi.rgb*sky;
    return base+sun_color.rgb*saturate(dot(n,-sun_direction.xyz))*SunVisibility(p)*sun_mask;}
uint CubeFace(float3 d){float3 a=abs(d);if(a.x>=a.y && a.x>=a.z)return d.x>=0?0:1;
    if(a.y>=a.z)return d.y>=0?2:3;return d.z>=0?4:5;}
float LocalVisibility(float3 p){if(alpha.w<.5)return 1;uint face=light_color.w>1.5?0:CubeFace(p-light_position.xyz);
    float4 projected=mul(float4(p,1),shadow[face]);
    if(face==0)return PCF(shadow0,projected);if(face==1)return PCF(shadow1,projected);
    if(face==2)return PCF(shadow2,projected);if(face==3)return PCF(shadow3,projected);
    if(face==4)return PCF(shadow4,projected);return PCF(shadow5,projected);}
float3 LocalLight(float3 p,float3 n){float3 delta=light_position.xyz-p;float distance=length(delta);
    float3 direction=delta/max(distance,.0001);float attenuation=saturate(1-distance/light_position.w);attenuation*=attenuation;
    if(light_color.w>1.5){float angle=dot(-direction,light_direction.xyz);
        attenuation*=saturate((angle-light_direction.w)/max(.001,(1-light_direction.w)*.2));}
    if(attenuation<=0)return 0;
    return light_color.rgb*(saturate(dot(n,direction))*attenuation*LocalVisibility(p));}
float4 PSSun(Varying v):SV_Target {int2 at=int2(v.p.xy);float depth=image1.Load(int3(at,0)).r;
    if(depth>=1)return 0;float4 n=image0.Load(int3(at,0));float3 p=WorldPosition(v.p.xy,depth);
    if((int)screen.z==8)return float4(SunVisibility(p).xxx,1);
    // SoC R2 combine uses raw static occlusion *2; .5 encodes unmasked runtime sun.
    return float4(SunLight(p,n.xyz,n.w,shadow1.Load(int3(at,0)).a*2),1);}
float4 PSLocal(Varying v):SV_Target {int2 at=int2(v.p.xy);float depth=image1.Load(int3(at,0)).r;
    if(depth>=1)return 0;float3 n=image0.Load(int3(at,0)).xyz;return float4(LocalLight(WorldPosition(v.p.xy,depth),n),0);}
float4 PSForwardSun(Varying v,bool front:SV_IsFrontFace):SV_Target {float4 c=Base(v);
    return float4(c.rgb*SunLight(v.w,Normal(v,front),saturate(v.n.w),1),c.a);}
float4 PSForwardLocal(Varying v,bool front:SV_IsFrontFace):SV_Target {float4 c=Base(v);
    return float4(c.rgb*LocalLight(v.w,Normal(v,front))*(alpha.z>0?c.a:1),0);}
// combine: image0 albedo, image1 light; shadow0 normal; shadow1 depth.
float4 PSCombine(Varying v):SV_Target {int2 at=int2(v.p.xy);float depth=shadow1.Load(int3(at,0)).r;
    if(depth>=1){if(alpha.w>19.5)discard;return float4(fog.rgb,1);}float4 a=image0.Load(int3(at,0)),n=shadow0.Load(int3(at,0));
    float3 light=image1.Load(int3(at,0)).rgb;int mode=(int)screen.z;
    if(mode==1)return float4(a.rgb,1);if(mode==2)return float4(n.xyz*.5+.5,1);
    if(mode==3)return float4(saturate(length(WorldPosition(v.p.xy,depth)-camera.xyz)/camera.w).xxx,1);
    if(mode==4 || mode==8)return float4(light,1);if(mode==5)return float4(n.www,1);
    if(mode==6 || mode==9)return float4(a.rgb,1);if(mode==7)return float4(a.aaa,1);
    return float4(a.rgb*light,1);}

// Native environment entries use the engine's two weather cubemaps.
TextureCube sky0:register(t0);TextureCube sky1:register(t1);
float4 PSSky(Varying v):SV_Target {float3 d=v.n.xyz;return float4(lerp(sky0.Sample(surface,d).rgb,sky1.Sample(surface,d).rgb,v.c.a)*v.c.rgb*2,1);}
float4 PSClouds(Varying v):SV_Target {return float4((image0.Sample(surface,v.uv).rgb+image1.Sample(surface,v.n.xy).rgb)*v.c.rgb,v.c.a);}
float4 PSWater(Varying v,bool front:SV_IsFrontFace):SV_Target {
    float t=light_position.w;float2 flow=v.uv+float2(t*.015,t*.01);
    float4 water=image0.Sample(surface,flow);float2 wave=(water.rg-.5)*.012;
    float2 screen_uv=v.p.xy/screen.xy;float3 behind=image1.Sample(surface,saturate(screen_uv+wave)).rgb;
    float3 n=Normal(v,front);float fresnel=pow(1-saturate(dot(n,normalize(camera.xyz-v.w))),5);
    float3 lit=water.rgb*SunLight(v.w,n,saturate(v.n.w),1);return float4(lerp(lerp(behind,lit,.35),hemi.rgb,fresnel*.5),.8);
}
float4 PSDistortion(Varying v):SV_Target {float4 mask=Base(v);bool menu=alpha.w>29.5 && alpha.w<30.5;
    // Match R2 distort.ps: the menu mask is alpha-blended over neutral 0.5.
    float2 offset=(mask.rg-.5)*(menu?.05:.015)*mask.a;
    return float4(image1.Sample(surface,saturate(v.p.xy/max(screen.xy,1)+offset)).rgb,menu?1:mask.a);
}
// G3b mapDistort equivalent. The source texture is a displacement field, not
// a flame image: vertex RGB modulates strength only (X-Ray 1.6 particle_distort).
float4 PSParticleDistortionMask(Varying v):SV_Target {
    float4 field=image0.Sample(surface,v.uv);
    return float4(field.rgb,saturate(field.a*dot(v.c.rgb,float3(.33,.33,.33))));
}
float4 PSDistortionComposite(Varying v):SV_Target {
    // RGBA16F preserves neutral .5 exactly; the source engine used 127/255 in RGBA8.
    float2 displacement=(image1.Sample(surface,v.uv).rg-.5)*.05;
    return float4(image0.Sample(surface,saturate(v.uv+displacement)).rgb,1);
}

float4 PSPostprocess(Varying v):SV_Target {
    float2 split=light_position.yz*.5;float2 uv=v.uv;
    float3 c=(image0.Sample(surface,saturate(uv+split)).rgb+image0.Sample(surface,saturate(uv-split)).rgb)*.5;
    if(light_position.x>0){float2 step=light_position.x*3/screen.xy;c=(c*4+image0.Sample(surface,saturate(uv+float2(step.x,0))).rgb+
        image0.Sample(surface,saturate(uv-float2(step.x,0))).rgb+image0.Sample(surface,saturate(uv+float2(0,step.y))).rgb+image0.Sample(surface,saturate(uv-float2(0,step.y))).rgb)/8;}
    float3 gray=dot(c,hemi.rgb).xxx;c=lerp(c,gray,saturate(alpha.w));c=c*sun_color.rgb*2+ambient.rgb;
    float noise=frac(sin(dot(floor(v.p.xy/max(.01,light_direction.x))+light_direction.y,float2(12.9898,78.233)))*43758.5453)-.5;
    c+=noise*light_position.w;
    float gamma=max(.01,alpha.x),brightness=alpha.y*.5,contrast=alpha.z*.5;
    c=((contrast+.5)*pow(saturate(c),1/gamma)+(brightness-.5)*.5-contrast*.5+.25)*fog.rgb;
    return float4(saturate(c),1);
}
