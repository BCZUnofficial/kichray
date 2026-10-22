#!/usr/bin/env python3
"""Run the production native shader selection functions with fake engine resources."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    at = brace + 1
    while depth:
        if source[at] == "{":
            depth += 1
        elif source[at] == "}":
            depth -= 1
        at += 1
    return source[start:at]

renderer = (ROOT / "xr_3da/KichR4NativeRenderer.cpp").read_text()
native = (ROOT / "xr_3da/KichR4NativeDraw.cpp").read_text()
adapter = '#include "fake_engine.h"\n' + function(renderer, "void CRender::Submit(") + "\n"
adapter += function(native, "void KichR4_NativeElementSource(") + "\n"
adapter += function(native, "void KichR4_NativeElementPass(") + "\n"
with tempfile.TemporaryDirectory(prefix="kich-g3b-materials-") as temp:
    source = Path(temp) / "production_adapter.cpp"
    source.write_text(adapter)
    binary = Path(temp) / "materials"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(HERE), "-I" + str(ROOT / "xr_3da"), str(source),
                    str(HERE / "routing_tests.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    integration = Path(temp) / "distortion_session"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                    str(HERE / "distortion_session_tests.cpp"), str(ROOT / "xr_3da/RenderSnapshot.cpp"),
                    "-o", str(integration)], check=True)
    subprocess.run([str(integration)], check=True)
