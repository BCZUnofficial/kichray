#ifndef KICH_R4_NATIVE_CPU_H
#define KICH_R4_NATIVE_CPU_H
/* ABI adapters for the old CPU loaders. These objects have no device, runtime,
 * GPU allocation, or draw API. Only xrRender_R4 owns graphics resources. */
#include <d3d9.h>
#include <stdint.h>
#if defined(ENGINE_BUILD)
#define KICH_CPU_API __declspec(dllexport)
#else
#define KICH_CPU_API __declspec(dllimport)
#endif
KICH_CPU_API HRESULT KichR4_CPUVertexBuffer(uint32_t bytes,IDirect3DVertexBuffer9** out);
KICH_CPU_API HRESULT KichR4_CPUIndexBuffer(uint32_t bytes,IDirect3DIndexBuffer9** out);
KICH_CPU_API HRESULT KichR4_CPUDeclaration(const D3DVERTEXELEMENT9* elements,IDirect3DVertexDeclaration9** out);
KICH_CPU_API HRESULT KichR4_CPUTexture(const char* name,uint32_t width,uint32_t height,IDirect3DTexture9** out);
KICH_CPU_API const unsigned char* KichR4_CPUBuffer(void* resource,uint32_t& bytes);
KICH_CPU_API const D3DVERTEXELEMENT9* KichR4_CPUElements(void* declaration,uint32_t& count);
KICH_CPU_API const char* KichR4_CPUTextureName(void* texture);
KICH_CPU_API const unsigned char* KichR4_CPUTexturePixels(void* texture,uint32_t& width,uint32_t& height,uint64_t& revision);
KICH_CPU_API void KichR4_CPUTextureNameSet(void* texture,const char* name);
struct KichR4CPUState {uint32_t type,a,b,c;};
KICH_CPU_API IDirect3DStateBlock9* KichR4_CPUStates(const KichR4CPUState* states,uint32_t count);
#endif
