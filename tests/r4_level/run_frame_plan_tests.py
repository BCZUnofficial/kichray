#!/usr/bin/env python3
"""Production BVH/frame visibility equivalence, invalidation and workload checks."""
from pathlib import Path
import os
import subprocess
import tempfile

here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="kich-e13-plan-") as folder:
    output = Path(folder) / "frame_plan"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-Wall", "-Wextra",
                    "-Werror", str(here / "frame_plan_tests.cpp"), "-o", str(output)], check=True)
    subprocess.run([str(output)], check=True)
