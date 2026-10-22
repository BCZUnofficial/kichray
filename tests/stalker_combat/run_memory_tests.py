#!/usr/bin/env python3
"""Run stalker memory/selection regressions using extracted production functions."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
GAME = ROOT / "xr_3da/xrGame"


def source(name):
    return (GAME / name).read_bytes().decode("cp1251")


def function(text, signature):
    start = text.index(signature)
    begin = text.index("{", start)
    depth = 1
    end = begin + 1
    while depth:
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
        end += 1
    return text[start:end] + "\n"


def integration_source():
    enemy_text = source("enemy_manager.cpp")
    parts = ["\n".join(line for line in enemy_text.splitlines()
                       if line.startswith("static const ") or line == "#define USE_EVALUATOR")]
    for name, class_name, suffix in (
        ("visual_memory_manager.cpp", "CVisualMemoryManager", "Visual"),
        ("sound_memory_manager.cpp", "CSoundMemoryManager", "Sound"),
        ("hit_memory_manager.cpp", "CHitMemoryManager", "Hit"),
    ):
        text = source(name)
        for mode, argument in (("save", "const Record& record"), ("load", "Record& object")):
            body = function(text, "void " + class_name + "::" + mode + "(")
            start = body.index("#ifdef USE_LEVEL_TIME")
            end = body.index("#endif // USE_FIRST_LEVEL_TIME", start)
            end = body.index("\n", end)
            block = body[start:end]
            prefix = "const Record* I = &record;\n" if mode == "save" else ""
            parts.append("void " + mode + suffix + "(Packet& packet, " + argument + ", bool m_stalker) {\n" + prefix + block + "\n}\n")

    text = enemy_text
    for signature in (
        "float CEnemyManager::evaluate(",
        "bool CEnemyManager::change_from_wounded(",
        "IC bool CEnemyManager::enemy_inertia(",
        "void CEnemyManager::on_enemy_change(",
        "void CEnemyManager::remove_wounded(",
        "void CEnemyManager::process_wounded(",
        "bool CEnemyManager::need_update(",
        "void CEnemyManager::try_change_stalker_enemy(",
        "void CEnemyManager::try_change_enemy(",
    ):
        parts.append(function(text, signature))

    visual_text = source("visual_memory_manager.cpp")
    visual_inline = source("visual_memory_manager_inline.h")
    parts.append(function(visual_inline, "IC bool CVisualMemoryManager::enabled("))
    parts.append(function(visual_inline, "IC void CVisualMemoryManager::enable("))
    for signature in (
        "bool CVisualMemoryManager::visible_right_now(",
        "bool CVisualMemoryManager::visible_now(",
        "void CVisualMemoryManager::add_visible_object(const CObject*",
        "void CVisualMemoryManager::add_visible_object(const CVisibleObject",
        "u32 CVisualMemoryManager::direct_visible_time(",
        "bool CVisualMemoryManager::direct_visible_in_update(",
        "void CVisualMemoryManager::reset_direct_sight_update(",
        "void CVisualMemoryManager::remember_direct_sight(",
        "void CVisualMemoryManager::forget_direct_sight(",
        "bool CVisualMemoryManager::add_shared_memory(",
    ):
        parts.append(function(visual_text, signature))
    for signature in ("void CVisualMemoryManager::reinit(", "void CVisualMemoryManager::load("):
        assert "m_direct_sights.clear();" in function(visual_text, signature)
    assert "forget_direct_sight(object);" in function(visual_text, "void CVisualMemoryManager::remove_links(")
    parts.append(function(source("memory_manager.cpp"), "bool CMemoryManager::share_enemy_memory("))
    parts.append(function(source("hit_memory_manager.cpp"), "float CHitMemoryManager::recent_hit_weight("))

    text = function(source("hit_memory_manager.cpp"), "void CHitMemoryManager::add(float amount,")
    start = text.index("\tif (who && !fis_zero(amount))")
    end = text.index("\n\tobject().callback", start)
    parts.append("void CHitMemoryManager::record_test_hit(const CObject* who, float amount) {\n" + text[start:end] + "\n}\n")

    text = function(source("visual_memory_manager.cpp"), "void CVisualMemoryManager::update(float time_delta)")
    guard_start = text.index("\tif (!enabled())")
    start = text.index("\tm_last_update_time = Device.dwTimeGlobal;")
    enabled_guard = text[guard_start:start]
    end = text.index("\n\tVERIFY(m_objects);", start)
    prelude = text[start:end]
    assert "reset_direct_sight_update();" in prelude
    start = text.index("\n\t{", text.index('START_PROFILE("Memory Manager/visuals/update/make_invisible")'))
    end = text.index("\n\tSTOP_PROFILE", start)
    parts.append("void CVisualMemoryManager::begin_test_update() {\n" + enabled_guard + prelude + text[start:end] + "\n}\n")
    start = text.index("\n\t{", text.index('START_PROFILE("Memory Manager/visuals/update/make_not_yet_visible")'))
    end = text.index("\n\tSTOP_PROFILE", start)
    parts.append("void CVisualMemoryManager::decay_test_update(float time_delta) " + text[start:end] + "\n")

    # Ensure both ally entry points actually use the snapshot API; leave script API semantics alone.
    for name, signature in (
        ("ai/stalker/ai_stalker_misc.cpp", "void CAI_Stalker::process_enemies("),
        ("stalker_sound_data_visitor.cpp", "void CStalkerSoundDataVisitor::visit("),
    ):
        body = function(source(name), signature)
        assert "share_enemy_memory(" in body and ".memory(" in body
        assert "make_object_visible_somewhen(" not in body
    return "\n".join(parts)


def main():
    cxx = os.environ.get("CXX", "g++")
    environment = os.environ.copy()
    # Restricted CI containers may not expose /proc task lists required by LeakSanitizer.
    # Address bounds/use-after-free and undefined-behavior checks remain enabled.
    environment.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    with tempfile.TemporaryDirectory(prefix="kich-stalker-memory-") as temporary:
        temporary = Path(temporary)
        (temporary / "memory_production.inc").write_text(integration_source())
        flags = [cxx, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                 "-I" + str(HERE), "-I" + str(GAME), "-I" + str(temporary)]
        for first_time in (False, True):
            output = temporary / ("memory_first_time" if first_time else "memory_current_layout")
            extra = ["-DUSE_FIRST_LEVEL_TIME"] if first_time else []
            subprocess.run(flags + extra + [str(HERE / "memory_integration_tests.cpp"), "-o", str(output)], check=True)
            subprocess.run([str(output)], check=True, env=environment)
    print("PASS: production stalker memory timestamps, target selection, snapshot relay, direct sight and suspicion decay")


if __name__ == "__main__":
    main()
