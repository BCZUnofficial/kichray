#!/usr/bin/env python3
"""Portable production deferred planner/math validation, no fake GPU claims."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='kich-b44-') as folder:
    for name in ('math','session','optics'):
        binary=Path(folder)/name
        subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-O2','-Wall','-Wextra','-Werror',str(root/'tests/r4_deferred'/f'{name}_tests.cpp'),str(root/'xr_3da/RenderSnapshot.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
    source=(root/'xr_3da/xrRender/light.cpp').read_text()
    start=source.index('void light::CaptureR4() const')
    function=source[start:source.index('\nlight::light',start)].strip()
    unit=Path(folder)/'light_hook.cpp'
    unit.write_text((root/'tests/r4_deferred/light_hook_tests.cpp').read_text().replace('// PRODUCTION_HOOK',function))
    for guards in ([],['-D_EDITOR']):
        binary=Path(folder)/'light_hook'
        subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-Wall','-Wextra','-Werror',*guards,'-I'+str(root/'tests/r4_deferred'),str(unit),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
subprocess.run(['python3','-B',str(root/'tests/r4_deferred/embed_shaders.py'),'--check'],check=True)
