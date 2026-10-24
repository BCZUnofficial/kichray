#!/usr/bin/env python3
"""Execute production HLSL filter against independent analytic depth planes.
This checks math/sampling contracts, not a D3D driver or runtime appearance.
"""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'resources/shaders/r4/bridge_deferred.hlsl').read_text()
def function(signature):
    start=source.index(signature);at=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[at]=='{')-(source[at]=='}');at+=1
    return source[start:at]
helpers='\n'.join(function(x) for x in ('bool ShadowPlaneStable(', 'float2 ShadowGradient(', 'float ShadowTent(', 'float3 ShadowDerivative(', 'float PCF('))
helpers=helpers.replace('[unroll]','').replace('projected.xyz','xyz(projected)').replace('derivative.xyz','xyz(derivative)')
# The vector arithmetic shims preserve the production branch/sample order.
fixture=(root/'tests/r4_deferred/shadow_filter_tests.cpp').read_text().replace('// PRODUCTION_SHADOW_HELPERS',helpers)
with tempfile.TemporaryDirectory(prefix='kich-e15-shadow-') as directory:
    cpp=Path(directory)/'shadow.cpp';cpp.write_text(fixture)
    binary=Path(directory)/'shadow'
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'tests/r4_deferred'),str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
# Production derivatives must precede all flow that can diverge within a quad.
for entry in ('PSSun','PSLocal'):
    body=function('float4 '+entry+'(')
    assert body.index('ShadowDerivatives(')<body.index('if(depth>=1)')
for entry in ('PSForwardSun','PSForwardLocal'):
    body=function('float4 '+entry+'(')
    assert body.index('ShadowDerivatives(')<body.index('Base(v)')
for entry in ('PCF','SunVisibility','LocalVisibility','LocalLight'):
    body=function(('float4 ' if entry=='LocalLight' else 'float ')+entry+'(')
    assert 'ddx(' not in body and 'ddy(' not in body
backend=(root/'xr_3da/xrRender_R4/R4PipelineDX11.cpp').read_text()
assert 's.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT' in backend
print('PASS: derivative placement and point comparison sampler match PCF contract')
