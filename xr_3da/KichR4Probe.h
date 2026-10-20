#ifndef KICH_R4_PROBE_H
#define KICH_R4_PROBE_H
#include "KichR4LevelFiles.h"

/* Engine-side helper; not exported and not part of CRenderDevice/CEngineAPI. */
typedef void (*KichR4LogFn)(const char* message);
void KichR4_RunHardwareProbeIfRequested(const char* command_line, KichR4LogFn log,const KichR4LevelFiles* files=0);

#endif
