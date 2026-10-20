#!/usr/bin/env python3
"""Native ABI, production ownership/validation and real host-probe tests.
No GPU or Windows/MSVC execution is emulated by these checks.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

def main():
    cxx = os.environ.get("CXX", "g++")
    cc = os.environ.get("CC", "gcc")
    with tempfile.TemporaryDirectory(prefix="kich-b2-") as work:
        jobs = [
            (cc, ["-std=c11"], [HERE/"abi_c_test.c"], "abi"),
            (cxx, ["-std=c++11"], [HERE/"rules_tests.cpp"], "rules"),
            (cxx, ["-std=c++11", "-I"+str(ROOT/"tests/r4_bridge/fake_win32")],
             [HERE/"probe_tests.cpp", ROOT/"xr_3da/KichR4ResourceProbe.cpp"], "probe"),
            (cxx, ["-std=c++11", "-I"+str(ROOT/"tests/r4_bridge/fake_win32")],
             [ROOT/"tests/r4_bridge/loader_tests.cpp", ROOT/"xr_3da/KichR4Probe.cpp",
              ROOT/"xr_3da/KichR4ResourceProbe.cpp"], "loader"),
        ]
        for compiler, flags, sources, name in jobs:
            output = Path(work)/name
            subprocess.run([compiler, *flags, "-Wall", "-Wextra", "-Werror",
                            *map(str, sources), "-o", str(output)], check=True)
            subprocess.run([str(output)], check=True)
    print("PASS: B2 portable tests; Windows driver/MSVC checks remain separate")

if __name__ == "__main__":
    main()
