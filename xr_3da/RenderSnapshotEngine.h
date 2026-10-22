#pragma once
#if defined(_EDITOR)
inline void RenderSnapshot_EngineDeviceEvent(const char*){}
#else
void RenderSnapshot_EngineDeviceEvent(const char* reason);
#endif
