#!/usr/bin/env python3
"""Check production present metrics, including the Win32 x87 PC_24 failure mode."""
from pathlib import Path
import os
import platform
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent


def main():
    variants = [("native", [])]
    if platform.system() == "Linux" and platform.machine().lower() in ("x86_64", "amd64", "i386", "i686"):
        variants.append(("x87-pc24", ["-mfpmath=387", "-DKICH_TEST_X87"]))
    with tempfile.TemporaryDirectory(prefix="kich-e15-metrics-") as folder:
        for name, flags in variants:
            for source in ("metrics_tests.cpp", "phase_tests.cpp", "probe_tests.cpp"):
                output = Path(folder) / (name + "-" + Path(source).stem)
                peers = [str(HERE / "phase_peer.cpp")] if source == "phase_tests.cpp" else []
                subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                                *flags, str(HERE / source), *peers, "-o", str(output)], check=True)
                subprocess.run([str(output)], check=True)
    print("PASS: integer QPC intervals, bounded phase attribution, report cadence and eligibility gaps; no GPU required")


if __name__ == "__main__":
    main()
