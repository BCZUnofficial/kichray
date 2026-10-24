#!/usr/bin/env python3
"""Execute production sun-light HLSL with controlled GBuffer/shadow inputs.

The shadow-map sampler is an injected visibility value; the existing PCF suite
checks filtering. This regression checks lighting composition, not a GPU image.
"""
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


signatures = (
    "float PackMaterial(", "float MaterialGloss(", "float MaterialClass(",
    "float2 MaterialSlice(", "float2 MaterialResponse(", "float SpecularIntensity(",
    "float FogTransmittance(", "float3 ForwardFog(",
    "float4 DirectLight(", "float4 SunLighting(", "float4 PSSun(",
    "float4 PSForwardSun(",
)
helpers = "\n".join(function(signature) for signature in signatures)
# Syntax-only adapters: execute the production branches and numerical expressions.
helpers = helpers.replace(":SV_Target", "").replace(":SV_IsFrontFace", "")
helpers = helpers.replace("SunVisibility(p,dx,dy).xxx", "splat3(SunVisibility(p,dx,dy))")
helpers = re.sub(r"\b(\w+(?:\.\w+)*)\.(?:rgb|xyz)\b", r"xyz(\1)", helpers)
helpers = re.sub(r"\b(\w+(?:\.\w+)*)\.xy\b", r"xy(\1)", helpers)
helpers = re.sub(r"\b(\w+(?:\.\w+)*)\.aaa\b", r"splat3(\1.w)", helpers)
# HLSL's compact statements otherwise trigger C++ misleading-indentation.
helpers = re.sub(r"//[^\n]*", "", helpers).replace(";", ";\n")
fixture = (root / "tests/r4_deferred/sun_lighting_tests.cpp").read_text()
fixture = fixture.replace("// PRODUCTION_SUN_HELPERS", helpers)

with tempfile.TemporaryDirectory(prefix="kich-e16-sun-") as directory:
    cpp, binary = Path(directory) / "sun.cpp", Path(directory) / "sun"
    cpp.write_text(fixture)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-Wall",
                    "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
