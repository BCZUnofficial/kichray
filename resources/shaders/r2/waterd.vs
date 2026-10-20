// KICH_WATER_V3: distortion geometry + optional projected scene depth.
#include "common.h"
#include "shared\waterconfig.h"
#include "shared\watermove.h"

struct v_vert
{
    float4 P     : POSITION;
    float4 N     : NORMAL;
    float4 T     : TANGENT;
    float4 B     : BINORMAL;
    float4 color : COLOR0;
    float2 uv    : TEXCOORD0;
};

struct vf
{
    float4 hpos   : POSITION;
    float2 tbase  : TEXCOORD0;
    float2 tdist0 : TEXCOORD1;
    float2 tdist1 : TEXCOORD2;
#ifdef NEED_SOFT_WATER
    float4 water_clip : TEXCOORD3;
#endif
};

vf main(v_vert v)
{
    vf o;

    float4 P = watermove(v.P);
    o.tbase = unpack_tc_base(v.uv, v.T.w, v.B.w);
    o.tdist0 = watermove_tc(o.tbase * W_DISTORT_BASE_TILE_0, P.xz, W_DISTORT_AMP_0);
    o.tdist1 = watermove_tc(o.tbase * W_DISTORT_BASE_TILE_1, P.xz, W_DISTORT_AMP_1);
    o.hpos = mul(m_VP, P);

#ifdef NEED_SOFT_WATER
    o.water_clip = o.hpos;
    o.water_clip.z = mul(m_V, P).z;
#endif

    return o;
}
