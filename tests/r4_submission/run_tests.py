#!/usr/bin/env python3
"""Exercise production state cache and buffer API bodies with recording contexts.
This verifies submission decisions and failure behavior, not a Windows driver.
"""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
source = (ROOT / "xr_3da/xrRender_R4/R4Resources.cpp").read_text()
create = source[source.index("int32_t KICH_R4_CALL CreateBuffer("):source.index("bool CopyName(")]
update = source[source.index("int32_t KICH_R4_CALL UpdateBuffer("):source.index("void ResourceIdentity(")]
with tempfile.TemporaryDirectory(prefix="kich-e13-submission-") as directory:
    work = Path(directory)
    flags = [os.environ.get("CXX", "g++"), "-std=c++11", "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT)]
    state = work / "state"
    subprocess.run([*flags, "-I" + str(HERE / "fake"), str(HERE / "state_tests.cpp"), "-o", str(state)], check=True)
    subprocess.run([str(state)], check=True)
    buffer = work / "buffer.cpp"
    buffer.write_text((HERE / "buffer_fixture.h").read_text() + create + update + (HERE / "buffer_cases.cpp").read_text())
    exe = work / "buffer"
    subprocess.run([*flags, str(buffer), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

    model = work / "model.cpp"
    draw = source[source.index("int32_t DrawDeferredOrDetail("):source.index("int32_t KICH_R4_CALL DrawDeferred(")]
    model.write_text((HERE / "model_bind_fixture.h").read_text() + draw + (HERE / "model_bind_cases.cpp").read_text())
    exe = work / "model"
    subprocess.run([*flags, "-I" + str(HERE / "fake"), str(model), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
