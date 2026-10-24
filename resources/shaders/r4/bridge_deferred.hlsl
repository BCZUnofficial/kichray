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
SamplerState surface1:register(s2); SamplerState surface2:register(s3);
SamplerState surface3:register(s4); SamplerState surface4:register(s5);
SamplerState surface5:register(s6); SamplerState surface6:register(s7); SamplerState surface7:register(s8);
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
float4 Base(Varying v){float4 c=image0.Sample(surface,v.uv)*v.c;
    if(((uint)material.x&8)!=0)c.rgb*=shadow3.Sample(surface5,v.uv*material.yz).rgb*2;
    if(alpha.w>9.5 && alpha.w<10.5){float4 mask=shadow0.Sample(surface2,v.uv);mask/=max(dot(mask,1),.0001);
        float2 tc=v.uv*50;float3 detail=shadow1.Sample(surface3,tc).rgb*mask.r+shadow2.Sample(surface4,tc).rgb*mask.g+
            shadow3.Sample(surface5,tc).rgb*mask.b+shadow4.Sample(surface6,tc).rgb*mask.a;c.rgb*=detail*2;}
    AlphaTest(c.a);return c;}
float3 Normal(Varying v,bool front){float3 n=v.n.xyz;
    if(dot(n,n)<.05)n=cross(ddy(v.w),ddx(v.w));
    n=dot(n,n)>.000001?normalize(n):float3(0,1,0);
    float3 t=v.t-n*dot(n,v.t);float handed=dot(cross(n,t),v.b);
    if(((uint)material.x&1)!=0 && dot(t,t)>1e-8 && abs(handed)>1e-8){
        t=normalize(t);float3 b=normalize(cross(n,t))*(handed<0?-1:1);
        // SoC R2 sload: bump stores gloss,x-normal in alpha,y in B,z in G;
        // bump# stores signed reconstruction error in RGB, height in alpha.
        float4 Nu=image1.Sample(surface1,v.uv),NuE=shadow0.Sample(surface2,v.uv);
        float3 mapped=Nu.wzy+NuE.xyz-1;mapped.z*=.5;
        float3 result=t*mapped.x+b*mapped.y+n*mapped.z;
        if(dot(result,result)>1e-8)n=normalize(result);
    }
    // Flip the complete mapped normal on the back face, preserving mirrored UV handedness.
    return front?n:-n;}
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
float Gloss(Varying v){
    // SoC sload.h: bump R is squared gloss, flat surfaces use def_gloss.
    float gloss=2.0/255;
    if(((uint)material.x&1)!=0){float g=image1.Sample(surface1,v.uv).r;gloss=g*g;}
    if(((uint)material.x&9)==9)gloss*=shadow3.Sample(surface5,v.uv*material.yz).a*2;
    return saturate(gloss);
}
struct GeometryOutput {float4 albedo:SV_Target0;float4 normal:SV_Target1;float4 baked:SV_Target2;};
GeometryOutput PSGeometry(Varying v,bool front:SV_IsFrontFace){GeometryOutput o;
    uint flags=(uint)material.x;float h=v.n.w,s=(flags&32)!=0?v.baked_sun.w:.5;float3 baked=0;
    if((flags&16)!=0)baked=v.baked_sun.rgb;
    // R1/XRLC baked RGB remains additive irradiance, separate from material
    // response to direct lights. Do not reinterpret it as albedo or hemi.
    if((flags&2)!=0){float4 lm=shadow1.Sample(surface3,v.uv1);baked+=lm.rgb;s=lm.a;}
    if((flags&4)!=0){float4 lm=shadow2.Sample(surface4,v.uv1);h=dot(lm.rgb,float3(1.0/3,1.0/3,1.0/3));s=lm.a;}
    o.albedo=float4(Base(v).rgb,saturate(s));
    float gloss=Gloss(v);
    o.normal=float4(EncodeNormal(Normal(v,front)),saturate(h),PackMaterial(gloss,material.w));o.baked=float4(baked,0);
    // SoC E[4] accum_emissive[l] writes constant irradiance/specular. Fold it
    // into this base surface's accumulator once, retaining its depth/coverage.
    if((flags&256)!=0){o.baked.rgb+=ambient.www;o.baked.a+=hemi.w*gloss;}
    // Diagnostics keep separate components before accumulation, with no new MRT.
    if((int)screen.z==6)o.albedo.rgb=baked;
    if((int)screen.z==9){float3 n=v.n.xyz;if(dot(n,n)<.05)n=cross(ddy(v.w),ddx(v.w));
        n=dot(n,n)>1e-6?normalize(n):float3(0,1,0);o.albedo.rgb=(front?n:-n)*.5+.5;}
    if((int)screen.z==8)o.baked=0;
    return o;}
