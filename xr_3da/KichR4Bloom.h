#ifndef KICH_R4_BLOOM_H
#define KICH_R4_BLOOM_H
/* P0-C2: SoC/KichRay R2 phase_bloom on the CPU side. The shader side is
 * PSBloomBuild/PSBloomFilter/PSBloomFilterFast/PSBloomComposite. */
#include <math.h>
namespace kich_r4_bloom {
enum {Size=256}; // R2 BLOOM_size_X/Y
// r2_rendertarget_phase_bloom.cpp CalcGauss_k7, verbatim arithmetic.
// Samples: 0-central, -1..-7; w0=(-1..-4), w1=(-5,-6,-7, 0).
inline void GaussK7(float* w0,float* w1,float r=3.3f,float s_out=1.f){
    float W[8];float mag=0;
    for(int i=-7;i<=0;i++)W[-i]=expf(-float(i*i)/(2*r*r));
    for(int i=0;i<8;i++)mag+=i?2*W[i]:W[i];
    for(int i=0;i<8;i++)W[i]=s_out*W[i]/mag;
    w0[0]=W[1];w0[1]=W[2];w0[2]=W[3];w0[3]=W[4];
    w1[0]=W[5];w1[1]=W[6];w1[2]=W[7];w1[3]=W[0];
}
inline void GaussWave(float* w0,float* w1,float r_base=3.3f,float r_detail=1.f,float s_out=1.f){
    float t0[4],t1[4];GaussK7(w0,w1,r_base,s_out);GaussK7(t0,t1,r_detail,s_out);
    for(unsigned i=0;i<4;++i){w0[i]+=t0[i];w1[i]+=t1[i];}
}
// Horizontal kernel uses r2_ls_bloom_kernel_g, vertical scales it by h/w.
inline void FilterWeights(bool vertical,float kernel_g,float kernel_scale,unsigned width,unsigned height,float* w0,float* w1){
    const float kernel=vertical && width?kernel_g*float(height)/float(width):kernel_g;
    GaussWave(w0,w1,kernel,kernel/3.f,kernel_scale);
}
}
#endif
