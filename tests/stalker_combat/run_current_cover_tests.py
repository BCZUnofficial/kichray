#!/usr/bin/env python3
"""Link the production cover query and its danger-action extern in separate TUs."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
GAME = HERE.parents[1] / "xr_3da/xrGame"


def extract(source):
    signature = "float current_cover(CAI_Stalker* object)"
    assert source.count(signature) == 1, "Missing or duplicate current_cover definition"
    # Include the whole declaration line, so static/inline cannot be stripped away.
    start = source.rfind("\n", 0, source.index(signature)) + 1
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if not depth:
                return source[start:index + 1]
    raise ValueError("Unclosed current_cover definition")


def main():
    compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++ compiler is required for current_cover linkage regression")
    definition = extract((GAME / "stalker_combat_actions.cpp").read_text(encoding="cp1251"))
    consumer = (GAME / "stalker_danger_in_direction_actions.cpp").read_text(encoding="cp1251")
    declarations = re.findall(r"^extern float current_cover\([^;]+;", consumer, re.MULTILINE)
    assert len(declarations) == 1, "Missing or duplicate danger-action declaration"
    with tempfile.TemporaryDirectory(prefix="stalker-current-cover-") as directory:
        temporary = Path(directory)
        implementation = temporary / "current_cover.generated.cpp"
        implementation.write_text('#include "current_cover_fixture.h"\n' + definition, encoding="utf-8")
        (temporary / "current_cover_consumer.generated.h").write_text(declarations[0], encoding="utf-8")
        flags = [compiler, "-std=c++11", "-O1", "-Wall", "-Wextra", "-Werror", "-pedantic",
                 "-I", str(HERE), "-I", str(temporary)]
        objects = []
        for index, source in enumerate((implementation, HERE / "current_cover_tests.cpp")):
            output = temporary / f"unit{index}.o"
            subprocess.run(flags + ["-c", str(source), "-o", str(output)], check=True)
            objects.append(str(output))
        executable = temporary / "current_cover_tests"
        subprocess.run([compiler, *objects, "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
