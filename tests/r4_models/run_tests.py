#!/usr/bin/env python3
"""Compile production model planning and recording API: no GPU timing/visual claim."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='kich-g3b-models-') as directory:
    for name in ('session', 'material_session'):
        binary=Path(directory)/name
        subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-O2','-Wall','-Wextra','-Werror',str(HERE/(name+'_tests.cpp')),str(ROOT/'xr_3da/RenderSnapshot.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary),directory],check=True)
print('PASS: G3c palette class boundaries, exact identity reuse, reset, GPU byte accounting, pool budget and partial resource ownership')
