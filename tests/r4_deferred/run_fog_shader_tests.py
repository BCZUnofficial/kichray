#!/usr/bin/env python3
"""Execute production fog/combine HLSL arithmetic with controlled scene samples."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'resources/shaders/r4/bridge_deferred.hlsl').read_text()

def function(signature):
    start = source.index(signature)
    at = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
    return source[start:at]

helpers = '\n'.join(function(signature) for signature in (
    'float FogAmount(', 'float FogTransmittance(', 'float3 ForwardFog(', 'float4 PSCombine('))
helpers = helpers.replace(':SV_Target', '').replace('discard;', 'throw Discard();')
helpers = helpers.replace('saturate(length(LoadReceiverPosition(v.p.xy,depth))/camera.w).xxx',
                          'splat3(saturate(length(LoadReceiverPosition(v.p.xy,depth))/camera.w))')
helpers = re.sub(r'\b(\w+(?:\.\w+)*)\.(?:rgb|xyz)\b', r'xyz(\1)', helpers)
helpers = re.sub(r'\b(\w+(?:\.\w+)*)\.xy\b', r'xy(\1)', helpers)
helpers = re.sub(r'\b(\w+(?:\.\w+)*)\.aaa\b', r'splat3(\1.w)', helpers)
helpers = re.sub(r'\b(\w+(?:\.\w+)*)\.zzz\b', r'splat3(\1.z)', helpers)
helpers = re.sub(r'//[^\n]*', '', helpers).replace(';', ';\n')
fixture = (root / 'tests/r4_deferred/fog_shader_tests.cpp').read_text().replace('// PRODUCTION_FOG_HELPERS', helpers)
with tempfile.TemporaryDirectory(prefix='kich-e18-fog-') as directory:
    cpp, binary = Path(directory) / 'fog.cpp', Path(directory) / 'fog'
    cpp.write_text(fixture)
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
