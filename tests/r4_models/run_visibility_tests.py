#!/usr/bin/env python3
"""Exercise production camera/light math and native source collection without a GPU."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
source = (ROOT / "xr_3da/KichR4NativeRenderer.cpp").read_text()
start = source.index("void CRender::Calculate()")
end = source.index("\nvoid CRender::Render()", start)
with tempfile.TemporaryDirectory(prefix="kich-g3b-visibility-") as directory:
    work = Path(directory)
    (work / "native_calculate_under_test.cpp").write_text(source[start:end])
    light_start = source.index("class R4Light:public IRender_Light")
    light_end = source.index("\nclass R4Glow;", light_start)
    (work / "native_light_under_test.cpp").write_text(source[light_start:light_end])
    sector_start = source.index("uint32_t KichR4_NativeSector(")
    sector_end = source.index("\nIRender_Sector* CRender::detectSector(", sector_start)
    (work / "native_sector_under_test.cpp").write_text(source[sector_start:sector_end])
    for name in ("visibility", "native_candidates", "native_lights", "native_sector"):
        output = work / name
        subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(work), str(HERE / (name + "_tests.cpp")),
                        str(ROOT / "xr_3da/RenderSnapshot.cpp"), "-o", str(output)], check=True)
        subprocess.run([str(output)], check=True)
