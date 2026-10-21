#ifndef KICH_R4_NATIVE_H
#define KICH_R4_NATIVE_H
#include <stdint.h>
#if defined(_WIN32) && !defined(NO_ENGINE_API)
# if defined(ENGINE_BUILD)
#  define KICH_NATIVE_API __declspec(dllexport)
# else
#  define KICH_NATIVE_API __declspec(dllimport)
# endif
#else
# define KICH_NATIVE_API
#endif
class IRender_interface;
class IKichRenderDevice;
class ShaderElement;
class CTexture;
struct KichR4GameFrame;
struct KichR4LevelFiles;
namespace kich_r4_capture {struct Style;struct Batch;}

/* Process selection is fixed before the first renderer or GPU is created. */
KICH_NATIVE_API bool KichR4_NativeActive();
KICH_NATIVE_API bool KichR4_NativeInlineGeometry(void* visual);
bool KichR4_NativeSelect(const char* parameters);
IRender_interface* KichR4_CreateNativeRenderer();
IKichRenderDevice& KichR4_NativeDevice();
KICH_NATIVE_API void KichR4_NativeSetState(uint32_t state,uint32_t value);
KICH_NATIVE_API uint32_t KichR4_NativeGetState(uint32_t state);
KICH_NATIVE_API void KichR4_NativeViewport(float near_depth,float far_depth);
KICH_NATIVE_API void KichR4_NativeScissor(const int* rect);
KICH_NATIVE_API void KichR4_NativeElement(ShaderElement* element);
KICH_NATIVE_API void KichR4_NativeTexture(uint32_t stage,CTexture* texture);
KICH_NATIVE_API void KichR4_NativeDraw(void* declaration,void* vertices,void* indices,
    uint32_t stride,uint32_t topology,uint32_t base,uint32_t start,
    uint32_t vertex_count,uint32_t first,uint32_t primitives,bool indexed);
void KichR4_NativeFillRect(int32_t left,int32_t top,int32_t right,int32_t bottom,uint32_t color);
bool KichR4_NativeStyle(kich_r4_capture::Style& style);
bool KichR4_NativeCreate(void* window,uint32_t width,uint32_t height,
    const KichR4LevelFiles& files,void (*log)(const char*));
bool KichR4_NativeResize(uint32_t width,uint32_t height);
double KichR4_NativeQueueTime();
void KichR4_NativeBegin(uint32_t width,uint32_t height,uint32_t frame);
void KichR4_NativeMenuPP(bool enabled);
bool KichR4_NativePresent(const KichR4GameFrame& input);
void KichR4_NativeDestroy();
void KichR4_NativeLevel(const char* name);
KICH_NATIVE_API void KichR4_NativeTextureChanged(void* texture);
KICH_NATIVE_API void KichR4_NativeForgetTexture(void* texture);
KICH_NATIVE_API const char* KichR4_NativeTextureName(void* texture);
bool KichR4_NativeScreenshot(uint32_t mode,const char* name);
void KichR4_NativeClear(uint32_t color);
void KichR4_NativeSetInterval(bool vsync);
void KichR4_NativePumpLoading();
#undef KICH_NATIVE_API
#endif
