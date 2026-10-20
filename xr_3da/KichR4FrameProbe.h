#ifndef KICH_R4_FRAME_PROBE_H
#define KICH_R4_FRAME_PROBE_H
#include "KichR4Probe.h"
/* false means cleanup did not finish: retain the loaded module so no live
 * graphics object loses its DLL implementation before process shutdown. */
bool KichR4_CheckFrameBridge(void* module,KichR4LogFn log);
#endif
