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
    # Keep production includes and compile the adapter separately. Stripping
    # includes hid its missing Render.h dependency in the Windows build.
    (work/"snapshot_engine_under_test.cpp").write_text(adapter)
    for header in ("RenderSnapshotEngine.h","RenderSnapshotD3D9.h"):
        (work/header).write_bytes((ROOT/"xr_3da"/header).read_bytes())
    (work/"stdafx.h").write_text('#include "snapshot_engine_fake.h"\n')
    (work/"Render.h").write_text('#include "snapshot_render_fake.h"\n')
    for header in ("KichR4Native.h","IGame_Level.h","IGame_Persistent.h",
                   "Environment.h","XR_IOConsole.h","CustomHUD.h","ResourceManager.h"):
        (work/header).write_text('// Engine types are provided by snapshot_engine_fake.h.\n')
    for name in ("frontend","session","shadow_selection","snapshot","snapshot_engine","provenance"):
        binary=work/name
        extra=[str(work/"snapshot_engine_under_test.cpp")] if name=="snapshot_engine" else []
        subprocess.run([os.environ.get("CXX","g++"),"-std=c++11","-O2","-Wall","-Wextra","-Werror",
            "-I"+str(HERE),"-I"+str(work),"-I"+str(ROOT/"xr_3da"),"-I"+str(ROOT/"tests/r4_native"),str(HERE/(name+"_tests.cpp")),*extra,str(ROOT/"xr_3da/RenderSnapshot.cpp"),"-o",str(binary)],check=True)
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
