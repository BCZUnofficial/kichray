#ifndef KICH_R4_CAPTURE_H
#define KICH_R4_CAPTURE_H
#include <stdint.h>
/* New free-function EXE exports; existing engine classes and vtables stay intact. */
#if defined(_WIN32)
#if defined(ENGINE_BUILD)
#define KICH_R4_CAPTURE_API __declspec(dllexport)
#else
#define KICH_R4_CAPTURE_API __declspec(dllimport)
#endif
#else
#define KICH_R4_CAPTURE_API
#endif
KICH_R4_CAPTURE_API bool KichR4_CaptureActive();
KICH_R4_CAPTURE_API void KichR4_CaptureModel(void* visual,void* skeleton,uint32_t vertices,uint32_t first,uint32_t triangles);
KICH_R4_CAPTURE_API void KichR4_CaptureHUD(bool begin);
KICH_R4_CAPTURE_API void KichR4_CaptureUIDraw(void* declaration,void* vb,void* ib,uint32_t stride,
    uint32_t topology,uint32_t base_vertex,uint32_t start_vertex,uint32_t vertex_count,uint32_t first_index,uint32_t primitives,bool indexed);
#undef KICH_R4_CAPTURE_API
void KichR4_CaptureEnable(bool);
void KichR4_CaptureBeginFrame(uint32_t width,uint32_t height,uint32_t engine_frame);
void KichR4_CaptureLoad(void* visual,const void* source,uint32_t bytes);
void KichR4_CaptureCopy(void* visual,const void* source);
void KichR4_CaptureForget(void* visual);
void KichR4_CaptureLock(void* buffer,const void* data,uint32_t offset,uint32_t capacity,uint32_t discard);
void KichR4_CaptureUnlock(void* buffer,uint32_t bytes);
void KichR4_CaptureBuffer(void* buffer,const void* data,uint32_t bytes);
void KichR4_CaptureForgetBuffer(void* buffer);
namespace kich_r4_capture {struct State;struct Batch;}
kich_r4_capture::State* KichR4_CaptureState();
const kich_r4_capture::Batch* KichR4_CapturedFrame();
#endif
