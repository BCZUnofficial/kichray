#ifndef KICH_WATER_RAIN_H
#define KICH_WATER_RAIN_H
// All patterns use world XZ, independent of camera movement.
float kich_water_hash(float2 p)
{
    float3 h=frac(float3(p.x,p.y,p.x)*0.1031f);
    h+=dot(h,h.yzx+33.33f);
    return frac((h.x+h.y)*h.z);
}
float kich_water_noise(float2 p)
{
    float2 cell=floor(p), f=frac(p);
    f=f*f*(3.0f-2.0f*f);
    return lerp(lerp(kich_water_hash(cell),kich_water_hash(cell+float2(1,0)),f.x),
                lerp(kich_water_hash(cell+float2(0,1)),kich_water_hash(cell+float2(1,1)),f.x),f.y);
}
float2 kich_water_rain_layer(float2 world, float time, float rain)
{
    float2 cell=floor(world/0.50f);
    float phase=kich_water_hash(cell+float2(11.3f,71.9f));
    float period=lerp(0.9f,1.5f,kich_water_hash(cell+float2(41.2f,6.7f)));
    float clock=time/period+phase;
    float cycle=floor(clock), age=frac(clock);
    float2 key=cell+float2(cycle*13.7f,cycle*27.3f);
    float2 center=0.20f+0.10f*float2(kich_water_hash(key+3.1f),kich_water_hash(key+17.8f));
    float2 delta=world-cell*0.50f-center;
    float radius=length(delta);
    float ring=radius-(0.025f+0.13f*age);
    float footprint=max(length(ddx(world)),length(ddy(world)));
    float lod=1.0f-smoothstep(0.015f,0.055f,footprint);
    float wave=sin(ring*100.0f)*exp(-ring*ring*1800.0f);
    float life=smoothstep(0.0f,0.08f,age)*(1.0f-smoothstep(0.65f,1.0f,age));
    float chance=saturate(rain*KICH_WATER_RAIN_DENSITY);
    // Smooth probability threshold prevents sudden density jumps as weather blends.
    float event=saturate((chance-kich_water_hash(key+49.1f))*12.0f);
    return delta/max(radius,0.001f)*(wave*life*event*rain*lod);
}
float2 kich_water_rain(float2 world, float time, float rain)
{
    float2 a=kich_water_rain_layer(world,time,rain);
    float2 b=kich_water_rain_layer(world+float2(7.173f,3.291f),time*1.13f+3.7f,rain);
    return (a+b)*KICH_WATER_RAIN_STRENGTH;
}
#endif
