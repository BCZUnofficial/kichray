#include "common.h"

uniform float4 kich_particle_viewport; // inverse width, inverse height, 0, 0

struct vv
{
    float4 P : POSITION;
    float2 tc : TEXCOORD0;
    float4 c : COLOR0;
};

struct vf
{
    float4 hpos : POSITION;
    float2 tc : TEXCOORD0;
    float4 c : COLOR0;
    float4 projected : TEXCOORD1;
};

vf main(vv v)
{
    vf o;
    o.hpos = mul(m_WVP, v.P);
    o.tc = v.tc;
    o.c = v.c;
    // D3D9 half-texel alignment; pass linear view depth, not clip-space z.
    o.projected.xy = float2(o.hpos.x, -o.hpos.y) * 0.5
        + o.hpos.w * (0.5 + 0.5 * kich_particle_viewport.xy);
    o.projected.z = mul(m_WV, v.P).z;
    o.projected.w = o.hpos.w;
    return o;
}
