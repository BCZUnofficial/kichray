#!/usr/bin/env python3
"""Execute the actual query helper with a narrow read-only SDK seam."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="kich-g3b1-d3d9-") as directory:
    work=Path(directory)
    source=(ROOT/"xr_3da/RenderSnapshotD3D9.cpp").read_text()
    source=source.replace('#if defined(_WIN32) && !defined(_EDITOR) && !defined(DEDICATED_SERVER)', '#if 1',1)
    (work/"helper.cpp").write_text(source)
    binary=work/"tests"
    subprocess.run([os.environ.get("CXX","g++"),"-std=c++11","-O2","-Wall","-Wextra","-Werror","-I"+str(HERE/"d3d9_fake"),"-I"+str(ROOT/"xr_3da"),str(work/"helper.cpp"),str(HERE/"d3d9_resource_tests.cpp"),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
