#ifndef RENDER_SNAPSHOT_MATERIAL_H
#define RENDER_SNAPSHOT_MATERIAL_H
#include "RenderSnapshot.h"
#include <stdint.h>
struct ShaderElement;
/* Diagnostic sidecars deliberately do not extend Shader/SPass or a renderer ABI.
 * They retain bounded source metadata created before a capture. */
#if defined(_WIN32) && !defined(NO_ENGINE_API)
# if defined(ENGINE_BUILD)
#  define SNAPSHOT_MATERIAL_API __declspec(dllexport)
# else
#  define SNAPSHOT_MATERIAL_API __declspec(dllimport)
# endif
#else
# define SNAPSHOT_MATERIAL_API
#endif
#if defined(_EDITOR)
inline void RenderSnapshot_RegisterMaterialElement(const void*,uint32_t,const char*,const char*,const char*,const char*,const char*,uint32_t,uint64_t=UINT64_MAX){}
inline void RenderSnapshot_ForgetMaterialElement(const void*){}
inline void RenderSnapshot_CompilerBegin(const void*){}
inline void RenderSnapshot_CompilerSampler(const void*,const char*,const char*,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t){}
inline void RenderSnapshot_CompilerEnd(const void*,const void*){}
inline void RenderSnapshot_ForgetMaterialPass(const void*){}
inline void RenderSnapshot_MaterialElement(ShaderElement*,uint32_t){}
inline void RenderSnapshot_MaterialSelection(ShaderElement*,uint32_t,uint32_t,uint32_t,const char*){}
inline void RenderSnapshot_RegisterShaderVariant(const char*,const char*,const void*,uint32_t,const void*,uint32_t,const char* const*,const char* const*,uint32_t){}
inline void RenderSnapshot_ShaderIncludesBegin(){}
inline void RenderSnapshot_ShaderInclude(const char*,const void*,uint32_t){}
#else
SNAPSHOT_MATERIAL_API void RenderSnapshot_RegisterMaterialElement(const void*,uint32_t,const char*,const char*,const char*,const char*,const char*,uint32_t,uint64_t=UINT64_MAX);
SNAPSHOT_MATERIAL_API void RenderSnapshot_ForgetMaterialElement(const void*);
SNAPSHOT_MATERIAL_API void RenderSnapshot_CompilerBegin(const void*);
SNAPSHOT_MATERIAL_API void RenderSnapshot_CompilerSampler(const void*,const char*,const char*,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
SNAPSHOT_MATERIAL_API void RenderSnapshot_CompilerEnd(const void*,const void*);
SNAPSHOT_MATERIAL_API void RenderSnapshot_ForgetMaterialPass(const void*);
SNAPSHOT_MATERIAL_API void RenderSnapshot_MaterialElement(ShaderElement*,uint32_t);
SNAPSHOT_MATERIAL_API void RenderSnapshot_MaterialSelection(ShaderElement*,uint32_t,uint32_t,uint32_t,const char*);
SNAPSHOT_MATERIAL_API void RenderSnapshot_RegisterShaderVariant(const char*,const char*,const void*,uint32_t,const void*,uint32_t,const char* const*,const char* const*,uint32_t);
SNAPSHOT_MATERIAL_API void RenderSnapshot_ShaderIncludesBegin();
SNAPSHOT_MATERIAL_API void RenderSnapshot_ShaderInclude(const char*,const void*,uint32_t);
#endif
#undef SNAPSHOT_MATERIAL_API
#endif
