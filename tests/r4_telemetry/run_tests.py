#!/usr/bin/env python3
"""Build and run portable production-recorder telemetry/lifecycle tests."""
import argparse
import os
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--output-dir', type=pathlib.Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='kich-r4-telemetry-') as temporary:
    work = pathlib.Path(temporary)
    output = args.output_dir or work / 'logs'
    output.mkdir(parents=True, exist_ok=True)
    executable = work / 'recorder_tests'
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
                    str(root / 'tests/r4_telemetry/recorder_tests.cpp'),
                    str(root / 'xr_3da/RenderSnapshot.cpp'), '-o', str(executable)], check=True)
    subprocess.run([str(executable), str(output)], check=True)
