#!/usr/bin/env python3
"""Portable SoC sector/portal parsing, conservative visibility and ray checks."""
from pathlib import Path
import os
import subprocess
import tempfile

here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="kich-e12-portals-") as folder:
    output = Path(folder) / "portals"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-Wall", "-Wextra",
                    "-Werror", str(here / "portal_tests.cpp"), "-o", str(output)], check=True)
    subprocess.run([str(output)], check=True)
