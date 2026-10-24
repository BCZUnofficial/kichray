#!/usr/bin/env python3
"""Execute production native dispatch, cancellation, and frame-attribution gates."""
from pathlib import Path
import os
import subprocess
import tempfile
here = Path(__file__).resolve().parent
root = here.parents[1]
source = (root / "xr_3da/KichR4NativePresenter.cpp").read_text()

def block(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

requested = "const bool scene_requested=input.allowed || input.warmup;"
assert requested in source
load = source[source.index("if(!c.loaded && !c.name.empty() && scene_requested)"):]
load = load[:load.index("c.loaded=true;") + len("c.loaded=true;")] + "}"
dispatch = source[source.index("if(hr==0 && c.loaded && scene_requested)"):]
dispatch = dispatch[:dispatch.index("const bool screenshot=")]
cancel = block("if(preflight==KICH_R4_FRAME_SUSPENDED)")
eligible = source[source.index("const bool eligible="):].split(";", 1)[0] + ";"
world_current = source[source.index("const bool world_current="):].split(";", 1)[0] + ";"
methods = "bool StartScene(Context& c,const KichR4GameFrame& input){" + requested + load + "return true;}\n"
methods += "int Dispatch(Context& c,const KichR4GameFrame& input,const Batch* batch,int32_t hr,bool tracing){" + requested + dispatch + "return hr;}\n"
methods += "bool PreflightResult(Context& c,const KichR4GameFrame& input,int32_t preflight){" + cancel + "return preflight==KICH_R4_OK;}\n"
methods += "bool Eligible(Context& c,const KichR4GameFrame& input,int32_t hr,bool tracing,bool screenshot){" + eligible + "return eligible;}\n"
methods += "bool WorldCurrent(Context& c,const KichR4GameFrame& input,int32_t hr){" + requested + world_current + "return world_current;}\n"
with tempfile.TemporaryDirectory(prefix="kich-e17-frame-gates-") as folder:
    work = Path(folder)
    (work / "warmup_under_test.h").write_text(methods)
    output = work / "warmup"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(work), str(here / "warmup_tests.cpp"), "-o", str(output)], check=True)
    subprocess.run([str(output)], check=True)

