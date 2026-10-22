#pragma once

namespace stalker_combat_diagnostics
{
// Fixed storage and a global budget keep a trace of an entire squad bounded.
// Unsigned time differences also handle the engine clock wrapping naturally.
class TraceLimiter
{
public:
    enum { slot_count = 512, events_per_second = 64 };

    TraceLimiter() : m_window_start(0), m_window_count(0)
    {
        for (unsigned int i = 0; i != slot_count; ++i)
            m_entries[i].valid = false;
    }

    bool allow(unsigned int now, unsigned int npc_id, unsigned int channel, unsigned int message_hash)
    {
        Entry& entry = m_entries[(npc_id * 17u + channel) % slot_count];
        if (entry.valid && entry.npc_id == npc_id && entry.channel == channel)
        {
            const unsigned int interval = entry.message_hash == message_hash ? 1000u : 250u;
            if (now - entry.time < interval)
                return false;
        }

        if (now - m_window_start >= 1000u)
        {
            m_window_start = now;
            m_window_count = 0;
        }
        if (m_window_count >= events_per_second)
            return false;

        ++m_window_count;
        entry.npc_id = npc_id;
        entry.channel = channel;
        entry.message_hash = message_hash;
        entry.time = now;
        entry.valid = true;
        return true;
    }

private:
    struct Entry
    {
        unsigned int npc_id;
        unsigned int channel;
        unsigned int message_hash;
        unsigned int time;
        bool valid;
    };

    Entry m_entries[slot_count];
    unsigned int m_window_start;
    unsigned int m_window_count;
};

inline unsigned int trace_hash(const char* text, unsigned int seed = 2166136261u)
{
    for (; *text; ++text)
        seed = (seed ^ static_cast<unsigned char>(*text)) * 16777619u;
    return seed;
}
}
