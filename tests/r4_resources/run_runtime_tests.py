#!/usr/bin/env python3
"""Execute the actual upload body against a recording API; not a D3D driver test."""
from pathlib import Path
import os
import subprocess
import tempfile
here = Path(__file__).resolve().parent
root = here.parents[1]
source = (root / "xr_3da/xrRender_R4/R4Resources.cpp").read_text()
body = source[source.index("int32_t KICH_R4_CALL UploadBGRA8("):source.index("int32_t KICH_R4_CALL ReadNativePixels(")]
body += source[source.index("int32_t ShaderConstants("):source.index("int32_t CreatePipelineImpl(")]
with tempfile.TemporaryDirectory(prefix="kich-e1-runtime-") as directory:
    work = Path(directory)
    (work / "runtime_under_test.cpp").write_text('#include "runtime_fake.h"\n' + body)
    binary = work / "runtime"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-Wall", "-Wextra", "-Werror", "-I"+str(here), "-I"+str(root / "tests/r4_submission/fake"), "-I"+str(work), str(here / "runtime_tests.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
