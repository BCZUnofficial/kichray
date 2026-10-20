#!/usr/bin/env python3
"""Real SoC/DDS readers, camera, filesystem adapter and EXE viewer session.
Fixtures follow the repository's XRLC writer; no game assets/GPU are emulated.
"""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent

def main():
    with tempfile.TemporaryDirectory(prefix="kich-b42-") as folder:
        jobs = [(os.environ.get("CC", "gcc"), "-std=c11", "abi_c_test.c", "abi")]
        jobs += [(os.environ.get("CXX", "g++"), "-std=c++11", source+".cpp", source)
                 for source in ("data_tests", "files_tests", "session_tests")]
        for cc, standard, source, name in jobs:
            output = Path(folder)/name
            subprocess.run([cc, standard, "-Wall", "-Wextra", "-Werror",
                            str(HERE/source), "-o", str(output)], check=True)
            subprocess.run([str(output)], check=True)
    print("PASS: B4.2 portable tests; real level assets and Windows GPU remain separate")

if __name__ == "__main__":
    main()
