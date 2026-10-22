#!/usr/bin/env python3
"""Summarize schema-3 RenderSnapshot files without equating draws with objects.

Usage: python tools/analyze_render_snapshots.py snapshot*.txt --output analysis.json
Only recorded values are compared. Unknown values stay strings; no GPU/FPS/leak
or visible-pixel verdict is inferred. Input files are never modified.
"""
import argparse
import json
from collections import defaultdict
from pathlib import Path


def sections(text):
    result = []
    current = None
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("[") and line.endswith("]"):
            current = {"section": line[1:-1], "fields": {}}
            result.append(current)
        elif current is not None and "=" in line:
            key, value = line.split("=", 1)
            current["fields"][key] = value
    return result


def integer(value):
    if value is None:
        return None
    try:
        return int(value, 10)
    except (ValueError, TypeError):
        return None


def analyze(path):
    rows = sections(path.read_text(encoding="utf-8", errors="replace"))
    by_name = defaultdict(list)
    for row in rows:
        by_name[row["section"]].append(row["fields"])
    def first(name):
        return by_name[name][0] if by_name[name] else {}
    checks = []
    required = ("SNAPSHOT", "FRAME_IDENTITY", "COMPLETENESS", "TRACE_LOSS", "END")
    valid = first("SNAPSHOT").get("schema") == "3" and all(by_name[name] for name in required)
    checks.append({"invariant": "schema3_required_sections", "status": "PASS" if valid else "FAIL"})
    def equal(label, values, expected):
        known = all(value is not None for value in values) and expected is not None
        checks.append({"invariant": label, "status": "NOT_CHECKED" if not known else "PASS" if sum(values) == expected else "FAIL",
                       "components": values, "observed": expected})
    def accounting(name, fields, prefixes):
        for prefix in prefixes:
            omitted = integer(fields.get(prefix+"omitted", "0"))
            equal(name+":"+prefix+"observed=retained+dropped+omitted",
                  [integer(fields.get(prefix+"retained")), integer(fields.get(prefix+"dropped")), omitted], integer(fields.get(prefix+"observed")))
    trace = first("TRACE_LOSS")
    accounting("TRACE_LOSS", trace, ("draw_records_", "draw_bytes_", "summary_updates_"))
    for row in rows:
        name, fields = row["section"], row["fields"]
        if name.startswith("DICTIONARY "):
            accounting(name, fields, ("updates_",))
        elif name.startswith("PASS "):
            accounting(name, fields, ("draw_records_", "draw_bytes_"))
    if by_name["EVENT_TAPE"]:
        accounting("EVENT_TAPE", first("EVENT_TAPE"), ("",))
    aggregate_rows = [row["fields"] for row in rows if row["section"].startswith("DRAW_AGGREGATE ")]
    totals = first("DRAW_AGGREGATE_TOTALS")
    mode = first("CAPTURE_MODE")
    counter_mode = any("COUNTER" in value for value in mode.values())
    if not counter_mode:
        equal("DRAW_AGGREGATE:sum_groups+lost=observed", [integer(row.get("draw_records_observed")) for row in aggregate_rows]+[integer(totals.get("observations_lost"))], integer(totals.get("draw_records_observed")))
    if by_name["END"]:
        equal("END:draw_events=trace_observed", [integer(trace.get("draw_records_observed"))], integer(first("END").get("draw_events")))
    semantic = defaultdict(lambda: {"submitted_draw_calls": 0, "skipped_draw_calls": 0, "triangles_submitted": 0, "coalesced_groups": 0})
    for row in aggregate_rows:
        key = "|".join(row.get(field, "UNKNOWN") for field in ("pass", "semantic_type", "material_id", "vertex_shader", "pixel_shader"))
        output = semantic[key]
        for field in ("submitted_draw_calls", "skipped_draw_calls", "triangles_submitted"):
            value = integer(row.get(field))
            if value is not None:
                output[field] += value
        output["coalesced_groups"] += row.get("coalesced") == "1"
    resources = [row["fields"] for row in rows if row["section"] == "RECORD RESOURCE"]
    metadata = {name: first(name) for name in ("SNAPSHOT", "FRAME_IDENTITY", "UI_STATE", "UI_TRANSITION", "CAPTURE_MODE", "COMPLETENESS", "MEMORY", "MODEL_RENDERER", "DETAIL_RENDERER", "LIGHT_REGISTRY", "CPU_CAPTURE", "TIMING", "TIMINGS", "GPU_TIMING", "TELEMETRY_COST", "END")}
    gpu = by_name["GPU_TIMING_COMPLETION"]
    for completion in gpu:
        identity = first("FRAME_IDENTITY")
        valid = all(completion.get(key) == identity.get(key) and key in identity for key in ("capture_id", "engine_frame", "render_frame"))
        checks.append({"invariant": "gpu_completion_matches_original_frame", "status": "PASS" if valid else "FAIL"})
    return {"file": str(path.resolve()), "metadata": metadata,
            "checks": checks, "invariant_failures": sum(check["status"] == "FAIL" for check in checks),
            "semantic_groups": dict(semantic), "trace": trace, "aggregate_totals": totals,
            "dictionary_status": {row["section"][11:]: row["fields"] for row in rows if row["section"].startswith("DICTIONARY ")},
            "resource_observations": resources, "gpu_timing_completions": gpu,
            "interpretation": "Submitted draws are not unique objects or visible pixels. Resource observations are bounded; retained objects do not prove a leak. Counter profiles include diagnostic overhead."}


