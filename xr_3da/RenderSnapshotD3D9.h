#ifndef KICH_RENDER_SNAPSHOT_D3D9_H
#define KICH_RENDER_SNAPSHOT_D3D9_H
#include <stdint.h>
/* EXE-side read-only SDK adapter; no COM pointer crosses the renderer ABI. */
void RenderSnapshot_D3D9Resources(void* device,uint32_t stride);
#endif
