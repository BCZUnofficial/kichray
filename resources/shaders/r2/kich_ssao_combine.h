#ifndef KICH_SSAO_COMBINE_EX2
#define KICH_SSAO_COMBINE_EX2
uniform sampler2D s_kich_ssao;
uniform float4 kich_ao_control; // enabled, strength, debug, reserved
uniform float4 kich_ao_texel;

float2 kich_ao_gather(float2 uv, float3 P, float3 N, float spatial)
{
    float3 Q = tex2D(s_position,uv).xyz;
    float3 M = tex2D(s_normal,uv).xyz;
    M *= rsqrt(max(dot(M,M),0.000001f));
    float tolerance = max(0.02f,P.z*0.02f);
    float difference = abs(Q.z-P.z);
    float normalWeight = saturate((dot(N,M)-0.7f)/0.3f);
    float w = spatial*step(0.00001f,Q.z)*step(difference,tolerance)*normalWeight;
    w /= 1.0f+difference/tolerance;
    return float2(tex2D(s_kich_ssao,uv).r*w,w);
}

float kich_ssao_resolve(float2 tc, float3 P, float3 N)
{
    N *= rsqrt(max(dot(N,N),0.000001f));
    float2 texel = kich_ao_texel.xy;
    float2 grid = tc/texel-0.5f;
    float2 fraction = frac(grid);
    float2 base = (floor(grid)+0.5f)*texel;
    float2 lo=texel*0.5f, hi=1.0f-lo;
    float2 sum = kich_ao_gather(clamp(base,lo,hi),P,N,(1.0f-fraction.x)*(1.0f-fraction.y));
    sum += kich_ao_gather(clamp(base+float2(texel.x,0),lo,hi),P,N,fraction.x*(1.0f-fraction.y));
    sum += kich_ao_gather(clamp(base+float2(0,texel.y),lo,hi),P,N,(1.0f-fraction.x)*fraction.y);
    sum += kich_ao_gather(clamp(base+texel,lo,hi),P,N,fraction.x*fraction.y);
    // No compatible low-resolution sample: neutral AO, never a black hole.
    float valid = step(0.00001f,sum.y)*step(0.00001f,P.z);
    return lerp(1.0f,saturate(sum.x/max(sum.y,0.00001f)),valid);
}
#endif
