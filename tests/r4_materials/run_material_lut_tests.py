#!/usr/bin/env python3
"""P0-L3: R2 s_material generation parity and production LUT sampling vs tex3D."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "resources/shaders/r4/bridge_deferred.hlsl").read_text()


def function(signature):
    start = source.index(signature)
    at = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[at] == "{") - (source[at] == "}")
        at += 1
    return source[start:at]


helpers = "\n".join(function(s) for s in ("float2 MaterialLutSlice(", "float2 MaterialResponse("))
helpers = helpers.replace("[branch]", "")
fixture = (root / "tests/r4_materials/material_lut_sampling_tests.cpp").read_text()
fixture = fixture.replace("// PRODUCTION_MATERIAL_LUT", helpers)
include = ["-I", str(root / "xr_3da")]
with tempfile.TemporaryDirectory(prefix="kich-p0-l3-") as folder:
    for name, text in (("generation", (root / "tests/r4_materials/material_lut_tests.cpp").read_text()), ("sampling", fixture)):
        cpp, binary = Path(folder) / (name + ".cpp"), Path(folder) / name
        cpp.write_text(text)
        subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", *include, str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
