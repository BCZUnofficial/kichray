#ifndef KICH_OBJECT_WET_H
#define KICH_OBJECT_WET_H
uniform float4 kich_object_wet;
uniform float4 kich_npc_drips;
uniform float4 kich_drip_up, kich_drip_x, kich_drip_z, kich_drip_origin;
half4 kich_object_material(half4 color)
{
    half wet = saturate(kich_object_wet.x);
    color.rgb *= 1.0h - wet * kich_object_wet.z;
    color.a = saturate(color.a + (1.0h-color.a) * wet * kich_object_wet.y);
    return color;
}
// Isolated ellipsoid: horizontal/vertical normal slope and coverage.
// A single final return is required by legacy D3DX.
float3 kich_bead_plane(float2 p)
{
    float2 spacing = float2(0.06f,0.18f);
    float2 cell = floor(p/spacing);
    float seed = frac(sin(dot(cell,float2(37.1f,91.7f))) * 15731.7f);
    float age = frac(kich_npc_drips.y * 0.75f + seed);
    float life = smoothstep(0.0f,0.10f,age) * (1.0f-smoothstep(0.50f,0.70f,age));
    float travel = min(kich_npc_drips.z,0.12f) * age / 0.75f;
    float2 center = float2(0.015f+0.03f*frac(seed*7.13f),0.125f-travel);
    float2 radius = float2(0.0026f,0.0039f);
    float2 q = (p-cell*spacing-center)/radius;
    float r2 = dot(q,q);
    // Differentiate continuous coordinates, not the discontinuous cell ID.
    float footprint = max(length(ddx(p/radius)),length(ddy(p/radius)));
    float aa = clamp(footprint*0.5f,0.08f,0.65f);
    float coverage = (1.0f-smoothstep(1.0f-aa,1.0f+aa,r2))*life;
    coverage *= 1.0f-smoothstep(1.0f,3.0f,footprint);
    float dome = coverage * rsqrt(max(0.20f,1.0f-saturate(r2)*0.8f));
    return float3(q*dome,coverage);
}
void kich_object_drips(inout f_deffer O, float3 view_position)
{
    // Uniform condition, with no asymmetric early return.
    if (kich_npc_drips.x > 0.0f)
    {
        float wet = smoothstep(0.35f,0.85f,kich_object_wet.x);
        float3 N = normalize(O.Ne.xyz);
        float3 up = kich_drip_up.xyz;
        float3 relative = view_position-kich_drip_origin.xyz;
        float y = dot(relative,up);
        // Vertical projections cover upright surfaces; XZ alone misses them.
        float3 bx = kich_bead_plane(float2(dot(relative,kich_drip_x.xyz),y));
        float3 bz = kich_bead_plane(float2(dot(relative,kich_drip_z.xyz)+0.057f,y+0.071f));
        float nx = dot(N,kich_drip_x.xyz);
        float nz = dot(N,kich_drip_z.xyz);
        float2 weights = float2(nz*nz,nx*nx);
        weights *= weights;
        weights /= max(weights.x+weights.y,0.0001f);
        float3 gradient = weights.x*(kich_drip_x.xyz*bx.x+up*bx.y)
                        + weights.y*(kich_drip_z.xyz*bz.x+up*bz.y);
        gradient -= N*dot(gradient,N);
        float slope = 1.0f-dot(N,up)*dot(N,up);
        float strength = wet*smoothstep(0.02f,0.20f,slope)*kich_npc_drips.x;
        float bead = dot(float2(bx.z,bz.z),weights);
        O.Ne.xyz = normalize(N+gradient*strength*2.5f);
        O.C.a = saturate(O.C.a+(1.0f-O.C.a)*bead*strength*3.0f);
    }
}
#endif
