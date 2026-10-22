#!/usr/bin/env python3
"""Compile production G3b immutable-model frontend; no GPU substitution claim."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="kich-g3b-frontend-") as temp:
    binary = Path(temp)/"frontend"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
        str(HERE/"frontend_tests.cpp"), str(ROOT/"xr_3da/KichR4Capture.cpp"), str(ROOT/"xr_3da/RenderSnapshot.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
