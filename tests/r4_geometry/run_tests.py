#!/usr/bin/env python3
"""Exercise the production geometry rules, scene math and EXE session.
These checks do not emulate a Windows driver or execute the HLSL compiler.
"""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent

def main():
    with tempfile.TemporaryDirectory(prefix="kich-b41-") as folder:
        for cc, standard, source, name in [
            (os.environ.get("CC", "gcc"), "-std=c11", "abi_c_test.c", "abi"),
            (os.environ.get("CXX", "g++"), "-std=c++11", "geometry_tests.cpp", "geometry"),
        ]:
            output = Path(folder)/name
            subprocess.run([cc, standard, "-Wall", "-Wextra", "-Werror",
                            str(HERE/source), "-o", str(output)], check=True)
            subprocess.run([str(output)], check=True)
    print("PASS: B4.1 portable geometry; Windows GPU/MSVC validation remains separate")

if __name__ == "__main__":
    main()
