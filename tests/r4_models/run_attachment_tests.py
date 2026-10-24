#!/usr/bin/env python3
"""Execute native enqueue/pose preparation with controlled attachment callbacks."""
from pathlib import Path
import os
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
source = (root / "xr_3da/KichR4NativeRenderer.cpp").read_text()

def extract(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

methods = extract("static void NativePrepareVisualPose(") + "\n" + extract("void CRender::add_Visual(")
# Exercise the actual deferred pose/wallmark step too. The remaining Submit
# body selects shaders and draws; neither operation may repair a queued world.
submit = extract("void CRender::Submit(")
prepare = submit[submit.index("{") + 1:submit.index("const Fmatrix original=")]
methods += "\nvoid CRender::SubmitPose(IRender_Visual* v,CKinematics* parent){" + prepare + "(void)parent;}\n"
with tempfile.TemporaryDirectory(prefix="kich-e16-attachments-") as folder:
    work = Path(folder)
    (work / "attachment_under_test.h").write_text(methods)
    output = work / "attachments"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(work), str(here / "attachment_tests.cpp"), "-o", str(output)], check=True)
    subprocess.run([str(output)], check=True)

