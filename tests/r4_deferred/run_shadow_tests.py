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
helpers='\n'.join(function(x) for x in ('float3 RelativePosition(', 'float3 WorldPosition(', 'float3 ReceiverPosition(', 'float3 ShadowNumericalOffset(', 'bool ShadowPlaneStable(', 'float ShadowPlaneErrorSquared(', 'bool ShadowGradient(', 'float ShadowTent(', 'float3 ShadowDerivative(', 'float PCF(', 'float PCFWide(', 'void ShadowNormalPlane('))
helpers=helpers.replace('float PCF(Texture2D map','float PCF(const Texture2D& map').replace('float PCFWide(Texture2D map','float PCFWide(const Texture2D& map').replace('[unroll]','').replace('[branch]','').replace('inout float3','float3&').replace('projected.xyz','xyz(projected)').replace('derivative.xyz','xyz(derivative)')
helpers=helpers.replace('p.xyz','xyz(p)').replace('camera.xyz','xyz(camera)')
helpers=helpers.replace('out float2 gradient','float2& gradient')
helpers=helpers.replace('receiver_inverse[3][3]', 'receiver_inverse.m[15]')
helpers=helpers.replace('return mul(float4(ndc*linear_depth,linear_depth,1),receiver_inverse).xyz;', 'return xyz(mul(float4(ndc*linear_depth,linear_depth,1),receiver_inverse));')
# P0-S5 cascade selection: production SunFarVisibility/SunCascadeVisibility/SunVisibility.
import re
cascades='\n'.join(function(x) for x in ('float SunFarVisibility(','float SunCascadeVisibility(','float SunVisibility('))
cascades=re.sub(r'\b(\w+)\.xyz\b',r'xyz(\1)',cascades.replace('[branch]','').replace('shadow[2][3][3]','shadow[2].m[15]'))
helpers+='\n'+cascades
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
for entry in ('PCF','PCFWide','SunFarVisibility','SunCascadeVisibility','SunVisibility','SunMask','LocalVisibility','LocalLight','ShadowNormalPlane','SunLighting'):
    body=function(('float4 ' if entry in ('LocalLight','SunLighting') else 'void ' if entry=='ShadowNormalPlane' else 'float ')+entry+'(')
    assert 'ddx(' not in body and 'ddy(' not in body
backend=(root/'xr_3da/xrRender_R4/R4PipelineDX11.cpp').read_text()
assert 's.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT' in backend
print('PASS: derivative placement and point comparison sampler match PCF contract')
