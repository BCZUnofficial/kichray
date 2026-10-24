#!/usr/bin/env python3
"""Test real B3 state/ownership ordering and EXE session against recording drivers.
Windows UI, DXGI driver and MSVC SEH require the separate Windows run.
"""
from pathlib import Path
import os
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent

def main():
    with tempfile.TemporaryDirectory(prefix="kich-b3-") as folder:
        for cc,standard,source,name in [
            (os.environ.get("CC","gcc"),"-std=c11","abi_c_test.c","abi"),
            (os.environ.get("CXX","g++"),"-std=c++11","controller_tests.cpp","controller"),
            (os.environ.get("CXX","g++"),"-std=c++11","shadow_pool_tests.cpp","shadow_pool"),
            (os.environ.get("CXX","g++"),"-std=c++11","light_transition_tests.cpp","light_transitions"),
            (os.environ.get("CXX","g++"),"-std=c++11","light_admission_tests.cpp","light_admission"),
        ]:
            output=Path(folder)/name
            subprocess.run([cc,standard,"-Wall","-Wextra","-Werror",str(HERE/source),"-o",str(output)],check=True)
            subprocess.run([str(output)],check=True)
    print("PASS: B3 ABI, frame lifecycle and host session; Windows validation remains separate")

if __name__=="__main__":main()
