#include "stdafx.h"
#include "stalker_combat_diagnostics.h"
#include "stalker_combat_trace_policy.h"
#include <cstdarg>
#include <cstdio>

namespace stalker_combat_diagnostics
{
int trace_enabled = 0;
int trace_object_id = -1;

bool enabled(unsigned int npc_id)
{
    return trace_enabled != 0 && (trace_object_id < 0 || npc_id == static_cast<unsigned int>(trace_object_id));
}

void log(unsigned int npc_id, Event event, const char* reason, const char* format, ...)
{
    if (!enabled(npc_id) || event < target || event >= event_count)
        return;

    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format ? format : "", args);
    va_end(args);
    message[sizeof(message) - 1] = 0;
    if (!reason)
        reason = "unspecified";

    static TraceLimiter limiter;
    static xrCriticalSection lock;
    const unsigned int now = Device.dwTimeGlobal;
    const unsigned int hash = trace_hash(message, trace_hash(reason));
    lock.Enter();
    const bool emit = limiter.allow(now, npc_id, static_cast<unsigned int>(event), hash);
    lock.Leave();
    if (!emit)
        return;

    static const char* const names[] = {"target", "memory", "cover", "movement", "fire", "grenade", "action"};
    Msg("[stalker_ai] t=%u npc=%u event=%s reason=%s %s", now, npc_id, names[event], reason, message);
}
}
