#!/usr/bin/env python3
"""Execute real alpha shader/adapters against current SoC R2 pass boundaries."""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
def function(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]
shader = (root / 'resources/shaders/r4/bridge_deferred.hlsl').read_text()
services = (root / 'xr_3da/KichR4NativeServices.cpp').read_text()
source = (root / 'tests/r4_materials/alpha_tests.cpp').read_text()
source = source.replace('// PRODUCTION_ALPHA_TEST', function(shader, 'void AlphaTest(').replace('discard;', 'throw false;'))
source = source.replace('// PRODUCTION_MATERIAL_STATE', function(services, 'bool KichR4_NativeMaterialState('))
source = source.replace('// PRODUCTION_PASS_STATE', function(services, 'kich_r4_material::PassState NativePassState('))
with tempfile.TemporaryDirectory(prefix='kich-e19-alpha-') as temp:
    cpp, binary = Path(temp) / 'alpha.cpp', Path(temp) / 'alpha'
    cpp.write_text(source)
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'xr_3da'), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
