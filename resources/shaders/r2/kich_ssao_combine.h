#ifndef KICH_SSAO_COMBINE_EX2
#define KICH_SSAO_COMBINE_EX2
uniform sampler2D s_kich_ssao;
uniform float4 kich_ao_control;
uniform float4 kich_ao_texel;

float2 kich_ao_filter_tap(float2 uv, float depth, float tolerance, float spatial)
{
    float2 lo = kich_ao_texel.xy*0.5f;
    uv = clamp(uv,lo,1.0f-lo);
    float z = tex2Dlod(s_position,float4(uv,0,0)).z;
    float w = saturate(1.0f-abs(z-depth)/tolerance);
    w = spatial*w*w*step(0.00001f,z);
    float ao = tex2Dlod(s_kich_ssao,float4(uv,0,0)).r;
    return float2(ao*w,w);
}

float kich_ssao_resolve(float2 tc, float3 P, float3 unusedNormal)
{
    // Full-resolution buffers share pixel centers. A positive center weight
    // preserves valid AO even if all neighbors are rejected at a silhouette.
    float center = tex2Dlod(s_kich_ssao,float4(tc,0,0)).r;
    float2 sum = float2(center*4.0f,4.0f);
    float2 d = kich_ao_texel.xy;
    float tolerance = max(0.04f,abs(P.z)*0.02f);
    // Explicit 3x3 Gaussian weights, depth-aware. No loops or attributes.
    sum += kich_ao_filter_tap(tc+float2(-d.x,-d.y),P.z,tolerance,1.0f);
    sum += kich_ao_filter_tap(tc+float2( 0.0f,-d.y),P.z,tolerance,2.0f);
    sum += kich_ao_filter_tap(tc+float2( d.x,-d.y),P.z,tolerance,1.0f);
    sum += kich_ao_filter_tap(tc+float2(-d.x, 0.0f),P.z,tolerance,2.0f);
    sum += kich_ao_filter_tap(tc+float2( d.x, 0.0f),P.z,tolerance,2.0f);
    sum += kich_ao_filter_tap(tc+float2(-d.x, d.y),P.z,tolerance,1.0f);
    sum += kich_ao_filter_tap(tc+float2( 0.0f, d.y),P.z,tolerance,2.0f);
    sum += kich_ao_filter_tap(tc+float2( d.x, d.y),P.z,tolerance,1.0f);
    return lerp(1.0f,saturate(sum.x/sum.y),step(0.00001f,P.z));
}
#endif
