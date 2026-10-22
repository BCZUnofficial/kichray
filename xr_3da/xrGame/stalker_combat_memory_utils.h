#pragma once

// Stalker-only policy helpers. No engine/renderer dependencies and no save layout changes.
namespace stalker_combat_memory
{
typedef unsigned int time_type;

inline time_type saved_age(time_type now, time_type timestamp)
{
	return now >= timestamp ? now - timestamp : 0;
}

inline time_type restored_timestamp(time_type now, time_type age)
{
	return now >= age ? now - age : 0;
}

inline bool interval_elapsed(time_type now, time_type previous, time_type interval)
{
	// A level clock restart must not postpone the next update indefinitely.
	return now < previous || now - previous >= interval;
}

inline float recent_hit_weight(time_type now, time_type timestamp, time_type lifetime)
{
	if (!lifetime || timestamp > now || now - timestamp >= lifetime)
		return 0.f;
	return float(lifetime - (now - timestamp)) / float(lifetime);
}

inline float decay_suspicion(float value, float threshold, float elapsed_seconds, float forget_seconds)
{
	if (!(value > 0.f) || !(threshold > 0.f) || !(forget_seconds > 0.f))
		return 0.f;
	if (!(elapsed_seconds > 0.f))
		return value;
	const float decay = threshold * (elapsed_seconds / forget_seconds);
	return value > decay ? value - decay : 0.f;
}

inline bool meaningful_threat_improvement(float current_score, float candidate_score, float required_margin)
{
	// The comparison also rejects NaNs. Lower scores are more desirable.
	return candidate_score + required_margin < current_score;
}

inline bool relay_is_newer(time_type current_time, time_type relayed_time, bool currently_visible)
{
	// A report must never overwrite the receiver's own current sighting.
	return !currently_visible && relayed_time > current_time;
}
}
