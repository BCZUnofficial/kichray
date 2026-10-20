#ifndef KICH_WATER_DEPTH_H
#define KICH_WATER_DEPTH_H

#include "kich_water_config.h"

// x: current rain density; yz: viewport width/height. Bound by C++.
uniform float4 kich_water_weather;

// SoC-safe equivalent of the Clear Sky projected depth test.
// V2 already used the current clip position plus view-space Z; keep that path
// instead of importing CS' m_texgen/USE_SOFT_WATER renderer dependency.
float kich_water_gap(float4 clip)
{
    float2 uv = clip.xy / max(clip.w, 0.0001f) * float2(0.5f, -0.5f) + 0.5f;
    uv += 0.5f / max(kich_water_weather.yz, float2(1.0f, 1.0f));

    float sceneZ = tex2D(s_position, saturate(uv)).z;
    float valid = step(0.00001f, sceneZ);
    return lerp(20.0f, clamp(sceneZ - clip.z, 0.0f, 20.0f), valid);
}

float kich_water_shore(float gap)
{
#if KICH_WATER_SOFT
    return smoothstep(0.0f, KICH_WATER_SHORE_WIDTH, gap);
#else
    return 1.0f;
#endif
}

// Clear Sky uses an exponential depth term for soft-water opacity.
float kich_water_depth_opacity(float gap)
{
    return 1.0f - exp(-KICH_WATER_DEPTH_OPACITY * max(gap, 0.0f));
}

// Clear Sky fades the distortion vector separately near intersections.
float kich_water_distort_shore(float gap)
{
#if KICH_WATER_SOFT
    return saturate(KICH_WATER_DISTORT_SHORE * max(gap, 0.0f));
#else
    return 1.0f;
#endif
}

#endif
