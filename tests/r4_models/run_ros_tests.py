#!/usr/bin/env python3
"""Compile the production R4ROS class with deterministic collision observations."""
from pathlib import Path
import os
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
source = (root / "xr_3da/KichR4NativeRenderer.cpp").read_text()
start = source.index("class R4ROS:public IRender_ObjectSpecific {")
end = source.index("\nxr_vector<ref_light> static_lights;", start)
with tempfile.TemporaryDirectory(prefix="kich-e15-ros-") as folder:
    work = Path(folder)
    (work / "ros_under_test.h").write_text(source[start:end])
    output = work / "ros"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-Wno-misleading-indentation", "-I" + str(work), str(here / "ros_tests.cpp"), "-o", str(output)], check=True)
    subprocess.run([str(output)], check=True)
