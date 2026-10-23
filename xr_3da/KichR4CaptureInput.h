#ifndef KICH_R4_CAPTURE_INPUT_H
#define KICH_R4_CAPTURE_INPUT_H
#include "KichR4Stream.h"
#include "KichR4Capture.h"
#include <string.h>
namespace kich_r4_material {struct Descriptor;}
/* Small EXE-internal borrowed snapshots. This header has no engine or STL
 * dependency: the old PCH adapter uses the engine's original EH settings,
 * while parsing/allocations and their exception handling live in NoPCH code. */
namespace kich_r4_capture {
struct Style {
    uint32_t blend,src,dst,clamp,cull,depth,write,alpha_func;float alpha_ref,min_z,max_z;bool scissor;int32_t rect[4];
    Style():blend(0),src(5),dst(6),clamp(0),cull(KICH_R4_CULL_BACK),depth(1),write(1),alpha_func(8),alpha_ref(0),min_z(0),max_z(1),scissor(false){memset(rect,0,sizeof(rect));}
};
struct ModelInput {
    void* visual;const void* skeleton;uint32_t count,first,triangles,bone_count,skin_mode;
    float world[16],vp[16];const float* bones;Style style;
    /* Borrowed only until CaptureSubmitModel returns. Null uses source OGF metadata. */
    const char* shader;const char* texture;const char* secondary;uint32_t effect,shader_element,shader_pass;
    const kich_r4_material::Descriptor* material;
    bool object_light_valid;float object_hemi,object_sun;
};
struct UIInput {
    void* vb;void* ib;
    uint32_t stride,topology,base,start,vertex_count,first,primitives,position,color,uv;
    bool indexed;const char* texture;Style style;
};
}
bool KichR4_CaptureModelNeeded(void*,uint32_t& bones);
bool KichR4_CaptureUIActive();
void KichR4_CaptureFail(const char*);
void KichR4_CaptureModelFail(void*,const char*);
void KichR4_CaptureSubmitModel(const kich_r4_capture::ModelInput&);
void KichR4_CaptureSubmitUI(const kich_r4_capture::UIInput&);
#endif
