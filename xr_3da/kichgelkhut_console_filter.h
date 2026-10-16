#pragma once

#include <cstring>

namespace kichgelkhut_console
{
inline bool is_debug_config_command(const char* command)
{
    static const char* names[] = {
        "r__dtex_range", "r__ssa_glod_end", "r__ssa_glod_start",
        "r__wallmark_ttl", "rs_detail", "rs_skeleton_update", "vid_bpp"
    };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (std::strcmp(command, names[i]) == 0) return true;
    return false;
}

// Match one exact command token; do not swallow typos or similarly named keys.
inline bool debug_config_command(const char* line, char (&command)[128])
{
    const char* first = line + std::strspn(line, " \t\r\n");
    const size_t count = std::strcspn(first, " \t\r\n");
    if (!count || count >= sizeof(command)) return false;
    std::memcpy(command, first, count);
    command[count] = 0;
    return is_debug_config_command(command);
}

// Display-only filter. LogFile and bin/logs keep every original message.
inline bool visible(const char* line)
{
    if (!line) return false;
    static const char* prefixes[] = {
        "* [prefetch] time:", "* [prefetch] memory:",
        "* phase time:", "* phase cmem:",
        "* [Loading VB] ", "* [Loading IB] ",
        "* [config] Skipped unavailable debug command: "
    };
    for (unsigned i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i)
        if (std::strncmp(line, prefixes[i], std::strlen(prefixes[i])) == 0) return false;
    return true;
}
}
