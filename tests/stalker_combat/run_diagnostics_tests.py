#!/usr/bin/env python3
"""Compile and exercise the production stalker-only trace with a small log adapter."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="stalker-diagnostics-") as folder:
        binary = Path(folder) / "diagnostics-tests"
        subprocess.run([
            os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread",
            "-I" + str(HERE / "diagnostics_fake"), "-I" + str(ROOT / "xr_3da/xrGame"),
            str(HERE / "diagnostics_tests.cpp"),
            str(ROOT / "xr_3da/xrGame/stalker_combat_diagnostics.cpp"), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
