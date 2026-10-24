#!/usr/bin/env python3
"""Bounded normal-map cause attribution; no GPU/pixel claims."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
with tempfile.TemporaryDirectory(prefix="kich-e19-normal-probe-") as directory:
    binary = Path(directory) / "normal_probe"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "xr_3da"), str(HERE / "normal_map_probe_tests.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    model_binary = Path(directory) / "normal_models"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                    str(HERE / "normal_map_model_tests.cpp"), str(ROOT / "xr_3da/RenderSnapshot.cpp"), "-o", str(model_binary)], check=True)
    subprocess.run([str(model_binary)], check=True)
