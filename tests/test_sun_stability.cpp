#include "../xr_3da/xrRender_R2/kich_sun_stability.h"
#include <cassert>
#include <initializer_list>
#include <cstdio>

int main()
{
    // Every near/far corner must remain inside the padded projection, across
    // camera yaw/pitch, aspect ratios, FOVs and shadow-map resolutions.
    for (float aspect : {0.5625f, 0.75f, 1.0f})
    for (float fov : {45.f, 67.5f, 90.f})
    for (float far_plane : {60.f, 120.f, 180.f})
    for (unsigned resolution : {512u, 1024u, 2048u, 4096u})
    {
        const float near_plane = 12.f;
        const float py = 1.f / std::tan(fov * 3.14159265f / 360.f);
        const float px = aspect * py;
        const float r = kich_sun::radius(near_plane, far_plane, px, py);
        const float extent = kich_sun::extent(r, resolution);
        const float texel = 2.f * extent / resolution;
        for (int yaw = 0; yaw < 360; yaw += 7)
        for (int pitch = -80; pitch <= 80; pitch += 13)
        {
            const float a = yaw * 3.14159265f / 180.f;
            const float b = pitch * 3.14159265f / 180.f;
            const float cx = 1000.f + std::sin(a) * 35.f;
            const float cy = -500.f + std::cos(b) * 27.f;
            const float sx = kich_sun::snap(cx, extent, resolution);
            const float sy = kich_sun::snap(cy, extent, resolution);
            assert(std::abs(sx - cx) <= texel * 0.501f);
            assert(std::abs(sy - cy) <= texel * 0.501f);
            for (float z : {near_plane, far_plane})
            for (int signx : {-1, 1})
            for (int signy : {-1, 1})
            {
                const float x = signx * z / px;
                const float y = signy * z / py;
                const float dz = z - (near_plane + far_plane) * 0.5f;
                const float yp = y * std::cos(b) - dz * std::sin(b);
                const float zp = y * std::sin(b) + dz * std::cos(b);
                const float xp = x * std::cos(a) + zp * std::sin(a);
                assert(std::abs(cx + xp - sx) < extent);
                assert(std::abs(cy + yp - sy) < extent);
            }
        }
        // Small translations around a texel centre do not move the projection;
        // crossing its cell advances exactly one texel, including negative space.
        for (int cell = -100; cell <= 100; ++cell)
        {
            const float center = cell * texel;
            assert(std::abs(kich_sun::snap(center + texel * 0.2f, extent, resolution) - center) < texel * 0.001f);
            assert(std::abs(kich_sun::snap(center - texel * 0.2f, extent, resolution) - center) < texel * 0.001f);
            assert(std::abs(kich_sun::snap(center + texel * 0.6f, extent, resolution) - center - texel) < texel * 0.001f);
        }
    }
    std::puts("PASS: frustum coverage, rotation-invariant scale, texel-grid stability");
}
