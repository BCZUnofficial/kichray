#ifndef KICH_WET_SURFACES_COMMON_H
#define KICH_WET_SURFACES_COMMON_H

#include "common.h"

// KICH_WET_V3_5
// V2.5 wet surfaces + temporal procedural puddles.
uniform sampler s_rain_smap;
uniform float4x4 m_wet_shadow;

uniform float4 wet_params;       // x=rain density, y=darkening, z=gloss add, w=far distance
uniform float4 wet_up_view;
uniform float4 wet_smap_texel;

uniform float4 wet_motion;       // x=time, y=base normal frequency, z=base normal strength, w=roof boost
uniform float4 wet_water;        // x=ripple visibility, y=ripple speed, z=flow visibility, w=flow speed
uniform float4 wet_timing;       // x=base normal speed, y=ripple sharpness, z=flow sharpness, w=unused
uniform float4 wet_extra;        // x=bubble visibility, y=bubble speed, z=unused, w=unused

// Puddles:
// x=current accumulated level [0..1]
// y=visual amount
// z=additional gloss
// w=additional darkening
uniform float4 wet_puddle;

// x=normal flattening
// y=world-space puddle scale
// z=enabled [0/1]
// w=unused
uniform float4 wet_puddle2;

uniform float4 wet_world_x_view;
uniform float4 wet_world_z_view;
uniform float4 wet_camera_world;

half kich_wet_visibility(float4 tc)
{
    float iw = 1.0f / max(abs(tc.w), 0.00001f);
    float3 q = tc.xyz * iw;

    float inside =
        step(0.0f, q.x) * step(q.x, 1.0f) *
        step(0.0f, q.y) * step(q.y, 1.0f) *
        step(0.0f, q.z) * step(q.z, 1.0f);

    float2 d = wet_smap_texel.xy * tc.w;

    half s = 0.0h;
    s += tex2Dproj(s_rain_smap, tc + float4(-d.x, -d.y, 0.0f, 0.0f)).x;
    s += tex2Dproj(s_rain_smap, tc + float4( d.x, -d.y, 0.0f, 0.0f)).x;
    s += tex2Dproj(s_rain_smap, tc + float4(-d.x,  d.y, 0.0f, 0.0f)).x;
    s += tex2Dproj(s_rain_smap, tc + float4( d.x,  d.y, 0.0f, 0.0f)).x;

    return s * 0.25h * inside;
}

half kich_wet_roof_factor(float up)
{
    half roofish = 1.0h - smoothstep(0.72h, 0.97h, up);
    return lerp(1.0h, wet_motion.w, roofish);
}

float2 kich_world_xz(float4 P)
{
    float3 axisX = normalize(wet_world_x_view.xyz);
    float3 axisZ = normalize(wet_world_z_view.xyz);

    float wx = dot(P.xyz, axisX) + wet_camera_world.x;
    float wz = dot(P.xyz, axisZ) + wet_camera_world.z;
    return float2(wx, wz);
}

float3 kich_geom_normal(float4 P, float3 fallbackN)
{
    float3 dPx = ddx(P.xyz);
    float3 dPy = ddy(P.xyz);
    float3 rawN = cross(dPy, dPx);
    float n2 = dot(rawN, rawN);
    float3 geomN = rawN * rsqrt(max(n2, 1e-12f));
    geomN = (n2 > 1e-12f) ? geomN : fallbackN;

    float orient = step(0.0f, dot(geomN, fallbackN)) * 2.0f - 1.0f;
    geomN *= orient;
    return geomN;
}

// Stable value noise: used only for low-frequency puddle basins.
float kich_puddle_hash(float2 p)
{
    return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f);
}

float kich_puddle_noise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);

    float a = kich_puddle_hash(i);
    float b = kich_puddle_hash(i + float2(1.0f, 0.0f));
    float c = kich_puddle_hash(i + float2(0.0f, 1.0f));
    float d = kich_puddle_hash(i + float2(1.0f, 1.0f));

    float x0 = lerp(a, b, f.x);
    float x1 = lerp(c, d, f.x);
    return lerp(x0, x1, f.y);
}

float kich_puddle_field(float2 worldXZ)
{
    float s = max(wet_puddle2.y, 0.05f);

    // V3.2: domain-warped rotated noise to further break square shoreline
    // artifacts on uneven terrain.
    float2 p0 = worldXZ;
    float2 p1 = float2(
        worldXZ.x * 0.819152f - worldXZ.y * 0.573576f,
        worldXZ.x * 0.573576f + worldXZ.y * 0.819152f);
    float2 p2 = float2(
        worldXZ.x * 0.882948f + worldXZ.y * 0.469472f,
       -worldXZ.x * 0.469472f + worldXZ.y * 0.882948f);

    float wx = kich_puddle_noise(worldXZ * (0.061f * s) + float2(4.1f, -17.3f));
    float wy = kich_puddle_noise(worldXZ * (0.067f * s) + float2(-9.7f, 12.5f));
    float2 warp = (float2(wx, wy) - 0.5f) * (3.0f / max(s, 0.05f));

    p0 += warp;
    p1 += warp * 0.85f;
    p2 += warp * 0.65f;

    float n0 = kich_puddle_noise(p0 * (0.115f * s) + float2(13.7f, -5.1f));
    float n1 = kich_puddle_noise(p1 * (0.235f * s) + float2(-8.3f, 21.9f));
    float n2 = kich_puddle_noise(p2 * (0.470f * s) + float2(31.1f, 7.7f));

    return n0 * 0.48f + n1 * 0.34f + n2 * 0.18f;
}


