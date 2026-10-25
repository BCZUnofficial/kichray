#ifndef KICH_R4_HDR_PROBE_H
#define KICH_R4_HDR_PROBE_H
/* P0-S11 GLO01 HDR probe: the unclamped combine_1 terms at the 5x5 pixels
 * around the screen centre (the crosshair), with the light accumulator after
 * PSSun and after each PSLocal, the stored low/high targets and the bloom
 * composite. PSHdrProbe writes every float as its four little-endian bytes
 * into one RGBA8 pixel of a strip at the top-left of the swap-chain image
 * (column = field, row = sample); read_pixels returns it before postprocess
 * covers the image. No device ABI change: the strip lives one frame, unseen. */
#include <stdint.h>
#include <string.h>
#include <math.h>
namespace kich_r4_hdr_probe {
enum {Side=5,Samples=Side*Side,CombineFields=27,TargetFields=8,BloomFields=12,LightFields=4,MaxLights=16,
    CombineColumn=0,TargetColumn=CombineColumn+CombineFields,BloomColumn=TargetColumn+TargetFields,
    SunColumn=BloomColumn+BloomFields,LocalColumn=SunColumn+LightFields,Width=LocalColumn+MaxLights*LightFields,Height=Samples};
// PSHdrProbe light_direction.z.
enum Stage {LightStage=0,CombineStage=1,TargetStage=2,BloomStage=3};
// R2 phase_luminance weights (bloom_luminance_1.ps).
inline float Luminance(const float* c){return .3f*c[0]+.48f*c[1]+.22f*c[2];}
inline float Decode(const unsigned char* rgba,uint32_t width,unsigned column,unsigned sample){
    const unsigned char* p=rgba+(size_t(sample)*width+column)*4;
    const uint32_t u=uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;float f;memcpy(&f,&u,4);return f;}
// CPU model of PSHdrProbe + R8G8B8A8_UNORM: byte b leaves as b/255 and is
// stored as round(255*(b/255)).
inline void Encode(float value,unsigned char* rgba){uint32_t u;memcpy(&u,&value,4);
    for(unsigned i=0;i<4;++i){const float unorm=float((u>>(8*i))&255u)/255.f;rgba[i]=static_cast<unsigned char>(floorf(unorm*255.f+.5f));}}
struct Sample {
    int32_t x,y;
    // Combine inputs exactly as PSCombine reads them; gbuffer = raw normal target
    // (encoded xy, hemi, packed material); light = the accumulator combine sees.
    float albedo[4],gbuffer[4],normal[3],gloss,material,depth,distance,hdiffuse[3],hspecular[3],fog_amount,exposure,light[4];
    float sun[4],after_local[MaxLights][4]; // accumulator after PSSun / after PSLocal i
    float low[4],high[4];                    // stored generic0/generic1 right after combine
    float final_color[4],pre_bloom[4],bloom[4]; // composite, its input, bloom1 at the pixel
    bool Geometry()const{return depth<1;}
};
// combine_1 split into its four unclamped HDR terms (before fog and exposure).
struct Terms {float direct_diffuse[3],direct_specular,indirect_diffuse[3],indirect_specular[3],sum[3],combined[3],scaled[3],bloom[3];};
inline Terms Split(const Sample& s,const float fog[3]){Terms t;
    t.direct_specular=s.light[3];
    for(unsigned i=0;i<3;++i){t.direct_diffuse[i]=s.albedo[i]*s.light[i];t.indirect_diffuse[i]=s.albedo[i]*s.hdiffuse[i];t.indirect_specular[i]=s.hspecular[i];
        t.sum[i]=s.albedo[i]*(s.light[i]+s.hdiffuse[i])+s.light[3]+s.hspecular[i];
        t.combined[i]=t.sum[i]+(fog[i]-t.sum[i])*s.fog_amount;t.scaled[i]=t.combined[i]*s.exposure;t.bloom[i]=s.bloom[i]*s.bloom[3];}
    return t;}
// Direct light of one accumulator step in combine units: albedo*rgb + specular.
inline float DirectLuminance(const Sample& s,const float* before,const float* after){
    float rgb[3];for(unsigned i=0;i<3;++i)rgb[i]=s.albedo[i]*(after[i]-before[i]);return Luminance(rgb)+(after[3]-before[3]);}
struct Frame {
    uint32_t width,height,lights;int32_t center[2];
    uint64_t serials[MaxLights];
    Sample samples[Samples];
    Frame():width(0),height(0),lights(0){center[0]=center[1]=0;memset(serials,0,sizeof(serials));memset(samples,0,sizeof(samples));}
    static bool Fits(uint32_t w,uint32_t h){return w>=Width && h>=Height;}
    void Begin(uint32_t w,uint32_t h,uint32_t light_count){width=w;height=h;lights=light_count<MaxLights?light_count:MaxLights;
        center[0]=int32_t(w/2);center[1]=int32_t(h/2);memset(serials,0,sizeof(serials));}
    static void Read(const unsigned char* rgba,uint32_t w,unsigned column,unsigned sample,float* out,unsigned count){
        for(unsigned i=0;i<count;++i)out[i]=kich_r4_hdr_probe::Decode(rgba,w,column+i,sample);}
    // rgba: the full w x h image from read_pixels.
    void Decode(const unsigned char* rgba){
        for(unsigned j=0;j<Samples;++j){Sample& s=samples[j];s.x=center[0]+int32_t(j%Side)-Side/2;s.y=center[1]+int32_t(j/Side)-Side/2;
            const unsigned c=CombineColumn;
            Read(rgba,width,c,j,s.albedo,4);Read(rgba,width,c+4,j,s.gbuffer,4);Read(rgba,width,c+8,j,s.normal,3);
            s.gloss=kich_r4_hdr_probe::Decode(rgba,width,c+11,j);s.material=kich_r4_hdr_probe::Decode(rgba,width,c+12,j);
            s.depth=kich_r4_hdr_probe::Decode(rgba,width,c+13,j);s.distance=kich_r4_hdr_probe::Decode(rgba,width,c+14,j);
            Read(rgba,width,c+15,j,s.hdiffuse,3);Read(rgba,width,c+18,j,s.hspecular,3);
            s.fog_amount=kich_r4_hdr_probe::Decode(rgba,width,c+21,j);s.exposure=kich_r4_hdr_probe::Decode(rgba,width,c+22,j);Read(rgba,width,c+23,j,s.light,4);
            Read(rgba,width,TargetColumn,j,s.low,4);Read(rgba,width,TargetColumn+4,j,s.high,4);
            Read(rgba,width,BloomColumn,j,s.final_color,4);Read(rgba,width,BloomColumn+4,j,s.pre_bloom,4);Read(rgba,width,BloomColumn+8,j,s.bloom,4);
            Read(rgba,width,SunColumn,j,s.sun,4);
            memset(s.after_local,0,sizeof(s.after_local));
            for(unsigned i=0;i<lights;++i)Read(rgba,width,LocalColumn+i*LightFields,j,s.after_local[i],4);}
    }
    // Accumulator before light i: after PSSun for the first, else after light i-1.
    const float* Before(const Sample& s,unsigned i)const{return i?s.after_local[i-1]:s.sun;}
};
// Block means over geometry samples (depth < 1), as luminance.
struct Summary {
    unsigned geometry;float direct_diffuse,direct_specular,indirect_diffuse,indirect_specular,sum,scaled,exposure,fog_amount,
        low,high,pre_bloom,bloom,final_color,sun,lights[MaxLights],peak_scaled;unsigned peak_sample;
    const char* Dominant()const{const float v[4]={direct_diffuse,direct_specular,indirect_diffuse,indirect_specular};
        const char* names[4]={"DIRECT_DIFFUSE","DIRECT_SPECULAR","INDIRECT_DIFFUSE","INDIRECT_SPECULAR"};
        unsigned best=0;for(unsigned i=1;i<4;++i)if(v[i]>v[best])best=i;return geometry?names[best]:"NO_GEOMETRY";}
    // Largest direct source: -1 the sun, else the local light index.
    int DominantSource(unsigned light_count)const{int best=-1;float value=sun;
        for(unsigned i=0;i<light_count && i<MaxLights;++i)if(lights[i]>value){value=lights[i];best=int(i);}return best;}
};
inline Summary Summarize(const Frame& f,const float fog[3]){Summary m;memset(&m,0,sizeof(m));
    for(unsigned j=0;j<Samples;++j){const Sample& s=f.samples[j];if(!s.Geometry())continue;++m.geometry;const Terms t=Split(s,fog);
        m.direct_diffuse+=Luminance(t.direct_diffuse);m.direct_specular+=t.direct_specular;m.indirect_diffuse+=Luminance(t.indirect_diffuse);
        m.indirect_specular+=Luminance(t.indirect_specular);m.sum+=Luminance(t.sum);const float scaled=Luminance(t.scaled);m.scaled+=scaled;
        if(m.geometry==1 || scaled>m.peak_scaled){m.peak_scaled=scaled;m.peak_sample=j;}
        m.exposure+=s.exposure;m.fog_amount+=s.fog_amount;m.low+=Luminance(s.low);m.high+=Luminance(s.high);m.pre_bloom+=Luminance(s.pre_bloom);
        m.bloom+=Luminance(t.bloom);m.final_color+=Luminance(s.final_color);
        const float zero[4]={0,0,0,0};m.sun+=DirectLuminance(s,zero,s.sun);
        for(unsigned i=0;i<f.lights;++i)m.lights[i]+=DirectLuminance(s,f.Before(s,i),s.after_local[i]);}
    if(m.geometry){const float n=1.f/float(m.geometry);float* fields[]={&m.direct_diffuse,&m.direct_specular,&m.indirect_diffuse,&m.indirect_specular,&m.sum,&m.scaled,&m.exposure,&m.fog_amount,&m.low,&m.high,&m.pre_bloom,&m.bloom,&m.final_color,&m.sun};
        for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);++i)*fields[i]*=n;for(unsigned i=0;i<MaxLights;++i)m.lights[i]*=n;}
    return m;}
}
#endif
