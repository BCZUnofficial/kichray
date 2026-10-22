#include "stdafx.h"
#include "stalker_combat_diagnostics.h"
#include "stalker_combat_trace_policy.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

TestDevice Device = {0};
static std::vector<std::string> messages;

void Msg(const char* format, ...)
{
    char text[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    messages.push_back(text);
}

int main()
{
    using namespace stalker_combat_diagnostics;
    TraceLimiter limiter;
    assert(limiter.allow(100, 1, fire, 10));
    assert(!limiter.allow(349, 1, fire, 11));
    assert(limiter.allow(350, 1, fire, 11));
    assert(!limiter.allow(1349, 1, fire, 11));
    assert(limiter.allow(1350, 1, fire, 11));
    assert(limiter.allow(1350, 1, grenade, 11));
    assert(limiter.allow(1350, 2, fire, 11));

    // A storm of distinct NPCs and colliding table slots is globally bounded.
    TraceLimiter burst;
    unsigned int count = 0;
    for (unsigned int i = 0; i < 2000; ++i)
        count += burst.allow(20, i * TraceLimiter::slot_count, target, i) ? 1 : 0;
    assert(count == TraceLimiter::events_per_second);
    assert(burst.allow(1020, 4, fire, 123));
    // Verify the exact budget-window boundary independently.
    TraceLimiter boundary;
    for (unsigned int i = 0; i < TraceLimiter::events_per_second; ++i)
        assert(boundary.allow(0, i, fire, i));
    assert(!boundary.allow(999, 500, fire, 0));
    assert(boundary.allow(1000, 500, fire, 0));

    TraceLimiter wrapping;
    const unsigned int maximum = std::numeric_limits<unsigned int>::max();
    assert(wrapping.allow(maximum - 100, 10, cover, 1));
    assert(!wrapping.allow(50, 10, cover, 2));
    assert(wrapping.allow(200, 10, cover, 2));
    assert(trace_hash("cover") == trace_hash("cover"));
    assert(trace_hash("cover") != trace_hash("fire"));

    // Exercise the real production logging adapter, not just the limiter.
    assert(trace_enabled == 0);
    assert(trace_object_id == -1);
    log(7, fire, "blocked", "target=%u", 12u);
    assert(messages.empty());
    trace_enabled = 1;
    trace_object_id = 7;
    log(8, fire, "blocked", "target=%u", 12u);
    assert(messages.empty());
    log(7, fire, "blocked", "target=%u", 12u);
    assert(messages.size() == 1);
    assert(messages.back().find("npc=7 event=fire reason=blocked target=12") != std::string::npos);
    log(7, fire, "blocked", "target=%u", 12u);
    assert(messages.size() == 1);
    Device.dwTimeGlobal = 1000;
    log(7, fire, "blocked", "target=%u", 12u);
    assert(messages.size() == 2);
    trace_object_id = -1;
    log(8, target, "changed", "target=%u", 14u);
    assert(messages.size() == 3);
    const std::string long_message(3000, 'x');
    log(9, action, "begin", "%s", long_message.c_str());
    assert(messages.size() == 4);
    assert(messages.back().size() < 1200);
    log(9, event_count, "invalid", "ignored");
    assert(messages.size() == 4);
    puts("PASS: stalker diagnostics defaults, filtering, throttling, clock wrap and production adapter");
}