void PSShadow(Varying v){if((int)alpha.y!=8)Base(v);}
float4 PSPlain(Varying v):SV_Target {float4 c=Base(v);
    // Non-emissive UI/copy/glow keeps the authored value. Blended self-light
    // folds its known R2 emissive response once; opaque emission uses GBuffer.
    if(ambient.w>0)c.rgb=c.rgb*ambient.w+hemi.w*Gloss(v);
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
float3 WorldPosition(float2 pixel,float depth){float4 p=float4(pixel.x*2/screen.x-1,1-pixel.y*2/screen.y,depth,1);
    p=mul(p,inverse_vp);return p.xyz/p.w;}
// Derivatives are captured before depth rejection, alpha discard, attenuation
// and cube-face selection. Taking ddx/ddy inside those branches is undefined.
bool ShadowPlaneStable(float distance_squared,float step_squared){
    return isfinite(distance_squared) && isfinite(step_squared) && step_squared<=max(.0625,distance_squared*.0025);
}
void ShadowDerivatives(float3 p,out float3 dx,out float3 dy){
    dx=ddx(p);dy=ddy(p);
    // A foreground/background discontinuity does not describe one receiver
    // plane. Reject it instead of extrapolating a large false depth gradient.
    float3 from_camera=p-camera.xyz;
    if(!all(isfinite(dx)) || !all(isfinite(dy)) || !ShadowPlaneStable(dot(from_camera,from_camera),max(dot(dx,dx),dot(dy,dy)))){dx=0;dy=0;}
}
float2 ShadowGradient(float3 dx,float3 dy){
    float determinant=dx.x*dy.y-dx.y*dy.x;
    float scale=max(dx.x*dx.x+dx.y*dx.y,dy.x*dy.x+dy.y*dy.y);
    if(abs(determinant)<=max(scale*1e-5,1e-20))return float2(0,0);
    float2 gradient=float2(dx.z*dy.y-dy.z*dx.y,dx.x*dy.z-dy.x*dx.z)/determinant;
    return all(isfinite(gradient))?gradient:float2(0,0);
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
    float2 gradient=ShadowGradient(ShadowDerivative(projected,dx),ShadowDerivative(projected,dy));
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
float SunVisibility(float3 p,float3 dx,float3 dy){
    if(sun_color.w<=0)return 0;
    // Precision allowance only: plane correction handles the filter footprint.
    p-=sun_direction.xyz*.01;
    float4 near_position=mul(float4(p,1),shadow[0]);
    float3 q=near_position.xyz/near_position.w;
    float edge=max(abs(q.x),abs(q.y));
    [branch] if(q.z<0 || q.z>1 || edge>=1)return PCF(shadow1,mul(float4(p,1),shadow[1]),mul(float4(dx,0),shadow[1]),mul(float4(dy,0),shadow[1]));
    float near_visibility=PCF(shadow0,near_position,mul(float4(dx,0),shadow[0]),mul(float4(dy,0),shadow[0]));
    [branch] if(edge<=.9)return near_visibility;
    float far_visibility=PCF(shadow1,mul(float4(p,1),shadow[1]),mul(float4(dx,0),shadow[1]),mul(float4(dy,0),shadow[1]));
    return lerp(near_visibility,far_visibility,saturate((edge-.9)*10));
}
// Analytical evaluation of SoC r2_rendertarget.cpp's four-slice material LUT.
// This retains its diffuse/specular response, not a new PBR/brightness model;
// LUT texel quantization and environment-cubemap specular remain separate work.
float2 MaterialSlice(float ld,float ls,int index){
    if(index==0)return float2(pow(ld,.75),pow(ls,16)*.5);
    if(index==1)return float2(pow(ld,.90),pow(ls,24));
    if(index==2)return float2(ld,saturate(pow(ls*1.01,128)));
    float s0=abs(1-abs(.05*sin(33*ld)+ld-ls));
    float s1=abs(1-abs(.05*cos(33*ld*ls)+ld-ls));
    float s2=abs(1-abs(ld-ls));
    return float2(ld,saturate(pow(max(s0,max(s1,s2)),24)*pow(ld,1.0/7)));
}
float2 MaterialResponse(float ld,float lh,float m){
    ld=saturate(ld);float ls=saturate(lh)*pow(ld,1.0/32);
    m=clamp(m,0,3);int lo=(int)floor(m);
    return lerp(MaterialSlice(ld,ls,lo),MaterialSlice(ld,ls,min(lo+1,3)),frac(m));
}
float SpecularIntensity(float3 color){float value=max(0,dot(color,float3(1.0/3,1.0/3,1.0/3)));
    return value<1?pow(value,2.0/3):value;}
float4 DirectLight(float3 p,float3 n,float3 direction,float3 color,float gloss,float m){
    float3 view=camera.xyz-p;view/=max(length(view),1e-8);
    float3 half_vector=direction+view;half_vector/=max(length(half_vector),1e-8);
    float2 response=MaterialResponse(dot(n,direction),dot(n,half_vector),m);
    return float4(color*response.x,SpecularIntensity(color)*response.y*gloss);
}
float4 SunLighting(float3 p,float3 n,float h,float sun_mask,float gloss,float m,float3 dx,float3 dy){
    // Source hemi already encodes visibility; R2 hmodel does not multiply it
    // by a second world-up term. Weather hemi RGB remains the env approximation.
    float3 indirect=ambient.rgb+hemi.rgb*MaterialResponse(saturate(h),0,m).x;
    float4 direct=DirectLight(p,n,-sun_direction.xyz,sun_color.rgb,gloss,m)*SunVisibility(p,dx,dy)*sun_mask;
    return float4(indirect,0)+direct;
}
float3 SunLight(float3 p,float3 n,float h,float sun_mask,float3 dx,float3 dy){return SunLighting(p,n,h,sun_mask,0,material.w,dx,dy).rgb;}
uint CubeFace(float3 d){float3 a=abs(d);if(a.x>=a.y && a.x>=a.z)return d.x>=0?0:1;
    if(a.y>=a.z)return d.y>=0?2:3;return d.z>=0?4:5;}
float LocalVisibility(float3 p,float3 dx,float3 dy){if(alpha.w<.5)return 1;uint face=light_color.w>1.5?0:CubeFace(p-light_position.xyz);
    // A fixed projected-depth subtraction scales to meters near the far plane.
    // Move the receiver by only 3 mm instead, preserving nearby blockers.
    float3 towards=light_position.xyz-p;
    p+=towards*(.003/max(length(towards),.003));
    float4 projected=mul(float4(p,1),shadow[face]);
    float4 sx=mul(float4(dx,0),shadow[face]),sy=mul(float4(dy,0),shadow[face]);
    if(face==0)return PCF(shadow0,projected,sx,sy);if(face==1)return PCF(shadow1,projected,sx,sy);
    if(face==2)return PCF(shadow2,projected,sx,sy);if(face==3)return PCF(shadow3,projected,sx,sy);
    if(face==4)return PCF(shadow4,projected,sx,sy);return PCF(shadow5,projected,sx,sy);}
float LocalAttenuation(float distance_squared,float range){float radius=max(range*.95,.0001);
    return saturate(1-distance_squared/(radius*radius));}
float4 LocalLight(float3 p,float3 n,float gloss,float m,float3 dx,float3 dy){float3 delta=light_position.xyz-p;float distance=length(delta);
    float3 direction=delta/max(distance,.0001);float attenuation=LocalAttenuation(dot(delta,delta),light_position.w);
    if(light_color.w>1.5){float angle=dot(-direction,light_direction.xyz);
        attenuation*=saturate((angle-light_direction.w)/max(.001,(1-light_direction.w)*.2));}
    if(attenuation<=0)return 0;
    return DirectLight(p,n,direction,light_color.rgb,gloss,m)*(attenuation*LocalVisibility(p,dx,dy));}
float4 PSSun(Varying v):SV_Target {int2 at=int2(v.p.xy);float depth=image1.Load(int3(at,0)).r;
    float3 p=WorldPosition(v.p.xy,depth),dx,dy;ShadowDerivatives(p,dx,dy);
    if(depth>=1)return 0;float4 n=image0.Load(int3(at,0));
    if((int)screen.z==8)return float4(SunVisibility(p,dx,dy).xxx,0);
    // SoC R2 combine uses raw static occlusion *2; .5 encodes unmasked runtime sun.
    return SunLighting(p,DecodeNormal(n.xy),n.z,shadow2.Load(int3(at,0)).a*2,MaterialGloss(n.w),MaterialClass(n.w),dx,dy);}
float4 PSLocal(Varying v):SV_Target {int2 at=int2(v.p.xy);float depth=image1.Load(int3(at,0)).r;
    float3 p=WorldPosition(v.p.xy,depth),dx,dy;ShadowDerivatives(p,dx,dy);
    if(depth>=1)return 0;float4 n=image0.Load(int3(at,0));
    return LocalLight(p,DecodeNormal(n.xy),MaterialGloss(n.w),MaterialClass(n.w),dx,dy);}
float4 PSForwardSun(Varying v,bool front:SV_IsFrontFace):SV_Target {float3 dx,dy;ShadowDerivatives(v.w,dx,dy);float4 c=Base(v);
    float4 light=SunLighting(v.w,Normal(v,front),saturate(v.n.w),1,Gloss(v),material.w,dx,dy);
    return float4(c.rgb*light.rgb+light.aaa,c.a);}
float4 PSForwardLocal(Varying v,bool front:SV_IsFrontFace):SV_Target {float3 dx,dy;ShadowDerivatives(v.w,dx,dy);float4 c=Base(v);
    float4 light=LocalLight(v.w,Normal(v,front),Gloss(v),material.w,dx,dy);
    return float4((c.rgb*light.rgb+light.aaa)*(alpha.z>0?c.a:1),0);}
// combine: image0 albedo, image1 light; shadow0 normal; shadow1 depth.
float4 PSCombine(Varying v):SV_Target {int2 at=int2(v.p.xy);float depth=shadow1.Load(int3(at,0)).r;
    if(depth>=1){if(alpha.w>19.5)discard;return float4(fog.rgb,1);}float4 a=image0.Load(int3(at,0)),n=shadow0.Load(int3(at,0));
    float4 light=image1.Load(int3(at,0));int mode=(int)screen.z;
    if(mode==1)return float4(a.rgb,1);if(mode==2)return float4(DecodeNormal(n.xy)*.5+.5,1);
    if(mode==3)return float4(saturate(length(WorldPosition(v.p.xy,depth)-camera.xyz)/camera.w).xxx,1);
    if(mode==4 || mode==8)return float4(light.rgb+light.aaa,1);if(mode==5)return float4(n.zzz,1);
    if(mode==6 || mode==9)return float4(a.rgb,1);if(mode==7)return float4(a.aaa,1);
    return float4(a.rgb*light.rgb+light.aaa,1);}

// Native environment entries use the engine's two weather cubemaps.
TextureCube sky0:register(t0);TextureCube sky1:register(t1);
float4 PSSky(Varying v):SV_Target {float3 d=v.n.xyz;return float4(lerp(sky0.Sample(surface,d).rgb,sky1.Sample(surface1,d).rgb,v.c.a)*v.c.rgb*2,1);}
float4 PSClouds(Varying v):SV_Target {return float4((image0.Sample(surface,v.uv).rgb+image1.Sample(surface1,v.n.xy).rgb)*v.c.rgb,v.c.a);}
float4 PSWater(Varying v,bool front:SV_IsFrontFace):SV_Target {
    float3 dx,dy;ShadowDerivatives(v.w,dx,dy);
    float t=light_position.w;float2 flow=v.uv+float2(t*.015,t*.01);
    float4 water=image0.Sample(surface,flow);float2 wave=(water.rg-.5)*.012;
    float2 screen_uv=v.p.xy/screen.xy;float3 behind=image1.Sample(surface1,saturate(screen_uv+wave)).rgb;
    float3 n=Normal(v,front);float fresnel=pow(1-saturate(dot(n,normalize(camera.xyz-v.w))),5);
    float3 lit=water.rgb*SunLight(v.w,n,saturate(v.n.w),1,dx,dy);return float4(lerp(lerp(behind,lit,.35),hemi.rgb,fresnel*.5),.8);
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
