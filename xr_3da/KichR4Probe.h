#ifndef KICH_R4_PROBE_H
#define KICH_R4_PROBE_H

/* Engine-side helper; not exported and not part of CRenderDevice/CEngineAPI. */
typedef void (*KichR4LogFn)(const char* message);
void KichR4_RunHardwareProbeIfRequested(const char* command_line, KichR4LogFn log);

#endif
