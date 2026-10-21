// B4.4 experimental diffuse path. Legacy texture values; linear/HDR is later.
cbuffer Frame : register(b0) {
    row_major float4x4 world,mvp,normal_matrix,inverse_vp,shadow[6];
    float4 camera,screen,sun_direction,sun_color,ambient,hemi,fog;
    float4 light_position,light_direction,light_color,alpha;
};
Texture2D image0:register(t0); Texture2D image1:register(t1);
Texture2D shadow0:register(t2); Texture2D shadow1:register(t3);
Texture2D shadow2:register(t4); Texture2D shadow3:register(t5);
Texture2D shadow4:register(t6); Texture2D shadow5:register(t7);
SamplerState surface:register(s0); SamplerComparisonState shadow_surface:register(s1);
struct StaticInput {float3 p:POSITION;float2 uv:TEXCOORD0;float4 n:NORMAL;};
struct ModelInput {float4 p:POSITION;float2 uv:TEXCOORD0;float4 c:COLOR0;float4 n:NORMAL;float3 w:TEXCOORD1;};
struct ScreenInput {float3 p:POSITION;float2 uv:TEXCOORD0;};
struct Varying {float4 p:SV_Position;float2 uv:TEXCOORD0;float4 c:COLOR0;float4 n:TEXCOORD1;float3 w:TEXCOORD2;};
Varying VSStatic(StaticInput v){Varying o;o.p=mul(float4(v.p,1),mvp);o.uv=v.uv;o.c=1;
    o.n=float4(mul(float4(v.n.xyz,0),normal_matrix).xyz,v.n.w);o.w=mul(float4(v.p,1),world).xyz;return o;}
Varying VSModel(ModelInput v){Varying o;o.p=v.p;o.uv=v.uv;o.c=v.c;o.n=v.n;o.w=v.w;return o;}
Varying VSModelShadow(ModelInput v){Varying o=VSModel(v);o.p=mul(float4(v.w,1),mvp);return o;}
Varying VSScreen(ScreenInput v){Varying o=(Varying)0;o.p=float4(v.p,1);o.uv=v.uv;o.c=1;return o;}
float4 Base(Varying v){float4 c=image0.Sample(surface,v.uv)*v.c;
    if(alpha.w>9.5 && alpha.w<10.5){float4 mask=shadow0.Sample(surface,v.uv);mask/=max(dot(mask,1),.0001);
        float2 tc=v.uv*50;float3 detail=shadow1.Sample(surface,tc).rgb*mask.r+shadow2.Sample(surface,tc).rgb*mask.g+
            shadow3.Sample(surface,tc).rgb*mask.b+shadow4.Sample(surface,tc).rgb*mask.a;c.rgb*=detail*2;}
    float a=c.a,r=alpha.x;int f=(int)alpha.y;
    bool keep=f==8 || (f==2 && a<r) || (f==3 && a==r) || (f==4 && a<=r) ||
        (f==5 && a>r) || (f==6 && a!=r) || (f==7 && a>=r);
    if(!keep)discard;return c;}
float3 Normal(Varying v,bool front){float3 n=v.n.xyz;
    if(dot(n,n)<.05)n=cross(ddy(v.w),ddx(v.w));
    n=dot(n,n)>.000001?normalize(n):float3(0,1,0);return front?n:-n;}
struct GeometryOutput {float4 albedo:SV_Target0;float4 normal:SV_Target1;};
GeometryOutput PSGeometry(Varying v,bool front:SV_IsFrontFace){GeometryOutput o;
    o.albedo=float4(Base(v).rgb,1);o.normal=float4(Normal(v,front),saturate(v.n.w));return o;}
void PSShadow(Varying v){Base(v);}
float4 PSPlain(Varying v):SV_Target {return Base(v);}
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
float3 SunLight(float3 p,float3 n,float h){float sky=saturate(n.y*.5+.5)*h;
    float3 base=ambient.rgb+hemi.rgb*sky;
    return base+sun_color.rgb*saturate(dot(n,-sun_direction.xyz))*SunVisibility(p);}
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
    return float4(SunLight(p,n.xyz,n.w),1);}
float4 PSLocal(Varying v):SV_Target {int2 at=int2(v.p.xy);float depth=image1.Load(int3(at,0)).r;
    if(depth>=1)return 0;float3 n=image0.Load(int3(at,0)).xyz;return float4(LocalLight(WorldPosition(v.p.xy,depth),n),0);}
float4 PSForwardSun(Varying v,bool front:SV_IsFrontFace):SV_Target {float4 c=Base(v);
    return float4(c.rgb*SunLight(v.w,Normal(v,front),saturate(v.n.w)),c.a);}
float4 PSForwardLocal(Varying v,bool front:SV_IsFrontFace):SV_Target {float4 c=Base(v);
    return float4(c.rgb*LocalLight(v.w,Normal(v,front))*(alpha.z>0?c.a:1),0);}
// combine: image0 albedo, image1 light; shadow0 normal; shadow1 depth.
float4 PSCombine(Varying v):SV_Target {int2 at=int2(v.p.xy);float depth=shadow1.Load(int3(at,0)).r;
    if(depth>=1){if(alpha.w>19.5)discard;return float4(fog.rgb,1);}float4 a=image0.Load(int3(at,0)),n=shadow0.Load(int3(at,0));
    float3 light=image1.Load(int3(at,0)).rgb;int mode=(int)screen.z;
    if(mode==1)return float4(a.rgb,1);if(mode==2)return float4(n.xyz*.5+.5,1);
    if(mode==3)return float4(saturate(length(WorldPosition(v.p.xy,depth)-camera.xyz)/camera.w).xxx,1);
    if(mode==4)return float4(light,1);if(mode==5)return float4(n.www,1);
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
    float3 lit=water.rgb*SunLight(v.w,n,saturate(v.n.w));return float4(lerp(lerp(behind,lit,.35),hemi.rgb,fresnel*.5),.8);
}
float4 PSDistortion(Varying v):SV_Target {float4 mask=Base(v);bool menu=alpha.w>29.5 && alpha.w<30.5;
    // Match R2 distort.ps: the menu mask is alpha-blended over neutral 0.5.
    float2 offset=(mask.rg-.5)*(menu?.05:.015)*mask.a;
    return float4(image1.Sample(surface,saturate(v.p.xy/max(screen.xy,1)+offset)).rgb,menu?1:mask.a);
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
