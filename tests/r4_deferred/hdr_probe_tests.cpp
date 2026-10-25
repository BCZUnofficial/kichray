// P0-S11 GLO01 HDR probe: exact float transport through the RGBA8 strip,
// the strip layout PSHdrProbe writes, the combine_1 term split and the
// sun/per-light accumulator differences. Run from the repository root.
#include <cstdio>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "KichR4HdrProbe.h"
namespace {
int checks=0;
void Require(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
using namespace kich_r4_hdr_probe;
bool SameBits(float a,float b){return !memcmp(&a,&b,4);}
void Transport(){
    for(unsigned b=0;b<256;++b){const float unorm=float(b)/255.f;Require(unsigned(floorf(unorm*255.f+.5f))==b,"every byte survives the UNORM8 store exactly");}
    const float values[]={0.f,-0.f,1.f,.3f,-2.5f,1e-40f,3.4e38f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN(),65504.f,1.f/3.f};
    for(float v:values){unsigned char rgba[4];Encode(v,rgba);Require(SameBits(Decode(rgba,1,0,0),v),"float bits round-trip, including denormal, inf and NaN");}
}
// A strip as PSHdrProbe draws it: field k of sample j at column origin+k, row j.
struct Strip {uint32_t w,h;std::vector<unsigned char> rgba;
    Strip(uint32_t width,uint32_t height):w(width),h(height),rgba(size_t(width)*height*4,0){}
    void Put(unsigned column,unsigned sample,float v){Encode(v,&rgba[(size_t(sample)*w+column)*4]);}
    void Put(unsigned column,unsigned sample,const float* v,unsigned n){for(unsigned i=0;i<n;++i)Put(column+i,sample,v[i]);}};
float Field(unsigned sample,unsigned column){return float(sample)*1000+float(column);}
void Layout(){
    Require(Width==115 && Height==25 && LocalColumn+MaxLights*LightFields==Width,"strip is 115x25: combine 27, targets 8, bloom 12, sun 4, 16 lights x 4");
    Require(Frame::Fits(Width,Height) && !Frame::Fits(Width-1,Height) && !Frame::Fits(Width,Height-1),"strip must fit the swap chain");
    Strip strip(640,360);Frame f;f.Begin(640,360,3);
    for(unsigned j=0;j<Samples;++j)for(unsigned c=0;c<LocalColumn+3*LightFields;++c)strip.Put(c,j,Field(j,c));
    f.Decode(strip.rgba.data());
    Require(f.center[0]==320 && f.center[1]==180 && f.lights==3,"centre pixel and probed light count");
    const Sample& first=f.samples[0];const Sample& last=f.samples[Samples-1];
    Require(first.x==318 && first.y==178 && last.x==322 && last.y==182 && f.samples[7].x==320 && f.samples[7].y==179,"rows are the 5x5 pixels around the centre, row-major");
    const Sample& s=f.samples[13];
    Require(s.albedo[0]==Field(13,0) && s.gbuffer[3]==Field(13,7) && s.normal[2]==Field(13,10) && s.gloss==Field(13,11) && s.material==Field(13,12),"albedo, raw normal target, normal, gloss, class columns");
    Require(s.depth==Field(13,13) && s.distance==Field(13,14) && s.hdiffuse[0]==Field(13,15) && s.hspecular[2]==Field(13,20),"depth, distance, hmodel columns");
    Require(s.fog_amount==Field(13,21) && s.exposure==Field(13,22) && s.light[0]==Field(13,23) && s.light[3]==Field(13,26),"fog, exposure, combine accumulator columns");
    Require(s.low[0]==Field(13,TargetColumn) && s.high[3]==Field(13,TargetColumn+7),"stored low/high columns");
    Require(s.final_color[0]==Field(13,BloomColumn) && s.pre_bloom[0]==Field(13,BloomColumn+4) && s.bloom[3]==Field(13,BloomColumn+11),"composite, pre-bloom, bloom1 columns");
    Require(s.sun[0]==Field(13,SunColumn) && s.after_local[2][3]==Field(13,LocalColumn+11) && s.after_local[3][0]==0,"accumulator after PSSun and each PSLocal; unprobed lights stay zero");
    f.Begin(640,360,40);Require(f.lights==MaxLights,"lights beyond 16 are not probed");
}
Sample Lit(){Sample s;memset(&s,0,sizeof(s));
    const float albedo[4]={.5f,.4f,.2f,0},hd[3]={.1f,.2f,.3f},hs[3]={.01f,.02f,.03f},sun[4]={.2f,.2f,.2f,.05f},a[4]={1.4f,1.4f,1.4f,.9f},b[4]={1.6f,1.5f,1.4f,.95f};
    memcpy(s.albedo,albedo,16);memcpy(s.hdiffuse,hd,12);memcpy(s.hspecular,hs,12);memcpy(s.sun,sun,16);
    memcpy(s.after_local[0],a,16);memcpy(s.after_local[1],b,16);memcpy(s.light,b,16);
    s.depth=.5f;s.exposure=1;s.fog_amount=.25f;s.bloom[0]=.2f;s.bloom[1]=.1f;s.bloom[2]=0;s.bloom[3]=.5f;return s;}
void TermSplit(){
    const float fog[3]={.3f,.3f,.4f};const Sample s=Lit();const kich_r4_hdr_probe::Terms t=Split(s,fog);
    for(unsigned i=0;i<3;++i){const float sum=s.albedo[i]*(s.light[i]+s.hdiffuse[i])+s.light[3]+s.hspecular[i];
        Require(t.sum[i]==sum,"sum is combine_1 exactly: D*(L.rgb+hdiffuse)+L.a+hspecular");
        Require(fabsf(t.direct_diffuse[i]+t.direct_specular+t.indirect_diffuse[i]+t.indirect_specular[i]-sum)<1e-6f,"the four HDR terms add up to the sum");
        Require(fabsf(t.combined[i]-(sum*.75f+fog[i]*.25f))<1e-6f && t.scaled[i]==t.combined[i],"fog lerp, exposure 1");}
    Require(fabsf(t.bloom[0]-.1f)<1e-7f && t.bloom[2]==0,"bloom term is bloom.rgb*bloom.a");
    Frame f;f.Begin(640,360,2);for(unsigned j=0;j<Samples;++j){f.samples[j]=Lit();if(j>=20)f.samples[j].depth=1;}
    const Summary m=Summarize(f,fog);
    Require(m.geometry==20,"sky samples (depth 1) are excluded");
    const float zero[4]={0,0,0,0};const float total=DirectLuminance(s,zero,s.light);
    Require(fabsf(m.sun+m.lights[0]+m.lights[1]-total)<1e-5f,"sun and per-light shares add up to the accumulator combine reads");
    Require(fabsf(m.lights[0]-DirectLuminance(s,s.sun,s.after_local[0]))<1e-6f && fabsf(m.lights[1]-DirectLuminance(s,s.after_local[0],s.after_local[1]))<1e-6f,"each light is its own accumulator step");
    Require(m.DominantSource(2)==0,"the first lamp dominates this receiver");
    Require(!strcmp(m.Dominant(),"DIRECT_SPECULAR"),"specular 0.95 dominates direct diffuse ~0.6");
    Frame sky;sky.Begin(640,360,0);for(unsigned j=0;j<Samples;++j)sky.samples[j].depth=1;
    Require(!strcmp(Summarize(sky,fog).Dominant(),"NO_GEOMETRY"),"all-sky block names no culprit");
}
void Shader(){
    std::ifstream in("resources/shaders/r4/bridge_deferred.hlsl");std::stringstream text;text<<in.rdbuf();const std::string hlsl=text.str();
    Require(!hlsl.empty(),"HLSL source readable (run from the repository root)");
    Require(hlsl.find("float values[27]")!=std::string::npos && hlsl.find("min(field,26u)")!=std::string::npos,"PSHdrProbe carries CombineFields=27 values");
    Require(hlsl.find("int2(sample%5,sample/5)-2")!=std::string::npos,"PSHdrProbe samples the same 5x5 block as Frame::Decode");
    Require(hlsl.find("HdrProbePut(values,23,image1.Load(load))")!=std::string::npos && hlsl.find("values[22]=Exposure()")!=std::string::npos,"accumulator at 23, exposure at 22");
    Require(hlsl.find("HdrProbePut(values,8,image1.SampleLevel(surface1,(float2(at)+.5)/screen.xy,0))")!=std::string::npos,"bloom1 sampled at the pixel centre like PSBloomComposite");
}
}
int main(){try{Transport();Layout();TermSplit();Shader();printf("P0-S11 HDR probe: %d checks passed (CPU model; no GPU)\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
