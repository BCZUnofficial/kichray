#!/usr/bin/env python3
"""Test semantic material descriptors without D3D or the Windows engine."""
from pathlib import Path
import os
import subprocess
import tempfile
here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="kich-g3c-material-") as temp:
    binary = Path(temp) / "descriptor"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                    str(here / "descriptor_tests.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
