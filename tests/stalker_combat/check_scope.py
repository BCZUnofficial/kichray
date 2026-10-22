#!/usr/bin/env python3
"""Verify an AI change against an explicit base, including untracked additions.

This is a review gate for the user's requested scope, not a renderer runtime test.
"""
from pathlib import Path
import argparse
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
GAME = "xr_3da/xrGame/"
ALLOWED_GAME_FILES = {
    "ai/stalker/ai_stalker.h", "ai/stalker/ai_stalker.cpp",
    "ai/stalker/ai_stalker_cover.cpp", "ai/stalker/ai_stalker_fire.cpp", "ai/stalker/ai_stalker_misc.cpp",
    "console_commands.cpp", "xrGame.vcxproj", "xrGame.vcxproj.filters",
    "stalker_sound_data_visitor.cpp",
    "enemy_manager.h", "enemy_manager.cpp", "enemy_manager_inline.h",
    "memory_manager.h", "memory_manager.cpp", "memory_manager_inline.h",
    "visual_memory_manager.h", "visual_memory_manager.cpp", "visual_memory_manager_inline.h",
    "sound_memory_manager.h", "sound_memory_manager.cpp", "sound_memory_manager_inline.h",
    "hit_memory_manager.h", "hit_memory_manager.cpp", "hit_memory_manager_inline.h",
    "danger_manager.h", "danger_manager.cpp", "danger_manager_inline.h",
    "object_handler_planner.h", "object_handler_planner.cpp", "object_handler_planner_inline.h",
}


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def allowed(path):
    if path in {"resources/scripts/xr_combat_zombied.script", "docs/STALKER_COMBAT_AI_RU.md"}:
        return True
    if path.startswith("tests/stalker_combat/"):
        return True
    if path.startswith(GAME + "stalker_combat_"):
        return True
    return path.startswith(GAME) and path[len(GAME):] in ALLOWED_GAME_FILES


def normalized_xml(element):
    return (element.tag, sorted(element.attrib.items()), (element.text or "").strip(),
            [normalized_xml(child) for child in element])


def check_project(base, path):
    before = ET.fromstring(git("show", base + ":" + path))
    after = ET.fromstring((ROOT / path).read_bytes())
    original_names = {item.attrib.get("Include") for item in before.iter()}
    for parent in after.iter():
        for child in list(parent):
            name = child.attrib.get("Include", "")
            kind = child.tag.rsplit("}", 1)[-1]
            if name and name not in original_names and kind in {"ClCompile", "ClInclude"}:
                if not name.startswith("stalker_combat_") or not (ROOT / GAME / name).is_file():
                    raise SystemExit(f"Unexpected or missing project source in {path}: {name}")
                parent.remove(child)
    if normalized_xml(before) != normalized_xml(after):
        raise SystemExit(f"Existing project settings/source entries changed in {path}")


def check_preserved_region(base, path, start, end):
    before = git("show", base + ":" + path)
    after = (ROOT / path).read_bytes()
    def region(source):
        first = source.index(start)
        return source[first:source.index(end, first)]
    if region(before) != region(after):
        raise SystemExit(f"Previously fixed accuracy or existing shot mechanics changed in {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", required=True, help="audited commit before the stalker changes")
    args = parser.parse_args()
    base = git("rev-parse", "--verify", args.base + "^{commit}").decode().strip()
    paths = set(git("diff", "--name-only", "-z", base, "--").decode().split("\0"))
    paths.update(git("ls-files", "--others", "--exclude-standard", "-z").decode().split("\0"))
    paths.discard("")
    unexpected = sorted(path for path in paths if not allowed(path))
    if unexpected:
        raise SystemExit("Changes outside the reviewed stalker scope:\n" + "\n".join(unexpected))
    for path in (GAME + "xrGame.vcxproj", GAME + "xrGame.vcxproj.filters"):
        check_project(base, path)
    check_preserved_region(base, GAME + "ai/stalker/ai_stalker.cpp",
                           b"BOOL CAI_Stalker::net_Spawn(", b"void CAI_Stalker::net_Destroy(")
    check_preserved_region(base, GAME + "ai/stalker/ai_stalker_fire.cpp",
                           b"float CAI_Stalker::GetWeaponAccuracy()", b"void CAI_Stalker::Hit(")
    print(f"PASS: {len(paths)} changed/additional files are within the stalker AI scope against {base[:12]}")
    print("Renderer, shaders, graphics settings, core, physics and generic pathfinding are unchanged.")
    print("Project settings, previous rank/zoom accuracy fixes and shot-vector mechanics are preserved.")


if __name__ == "__main__":
    main()
