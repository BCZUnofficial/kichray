#ifndef KICH_R4_RESOURCE_PROBE_H
#define KICH_R4_RESOURCE_PROBE_H
#include "KichR4Resources.h"
#include "KichR4Probe.h"

/* Internal EXE test helpers, never exported by R4 or linked to engine PCH. */
bool KichR4_ResourceSelfTest(const KichR4ResourceAPI& api, KichR4LogFn log);
void KichR4_CheckResourceBridge(void* module, KichR4LogFn log);
#endif
