#ifndef KICH_GRAPHICS_MATH_H
#define KICH_GRAPHICS_MATH_H

// Scalar functions are also exercised by the portable verification harness.
float kich_particle_fade(float scene_depth, float particle_depth, float inverse_distance)
{
    // Zero is the explicitly cleared sky / empty G-buffer value.
    float t = saturate((scene_depth - particle_depth) * inverse_distance);
    float fade = t * t * (3.0f - 2.0f * t);
    // A single return also works with the legacy D3DX9 compiler.
    return (scene_depth <= 0.0f) ? 1.0f : fade;
}

float kich_bloom_alpha(float brightness, float threshold, float knee_ratio)
{
    float knee = max(threshold * knee_ratio, 0.0001f);
    float soft = min(max(brightness - threshold + knee, 0.0f), 2.0f * knee);
    soft = soft * soft / (4.0f * knee);
    return max(max(brightness - threshold, soft), 0.0f);
}

#endif
