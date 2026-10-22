#!/usr/bin/env python3
"""Compile and execute the actual dependency-free stalker cover policy."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
if not compiler:
    raise SystemExit("A C++ compiler is required for cover policy regression tests")
with tempfile.TemporaryDirectory(prefix="stalker-cover-test-") as directory:
    output = Path(directory) / "cover_policy_test"
    subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-pedantic",
                    str(ROOT / "tests/stalker_combat/cover_policy_test.cpp"), "-o", str(output)], check=True)
    subprocess.run([str(output)], check=True)
