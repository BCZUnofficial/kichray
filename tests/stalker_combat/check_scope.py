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
    "stalker_sound_data_visitor.cpp", "sight_manager.cpp",
    "stalker_danger_grenade_actions.cpp", "stalker_danger_grenade_actions.h",
    # AI-P7 owns a native prisluh gesture in the existing stalker animator.
    "stalker_animation_global.cpp", "stalker_animation_manager.cpp",
    "stalker_animation_manager.h", "stalker_animation_manager_inline.h",
    "stalker_animation_manager_update.cpp",
    "Level_Bullet_Manager.cpp", "Level_Bullet_Manager.h", "WeaponMagazined.cpp",
    "agent_member_manager.cpp", "agent_member_manager.h", "agent_enemy_manager.cpp",
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


def check_bullet_simulation(base):
    """Only an observation hook/value queue may be added to the bullet manager."""
    path = GAME + "Level_Bullet_Manager.cpp"
    before, after = git("show", base + ":" + path), (ROOT / path).read_bytes()
    if before == after:
        return
    if b"void CBulletManager::QueueNearMissSegment(" in before:
        previous = b"stalker->on_bullet_near_miss(point);"
        current = b"stalker->on_bullet_near_miss(point, segment.direction);"
        if before.count(previous) != 1 or after.count(current) != 1 or after.replace(current, previous, 1) != before:
            raise SystemExit("Existing bullet simulation/hit processing changed beyond the observed direction argument")
        path = GAME + "Level_Bullet_Manager.h"
        if git("show", base + ":" + path) != (ROOT / path).read_bytes():
            raise SystemExit("Existing bullet data/layout declarations changed")
        return
    begin = after.index(b"void CBulletManager::QueueNearMissSegment(")
    end = after.index(b"void CBulletManager::RegisterEvent(", begin)
    after = after[:begin] + after[end:]
    for addition in (b'#include "ai/stalker/ai_stalker.h"\n',
                     b"    m_near_miss_segments.clear();\n",
                     b"    QueueNearMissSegment(*bullet, RD, range);\n",
                     b"    CommitNearMissSegments();\n"):
        if after.count(addition) != 1:
            raise SystemExit("Missing/duplicated bullet observation hook")
        after = after.replace(addition, b"", 1)
    if before != after:
        raise SystemExit("Existing bullet simulation/hit processing changed")
    path = GAME + "Level_Bullet_Manager.h"
    before, after = git("show", base + ":" + path), (ROOT / path).read_bytes()
    begin = after.index(b"\n    // Worker writes value-only")
    end = after.index(b"    void CommitNearMissSegments();\n", begin) + len(b"    void CommitNearMissSegments();\n")
    if before != after[:begin] + after[end:]:
        raise SystemExit("Existing bullet data/layout declarations changed")


def check_weapon_fire(base):
    """Allow only the stalker safety veto before the unchanged live shot loop."""
    path = GAME + "WeaponMagazined.cpp"
    before, after = git("show", base + ":" + path), (ROOT / path).read_bytes()
    if before == after:
        return
    include = b'#include "ai/stalker/ai_stalker.h"\n'
    hook = b'''        CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(H_Parent());
        const bool initial_direction = m_iShotNum < m_iShootEffectorStart;
        if (stalker && !stalker->combat_safe_to_shoot(initial_direction ? m_vStartPos : p1,
            initial_direction ? m_vStartDir : d))
        {
            // Stop the existing queue before recoil, sound, ammo or ballistics.
            // A later planner decision can start a fresh burst after the ally clears.
            m_bFireSingleShot = false;
            FireEnd();
            fTime = _max(fTime, fTimeToFire);
            break;
        }
'''
    if after.count(include) != 1 or after.count(hook) != 1:
        raise SystemExit("Missing/duplicated stalker pre-shot safety hook")
    if after.replace(include, b"", 1).replace(hook, b"", 1) != before:
        raise SystemExit("Existing weapon accuracy, shot vectors, ammo or firing mechanics changed")


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
    check_bullet_simulation(base)
    check_weapon_fire(base)
    print(f"PASS: {len(paths)} changed/additional files are within the stalker AI scope against {base[:12]}")
    print("Renderer, shaders, graphics settings, core, physics and generic pathfinding are unchanged.")
    print("Project settings, accuracy, shot vectors and bullet simulation are preserved; weapon changes are limited to the stalker pre-shot safety veto.")


if __name__ == "__main__":
    main()
