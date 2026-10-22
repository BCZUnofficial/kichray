#!/usr/bin/env python3
"""Compile production detail frontend/session and snapshot recorder; no GPU claim."""
from pathlib import Path
import os
import subprocess
import tempfile
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="kich-g3a-") as directory:
    work=Path(directory)
    frontend=(ROOT/"xr_3da/R4DetailRenderer.cpp").read_text().replace('#include "KichR4NativePrelude.h"','#include "detail_engine.h"')
    (work/"frontend_under_test.cpp").write_text(frontend)
    adapter=(ROOT/"xr_3da/RenderSnapshotEngine.cpp").read_text()
    (work/"snapshot_engine_under_test.cpp").write_text("\n".join(line for line in adapter.splitlines() if not line.startswith("#include")))
    for name in ("frontend","session","snapshot","snapshot_engine"):
        binary=work/name
        subprocess.run([os.environ.get("CXX","g++"),"-std=c++11","-O2","-Wall","-Wextra","-Werror",
            "-I"+str(HERE),"-I"+str(work),"-I"+str(ROOT/"xr_3da"),"-I"+str(ROOT/"tests/r4_native"),str(HERE/(name+"_tests.cpp")),str(ROOT/"xr_3da/RenderSnapshot.cpp"),"-o",str(binary)],check=True)
        subprocess.run([str(binary),str(work)],check=True)
    editor=work/"editor.cpp"
    editor.write_text('#include "RenderSnapshot.h"\nint main(){RenderSnapshotInfo i={};RenderSnapshot_Begin(i);return RenderSnapshot_Request() || g_render_snapshot_active || g_render_snapshot_console;}\n')
    subprocess.run([os.environ.get("CXX","g++"),"-std=c++11","-D_EDITOR","-Wall","-Wextra","-Werror","-I"+str(ROOT/"xr_3da"),str(editor),str(ROOT/"xr_3da/RenderSnapshot.cpp"),"-o",str(work/"editor")],check=True)
    subprocess.run([str(work/"editor")],check=True)
ns={"m":"http://schemas.microsoft.com/developer/msbuild/2003"}
project=ET.parse(ROOT/"xr_3da/XR_3DA.vcxproj")
items={n.get("Include"):n for n in project.findall(".//m:ClCompile",ns) if n.get("Include")}
for source in ("R4DetailRenderer.cpp","RenderSnapshot.cpp","RenderSnapshotEngine.cpp"):
    assert source in items and items[source].find("m:PrecompiledHeader",ns).text=="NotUsing"
assert "KichR4NativePrelude.h" in items["R4DetailRenderer.cpp"].find("m:ForcedIncludeFiles",ns).text
assert "$(IntDir)" in items["RenderSnapshotEngine.cpp"].find("m:AdditionalIncludeDirectories",ns).text
print("PASS: G3a frontend, native submission, snapshot and project wiring (Windows/GPU tests remain separate)")
