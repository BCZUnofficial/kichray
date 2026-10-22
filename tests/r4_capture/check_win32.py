#!/usr/bin/env python3
"""Compile/link real x86 Windows SDK contracts; does not run a D3D driver.

Requires MinGW-w64 C/C++ tools (MINGW_CXX/MINGW_CC may select paths).
Engine services are declarations at the SDK seam, not a replacement MSVC build.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import sys
import re
sys.dont_write_bytecode = True
from run_tests import engine_source

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

def main():
    cxx = os.environ.get("MINGW_CXX", "i686-w64-mingw32-g++")
    cc = os.environ.get("MINGW_CC", "i686-w64-mingw32-gcc")
    objdump = os.environ.get("MINGW_OBJDUMP", cxx.rsplit("g++",1)[0]+"objdump")
    with tempfile.TemporaryDirectory(prefix="kich-b43b-sdk-") as directory:
        work = Path(directory)
        flags = [cxx,"-std=c++11","-O1","-Wall","-Wextra","-Werror","-Wno-cast-function-type", "-I"+str(ROOT/"tests/r4_native")]
        native = []
        for source in ("xrRender_R4", "R4Resources", "R4FrameDX11", "R4PipelineDX11", "R4RendererAdapter"):
            obj=work/(source+".o");native.append(str(obj))
            subprocess.run([*flags,"-c",str(ROOT/"xr_3da/xrRender_R4"/(source+".cpp")),"-o",str(obj)],check=True)
        subprocess.run([cxx,"-shared","-static-libgcc","-static-libstdc++",*native,str(ROOT/"xr_3da/xrRender_R4/xrRender_R4.def"),"-luuid","-luser32","-o",str(work/"xrRender_R4.dll")],check=True)
        exports = subprocess.check_output([objdump,"-p",str(work/"xrRender_R4.dll")],text=True)
        for name in ("BridgeVersion","ProbeHardware","ProbeInfo","ResourceAPI","FrameAPI","GeometryAPI","LevelAPI","StreamAPI","DeferredAPI","NativeToolsAPI","RendererAPI","DetailAPI","ModelAPI","TelemetryAPI"):
            symbol = "KichR4_"+("Get"+name if name!="ProbeHardware" else name)
            if not re.search(r"\b"+symbol+r"\b",exports): raise RuntimeError("missing DLL export: "+symbol)
        imports = re.findall(r"DLL Name:\s*(\S+)",exports)
        if any("d3d9" in name.lower() or "d3dx9" in name.lower() for name in imports): raise RuntimeError("R4 unexpectedly imports legacy graphics: "+str(imports))
        subprocess.run([*flags,"-DENGINE_BUILD","-c",str(ROOT/"xr_3da/KichR4NativePresenter.cpp"),"-o",str(work/"native_presenter.o")],check=True)
        engine=work/"engine.cpp";engine.write_text(engine_source());adapter=work/"engine.o"
        seam=["-DENGINE_BUILD","-DKICH_R4_REAL_SDK","-I"+str(HERE),"-I"+str(ROOT/"xr_3da")]
        subprocess.run([*flags,"-fno-exceptions",*seam,"-c",str(engine),"-o",str(adapter)],check=True)
        host=[str(adapter)]
        for name in ("KichR4Probe", "KichR4ResourceProbe", "KichR4FrameProbe", "KichR4Game", "KichR4Capture", "RenderSnapshot", "RenderSnapshotD3D9"):
            obj=work/(name+".o");host.append(str(obj))
            subprocess.run([*flags,"-DENGINE_BUILD","-c",str(ROOT/"xr_3da"/(name+".cpp")),"-o",str(obj)],check=True)
        main=work/"main.cpp";main.write_text('#include "fake_engine.h"\nCaptureDevice Device;CaptureCache RCache;CaptureHardware HW;bool KichR4_NativeActive(){return false;}namespace kich_r4_capture{struct Style;struct ModelInput;}bool KichR4_NativeStyle(kich_r4_capture::Style&){return false;}void KichR4_NativeModelMaterial(kich_r4_capture::ModelInput&){}int main(){return 0;}\n')
        lib=work/"XR_3DA.dll.a"
        subprocess.run([*flags,*seam,str(main),*host,"-static-libgcc","-static-libstdc++","-luser32","-lgdi32","-Wl,--out-implib,"+str(lib),"-o",str(work/"XR_3DA.exe")],check=True)
        client=work/"client.cpp";client.write_text('#include "KichR4Capture.h"\n#include "KichR4Game.h"\nextern "C" __declspec(dllexport) void CheckImports(){KichR4_CaptureActive();KichR4_CaptureHUD(true);KichR4_CaptureModel(0,0,0,0,0);KichR4_CaptureModel(0,0,0,0,0,KICH_R4_SKIN_HARDWARE);KichR4_CaptureUIDraw(0,0,0,0,0,0,0,0,0,0,false);KichR4_GameSetModal(false);KichR4LocalLight l={};KichR4_CaptureLight(0,l,false);}\n')
        subprocess.run([*flags,"-shared","-I"+str(ROOT/"xr_3da"),str(client),str(lib),"-o",str(work/"client.dll")],check=True)
        abi=work/"abi.c";abi.write_text('#include "KichR4Renderer.h"\n#include "KichR4Telemetry.h"\nint main(void){return sizeof(KichR4TelemetryAPI)!=24 || sizeof(KichR4TelemetryContext)!=56 || sizeof(KichR4TelemetryRecord)!=584 || sizeof(KichR4TelemetryTiming)!=96 || sizeof(KichR4RendererAPI)!=628 || sizeof(KichR4BuildInfo)!=288 || sizeof(KichR4StreamDrawDesc)!=112 || sizeof(KichR4StreamAPI)!=24 || sizeof(KichR4PassDesc)!=84 || sizeof(KichR4DeferredDrawDesc)!=180 || sizeof(KichR4DeferredAPI)!=28 || sizeof(KichR4DetailAPI)!=32 || sizeof(KichR4ModelAPI)!=32 || sizeof(KichR4ModelPose)!=16400 || sizeof(KichR4ModelVertex)!=32 || sizeof(KichR4ModelDrawDesc)!=192;}\n')
        subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-I"+str(ROOT/"xr_3da"),"-c",str(abi),"-o",str(work/"abi.o")],check=True)
        for debug in ([],["-DDEBUG"]):
            subprocess.run([*flags,"-Wno-unknown-pragmas",*debug,"-DENGINE_BUILD","-I"+str(ROOT/"tests/r4_device/win32_sdk"),"-c",str(ROOT/"tests/r4_device/win32_compile.cpp"),"-o",str(work/"dx9.o")],check=True)
    print("PASS: real x86 Windows SDK: R4 DLL / no-EH engine adapter / EXE exports / renderer-game import client / C ABI / DX9 Release+Debug seams")

if __name__ == "__main__":
    main()
