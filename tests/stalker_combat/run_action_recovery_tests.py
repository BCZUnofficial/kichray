#!/usr/bin/env python3
"""Execute the production KillEnemy recovery state machine with controlled movement."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "xr_3da/xrGame/stalker_combat_actions.cpp"


def extract(source, signature):
    assert source.count(signature) == 1, signature
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
    raise ValueError(signature)


def main():
    compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++ compiler is required for action recovery regression tests")
    source = SOURCE.read_text(encoding="cp1251")
    with tempfile.TemporaryDirectory(prefix="stalker-action-recovery-") as directory:
        temporary = Path(directory)
        (temporary / "action_recovery.generated.h").write_text("\n\n".join(
            extract(source, signature) for signature in (
                "void CStalkerActionKillEnemy::initialize()",
                "void CStalkerActionKillEnemy::execute()")), encoding="utf-8")
        output = temporary / "action_recovery_tests"
        subprocess.run([compiler, "-std=c++11", "-O1", "-Wall", "-Wextra", "-Werror", "-pedantic",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                        "-I", str(temporary), str(ROOT / "tests/stalker_combat/action_recovery_tests.cpp"),
                        "-o", str(output)], check=True)
        environment = os.environ.copy()
        environment["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        subprocess.run([str(output)], check=True, env=environment)


if __name__ == "__main__":
    main()
