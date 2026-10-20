#!/usr/bin/env python3
"""Exercise production CPU capture and engine adapter without a graphics driver."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

def engine_source():
    source = (ROOT/"xr_3da/KichR4CaptureEngine.cpp").read_text()
    return source.replace('#include "stdafx.h"', '#include "fake_engine.h"').replace('#include "SkeletonCustom.h"', '')

def main():
    cxx = os.environ.get("CXX", "g++")
    with tempfile.TemporaryDirectory(prefix="kich-b43b-") as folder:
        adapter = Path(folder)/"engine.cpp"
        adapter.write_text(engine_source())
        flags = [cxx,"-std=c++11","-O2","-Wall","-Wextra","-Werror","-I"+str(HERE),"-I"+str(ROOT/"xr_3da")]
        for name in ("data", "engine", "session"):
            output = Path(folder)/name
            sources = [str(HERE/(name+"_tests.cpp")),str(ROOT/"xr_3da/KichR4Capture.cpp")]
            if name == "engine": sources.append(str(adapter))
            subprocess.run([*flags,*sources,"-o",str(output)],check=True)
            subprocess.run([str(output)],check=True)
    print("PASS: B4.3b CPU model / hands / HUD capture and native-stream submission contracts")

if __name__ == "__main__":
    main()