half kich_wet_amount_from(float4 P, float3 N, float2 tc)
{
    float n2 = dot(N, N);
    N *= rsqrt(max(n2, 0.000001f));

    float geometry = step(0.00001f, P.z);
    float eye_distance = length(P.xyz);

    float near_fade = smoothstep(0.8f, 1.2f, eye_distance);

    float far_start = wet_params.w * 0.75f;
    float far_fade = 1.0f - smoothstep(far_start, wet_params.w, eye_distance);

    float3 up_view = normalize(wet_up_view.xyz);
    float up = saturate(dot(N, up_view));

    float slope = smoothstep(0.12f, 0.55f, up);
    float roof_boost = kich_wet_roof_factor(up);

    float4 rain_tc = mul(m_wet_shadow, float4(P.xyz, 1.0f));
    float exposure = kich_wet_visibility(rain_tc);

    return saturate(wet_params.x * exposure * slope * roof_boost *
                    near_fade * far_fade * geometry);
}

half kich_wet_amount(float2 tc)
{
    float4 P = tex2D(s_position, tc);
    float3 N = tex2D(s_normal, tc).xyz;
    return kich_wet_amount_from(P, N, tc);
}

half kich_puddle_amount_from(float4 P, float3 N, float2 worldXZ)
{
    float level = saturate(wet_puddle.x);
    float enabled = step(0.5f, wet_puddle2.z);

    float geometry = step(0.00001f, P.z);
    float eye_distance = length(P.xyz);

    // V3.5: one continuous visibility fade, no distance-dependent drainage.
    float near_fade = smoothstep(1.20f, 1.80f, eye_distance);
    float far_fade = 1.0f - smoothstep(18.0f, 26.0f, eye_distance);

    // Use the ORIGINAL G-buffer material normal. All material passes must run
    // before draw_wet_normal(). No centimetre tests on FP16 view positions.
    float3 baseN = N * rsqrt(max(dot(N, N), 1e-8f));
    float base_up = saturate(dot(baseN, normalize(wet_up_view.xyz)));
    float flat_local = smoothstep(0.970f, 0.995f, base_up);

    float4 rain_tc = mul(m_wet_shadow, float4(P.xyz, 1.0f));
    float exposure = kich_wet_visibility(rain_tc);

    float field = kich_puddle_field(worldXZ);
    float threshold = lerp(0.82f, 0.44f, level);

    // Derivatives only anti-alias the world-space shoreline, never define
    // drainage or surface slope. Preserve coverage when the field is small.
    float field_aa = clamp(fwidth(field), 0.006f, 0.040f);

    float basin = smoothstep(threshold - 0.050f - field_aa,
                             threshold + 0.100f + field_aa,
                             field);

    return saturate(enabled * wet_puddle.y * level * basin *
                    flat_local * exposure *
                    near_fade * far_fade * geometry);
}
half kich_puddle_amount(float2 tc)
{
    float4 P = tex2D(s_position, tc);
    float3 N = normalize(tex2D(s_normal, tc).xyz);
    float2 worldXZ = kich_world_xz(P);
    return kich_puddle_amount_from(P, N, worldXZ);
}



// V3.13: only near-vertical static surfaces; independent of ground wet mask.
uniform float4 wet_wall; // accumulated level, bead strength, cycle seconds, reserved
uniform float4 wet_wall_limits; // width min/max metres, speed min/max metres/sec
half kich_wall_amount_from(float4 P,float3 N)
{
    N=normalize(N);
    float upright=1.f-smoothstep(0.15f,0.45f,abs(dot(N,normalize(wet_up_view.xyz))));
    float distance=length(P.xyz);
    float viewfade=smoothstep(0.25f,0.65f,distance)*(1.f-smoothstep(wet_params.w*0.75f,wet_params.w,distance));
    // Probe just outside the facade, reducing self-shadow from the facade top.
    float4 rain_tc=mul(m_wet_shadow,float4(P.xyz+N*0.08f,1.f));
    float exposure=kich_wet_visibility(rain_tc);
    return saturate(wet_wall.x*upright*viewfade*exposure*step(0.00001f,P.z));
}
half kich_wall_amount(float2 tc)
{
    return kich_wall_amount_from(tex2D(s_position,tc),tex2D(s_normal,tc).xyz);
}

#endif
