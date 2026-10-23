#!/usr/bin/env python3
"""Exercise stalker fire policies and extracted production gate methods without xrGame."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
GAME = ROOT / "xr_3da/xrGame"


def production_function(source, signature):
    """Keep the complete production function, including its signature and body."""
    assert source.count(signature) == 1, signature
    start = source.index(signature)
    begin = source.index("{", start)
    depth = 1
    end = begin + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[start:end] + "\n"


def integration_source():
    source = (GAME / "ai/stalker/ai_stalker_fire.cpp").read_bytes().decode("cp1251")
    enum_start = source.index("enum ECombatFireBlockReason")
    enum_end = source.index("};", enum_start) + 2
    result = [source[enum_start:enum_end]]
    for signature in (
        "void CAI_Stalker::can_kill_entity_from(",
        "IC float CAI_Stalker::start_pick_distance(",
        "void CAI_Stalker::update_can_kill_info(",
        "bool CAI_Stalker::combat_target_position(",
        "LPCSTR CAI_Stalker::combat_fire_block_reason(",
        "u32 CAI_Stalker::combat_fire_blocked_time(",
        "u32 CAI_Stalker::combat_suppression_remaining(",
        "bool CAI_Stalker::combat_fire_solution(",
        "bool CAI_Stalker::fire_make_sense(",
        "bool CAI_Stalker::combat_safe_to_shoot(",
        "void CAI_Stalker::on_weapon_shot_start(",
    ):
        result.append(production_function(source, signature))
    return "\n".join(result)


def burst_integration_source():
    planner = (GAME / "object_handler_planner.cpp").read_bytes().decode("cp1251")
    base = (GAME / "stalker_combat_action_base.cpp").read_bytes().decode("cp1251")
    result = [production_function(planner, "IC ObjectHandlerSpace::EWorldProperties CObjectHandlerPlanner::object_property(")]
    result.append(production_function(planner, "void CObjectHandlerPlanner::set_goal("))
    for signature in (
        "float combat_known_distance(",
        "void CStalkerActionCombatBase::fire(",
        "void CStalkerActionCombatBase::aim_ready(",
        "void CStalkerActionCombatBase::aim_ready_force_full(",
        "void CStalkerActionCombatBase::select_queue_params(",
    ):
        result.append(production_function(base, signature))
    weapon = (GAME / "WeaponMagazined.cpp").read_bytes().decode("cp1251")
    body = production_function(weapon, "void CWeaponMagazined::state_Fire(")
    begin = body.index("\twhile (!m_magazine.empty()")
    end = body.index("\n\tUpdateSounds();", begin)
    result.append("void CWeaponMagazined::state_Fire(float) {\n"
                  "Fvector p1=holder.fixture_origin, d=holder.fixture_direction;\n" +
                  body[begin:end] + "\n}\n")
    result.append("void CWeaponMagazined::advance_for_test(int shots_available) {\n"
                  "assert(shots_available > 0);\nfTime = -float(shots_available - 1);\nstate_Fire(0.f);\n}\n")
    update = production_function(weapon, "void CWeaponMagazined::UpdateCL(")
    begin = update.index("\t\tcase eFire:") + len("\t\tcase eFire:")
    end = update.index("\n\t\t\tbreak;", begin)
    result.append("void CWeaponMagazined::update_fire_for_test(float dt) {\n" +
                  update[begin:end] + "\n}\n")
    return "\n".join(result)


def main():
    cxx = os.environ.get("CXX", "g++")
    with tempfile.TemporaryDirectory(prefix="stalker-fire-") as folder:
        temporary = Path(folder)
        binary = temporary / "fire-policy-tests"
        subprocess.run([
            cxx, "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-I" + str(GAME), str(HERE / "fire_policy_tests.cpp"), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
        (temporary / "fire_gate_production.inc").write_text(integration_source())
        (temporary / "fire_burst_production.inc").write_text(burst_integration_source())
        flags = [
            cxx, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-I" + str(GAME), "-I" + str(HERE), "-I" + str(temporary),
        ]
        # The executor's process sandbox prevents LSan from inspecting /proc.
        # Keep address/undefined checks; heap ownership is not exercised here.
        environment = os.environ.copy()
        options = environment.get("ASAN_OPTIONS", "")
        environment["ASAN_OPTIONS"] = options + (":" if options else "") + "detect_leaks=0"
        for name in ("fire_gate", "fire_burst"):
            integration = temporary / (name + "-integration-tests")
            # select_queue_params contains a pre-existing /**/ marker inside a
            # disabled block. Keep the extracted source intact.
            extra = ["-Wno-comment"] if name == "fire_burst" else []
            subprocess.run(flags + extra + [str(HERE / (name + "_integration_tests.cpp")), "-o", str(integration)], check=True)
            subprocess.run([str(integration)], check=True, env=environment)


if __name__ == "__main__":
    main()
