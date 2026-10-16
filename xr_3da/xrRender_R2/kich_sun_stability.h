#pragma once
#include <cmath>

namespace kich_sun
{
// Enclose both perspective planes in a rotation-invariant sphere. Projection
// diagonal values come from the actual engine matrix (including its aspect convention).
inline float radius(float near_plane, float far_plane, float projection_x, float projection_y)
{
    const float half_depth = (far_plane - near_plane) * 0.5f;
    const float half_width = far_plane / projection_x;
    const float half_height = far_plane / projection_y;
    const float r = std::sqrt(half_depth * half_depth + half_width * half_width + half_height * half_height);
    // Coarse upward rounding prevents floating point noise from changing scale.
    return std::ceil(r * 16.0f) / 16.0f;
}

inline float extent(float radius, unsigned resolution)
{
    // Reserve two texels per edge for snapping and shadow filter taps.
    return radius * float(resolution) / float(resolution - 4);
}

inline float snap(float coordinate, float half_extent, unsigned resolution)
{
    const float texel = (2.0f * half_extent) / float(resolution);
    return std::floor(coordinate / texel + 0.5f) * texel;
}
}