def group_cycles(reports):
    groups = defaultdict(list)
    for report in reports:
        identity = report["metadata"]["FRAME_IDENTITY"]
        transition = report["metadata"]["UI_TRANSITION"]
        group = identity.get("group_id", transition.get("group_id", "0"))
        if integer(group) not in (None, 0):
            # Session identity prevents merging unrelated process runs with equal IDs.
            session = identity.get("session_id", "NOT_RECORDED")
            groups[(session, group)].append(report)
    output = []
    for (session, group), members in groups.items():
        members.sort(key=lambda report: integer(report["metadata"]["FRAME_IDENTITY"].get("render_frame")) or 0)
        baseline = members[0]["metadata"]["UI_STATE"]
        comparisons = []
        memory_baseline = members[0]["metadata"]["MEMORY"]
        for report in members:
            current = report["metadata"]["UI_STATE"]
            delta = {key: {"baseline": baseline.get(key, "NOT_RECORDED"), "observed": current.get(key, "NOT_RECORDED")}
                     for key in sorted(set(baseline) | set(current)) if baseline.get(key) != current.get(key)}
            memory = report["metadata"]["MEMORY"]
            memory_delta = {key: integer(memory[key])-integer(memory_baseline[key]) for key in sorted(set(memory) & set(memory_baseline)) if integer(memory[key]) is not None and integer(memory_baseline[key]) is not None}
            comparisons.append({"memory_counter_difference_from_first_supplied_frame": memory_delta, "file": report["file"], "transition": report["metadata"]["UI_TRANSITION"], "ui_state_difference_from_first_supplied_frame": delta})
        output.append({"session_id": session, "group_id": group, "frames": comparisons,
                       "comparison_scope": "Only supplied files; first supplied frame is not assumed to be a valid game baseline. State changes are observations, not automatic defects."})
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshots", nargs="+", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    reports = [analyze(path) for path in args.snapshots]
    document = {"analysis_schema": 1, "snapshots": reports, "ui_cycles": group_cycles(reports)}
    text = json.dumps(document, ensure_ascii=False, indent=2)+"\n"
    if args.output:
        if args.output.resolve() in {path.resolve() for path in args.snapshots}:
            parser.error("output must differ from all input snapshots")
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 1 if any(report["invariant_failures"] for report in reports) else 0

if __name__ == "__main__":
    raise SystemExit(main())
