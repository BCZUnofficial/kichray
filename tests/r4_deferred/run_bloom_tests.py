#!/usr/bin/env python3
"""P0-C2: production bloom HLSL texcoords/weights against R2 phase_bloom."""
from pathlib import Path
import os
import re
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


helpers = "\n".join(function(s) for s in ("float KichBloomAlpha(", "float4 PSBloomBuild(",
                                           "float4 PSBloomFilter(", "float4 PSBloomFilterFast(",
                                           "float4 PSLuminance1(", "float4 LuminanceBlock(",
                                           "float4 PSLuminance2(", "float4 PSLuminance3("))
# Syntax-only adapters: swizzles become C++ helpers; arithmetic stays production code.
helpers = helpers.replace(":SV_Target", "").replace("[branch]", "").replace("[unroll]", "")
helpers = helpers.replace("result.rgb*=light_position.z;",
                          "result.x*=light_position.z;result.y*=light_position.z;result.z*=light_position.z;")
helpers = helpers.replace(").rgb", ").xyz3()")
helpers = re.sub(r"\b(\w+(?:\.\w+)*)\.rgb\b", r"xyz(\1)", helpers)
helpers = re.sub(r"\b(\w+(?:\.\w+)*)\.xy\b", r"xy(\1)", helpers)
helpers = re.sub(r"\.a\b", ".w", helpers)
# Scalar .xxxx splat: the C++ float4(float) constructor keeps .x, which the test reads.
helpers = re.sub(r"\.r\b", ".x", helpers).replace(").xxxx", ")")
fixture = (root / "tests/r4_deferred/bloom_tests.cpp").read_text().replace("// PRODUCTION_BLOOM", helpers)
with tempfile.TemporaryDirectory(prefix="kich-p0-c2-") as folder:
    cpp, binary = Path(folder) / "bloom.cpp", Path(folder) / "bloom"
    cpp.write_text(fixture)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-I", str(root / "xr_3da"),
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
