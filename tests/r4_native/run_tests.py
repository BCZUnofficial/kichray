#!/usr/bin/env python3
"""Compile the actual G1 DLL adapter and EXE handshake without Windows/GPU."""
from pathlib import Path
import os
import subprocess
import tempfile
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

def main():
    with tempfile.TemporaryDirectory(prefix="kich-g01-") as directory:
        work = Path(directory)
        presenter = (ROOT/"xr_3da/KichR4NativePresenter.cpp").read_text()
        loader = presenter[presenter.index("void LogBuild("):presenter.index("\nvoid StopContext()")]
        (work/"loader_under_test.h").write_text(loader)
        flags = [os.environ.get("CXX", "g++"), "-std=c++11", "-Wall", "-Wextra", "-Werror", "-I"+str(HERE), "-I"+str(work)]
        for name, extra in [
            ("contract", [ROOT/"xr_3da/xrRender_R4/R4RendererAdapter.cpp"]),
            ("budget", [ROOT/"xr_3da/KichR4Capture.cpp",ROOT/"xr_3da/RenderSnapshot.cpp"]),
        ]:
            output = work/name
            subprocess.run([*flags, str(HERE/(name+"_tests.cpp")), *map(str, extra), "-o", str(output)], check=True)
            subprocess.run([str(output)], check=True)
        output = work/"abi"
        subprocess.run([os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror", str(HERE/"abi_c_test.c"), "-o", str(output)], check=True)
        subprocess.run([str(output)], check=True)
        # Check exact Win32 packing without depending on a 32-bit C runtime.
        x86 = work/"x86.c"
        x86.write_text('#include "KichR4Renderer.h"\n')
        subprocess.run([os.environ.get("CC", "gcc"), "-m32", "-ffreestanding", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-I"+str(ROOT/"xr_3da"), "-c", str(x86), "-o", str(work/"x86.o")], check=True)
    ns={"m":"http://schemas.microsoft.com/developer/msbuild/2003"}
    for path, source in [(ROOT/"xr_3da/XR_3DA.vcxproj", "KichR4NativePresenter.cpp"),
                         (ROOT/"xr_3da/xrRender_R4/xrRender_R4.vcxproj", "R4RendererAdapter.cpp")]:
        project=ET.parse(path)
        imports=[node.attrib["Project"] for node in project.findall("m:Import",ns)]
        assert any(p.endswith("build\\KichR4BuildIdentity.targets") for p in imports), path
        item=next(n for n in project.findall(".//m:ClCompile",ns) if n.get("Include")==source)
        assert "$(IntDir)" in item.find("m:AdditionalIncludeDirectories",ns).text, path
    identity = ET.parse(ROOT/"build/KichR4BuildIdentity.targets")
    task = identity.find("m:UsingTask", ns)
    assert task is not None and task.get("TaskFactory") == "RoslynCodeTaskFactory"
    # The standalone task only uses standard .NET APIs. Adding a legacy
    # assembly name made Roslyn fail with MSB3755 before loading the task.
    assert not task.findall("m:Task/m:Reference", ns), "Use Roslyn's default .NET reference set"
    source = task.find("m:Task/m:Code", ns)
    assert source is not None and source.get("Type") == "Class"
    assert source.get("Source") == "$(MSBuildThisFileDirectory)KichR4BuildIdentity.cs"
    print("PASS: G0/G1 C ABI (including x86 packing), real provider/EXE negotiation, capture diagnostics and project wiring")
    print("MSBuild identity task, full MSVC link and visual validation require the Windows build.")

if __name__ == "__main__":
    main()
