#pragma once

// Optional stalker-only decisions trace. This does not draw debug geometry or
// query the renderer. Kept available in Release for reproducible play tests.
namespace stalker_combat_diagnostics
{
enum Event
{
    target,
    memory,
    cover,
    movement,
    fire,
    grenade,
    action,
    event_count
};

extern int trace_enabled;
extern int trace_object_id;

bool enabled(unsigned int npc_id);
void log(unsigned int npc_id, Event event, const char* reason, const char* format, ...);
}
