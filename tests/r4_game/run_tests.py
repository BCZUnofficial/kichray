#!/usr/bin/env python3
"""Production game session and real EXE runtime, without a Windows GPU."""
from pathlib import Path
import os
import subprocess
import tempfile
import importlib.util
import sys
sys.dont_write_bytecode = True

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

def main():
    with tempfile.TemporaryDirectory(prefix="kich-b43-game-") as folder:
        for source in ("session_tests", "runtime_tests"):
            output = Path(folder)/source
            extra = [str(ROOT/"xr_3da/KichR4Capture.cpp")] if source == "runtime_tests" else []
            subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2",
                            "-Wall", "-Wextra", "-Werror", "-Wno-cast-function-type",
                            "-I"+str(HERE/"fake_win32"), str(HERE/(source+".cpp")), *extra,str(ROOT/"xr_3da/RenderSnapshot.cpp"),
                            "-o", str(output)], check=True)
            subprocess.run([str(output)], check=True)
        production = (ROOT/"xr_3da/KichR4GameEngine.cpp").read_text()
        # B5 appended a separate native presenter function. This legacy camera
        # seam deliberately includes only the original prepare function.
        function = production[production.index("void KichR4_GamePrepareEngineFrame()") : production.index("\nvoid KichR4_NativePostprocess")]
        engine_source = Path(folder)/"engine_frame.cpp"
        engine_source.write_text((HERE/"engine_frame_tests.cpp").read_text().replace("// PRODUCTION_FUNCTION", function))
        cxx = os.environ.get("CXX", "g++")
        for guards in ([], ["-DDEDICATED_SERVER"], ["-D_EDITOR"]):
            output = Path(folder)/"engine_frame"
            subprocess.run([cxx,"-std=c++11","-Wall","-Wextra","-Werror",*guards,"-I"+str(HERE),str(engine_source),"-o",str(output)],check=True)
            subprocess.run([str(output)],check=True)
        spec = importlib.util.spec_from_file_location("device_tests",ROOT/"tests/r4_device/run_tests.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        device_source=Path(folder)/"device.cpp"
        device_source.write_text(module.unit(False))
        for guards in ([],["-DDEBUG"]):
            common=[cxx,"-std=c++11","-Wall","-Wextra","-Werror","-Wno-unknown-pragmas","-Wno-strict-aliasing",
                    "-DKICH_R4_TEST_GAME_HOOKS",*guards,"-I"+str(ROOT/"tests/r4_device"),"-I"+str(ROOT/"tests/r4_device/fakes")]
            scenarios=Path(folder)/"scenarios.o"
            subprocess.run([*common,"-Dmain=UnusedDeviceScenarioEntry","-c",str(ROOT/"tests/r4_device/scenarios.cpp"),"-o",str(scenarios)],check=True)
            output=Path(folder)/"presenter"
            subprocess.run([*common,str(device_source),str(HERE/"presenter_tests.cpp"),str(scenarios),"-o",str(output)],check=True)
            subprocess.run([str(output)],check=True)
    print("PASS: B4.3a game session / loader / lifecycle; native Windows presentation requires the game test")

if __name__ == "__main__":
    main()
