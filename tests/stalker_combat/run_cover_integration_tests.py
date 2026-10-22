#!/usr/bin/env python3
"""Compile production cover lifecycle methods against controlled world queries."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "xr_3da/xrGame/ai/stalker/ai_stalker_cover.cpp"


def extract(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if not depth:
                return source[start:index + 1]
    raise ValueError(f"Unclosed production function: {signature}")


def main():
    compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++ compiler is required for cover lifecycle regression tests")
    source = SOURCE.read_text(encoding="cp1251")
    signatures = [
        "void CAI_Stalker::reset_best_cover_state()",
        "void CAI_Stalker::invalidate_best_cover()",
        "bool CAI_Stalker::combat_cover_blocked(",
        "void CAI_Stalker::abandon_best_cover(",
        "bool CAI_Stalker::update_cover_progress()",
        "void CAI_Stalker::accept_best_cover(",
        "void CAI_Stalker::best_cover_can_try_advance()",
        "void CAI_Stalker::update_best_cover_actuality(",
        "const CCoverPoint* CAI_Stalker::best_cover(",
    ]
    with tempfile.TemporaryDirectory(prefix="stalker-cover-integration-") as directory:
        temporary = Path(directory)
        (temporary / "cover_methods.generated.h").write_text(
            "\n\n".join(extract(source, signature) for signature in signatures), encoding="utf-8")
        output = temporary / "cover_integration_tests"
        subprocess.run([compiler, "-std=c++11", "-O1", "-Wall", "-Wextra", "-Werror", "-pedantic",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie", "-I", str(temporary),
                        str(ROOT / "tests/stalker_combat/cover_integration_tests.cpp"), "-o", str(output)], check=True)
        environment = os.environ.copy()
        environment["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        subprocess.run([str(output)], check=True, env=environment)


if __name__ == "__main__":
    main()
