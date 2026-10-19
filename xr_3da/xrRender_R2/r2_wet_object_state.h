#pragma once

// Independent from rendering: linear fill/drain with bounded state.
inline float kich_step_object_wetness(float level, float dt, float rain,
    bool exposed, float fill_seconds, float dry_seconds)
{
    if (dt < 0.f) dt = 0.f;
    if (rain < 0.f) rain = 0.f;
    if (rain > 1.f) rain = 1.f;
    if (fill_seconds < 1.f) fill_seconds = 1.f;
    if (dry_seconds < 1.f) dry_seconds = 1.f;
    level += exposed && rain > 0.f ? dt * rain / fill_seconds : -dt / dry_seconds;
    return level < 0.f ? 0.f : (level > 1.f ? 1.f : level);
}
