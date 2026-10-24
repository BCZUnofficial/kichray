#!/usr/bin/env python3
"""Portable production deferred planner/math validation, no fake GPU claims."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='kich-b44-') as folder:
    for name in ('math','session','optics','noise','warmup','shadow_record'):
        binary=Path(folder)/name
        subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-O2','-Wall','-Wextra','-Werror',str(root/'tests/r4_deferred'/f'{name}_tests.cpp'),str(root/'xr_3da/RenderSnapshot.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
    def function(source,signature):
        start=source.index(signature);at=source.index('{',start)+1;depth=1
        while depth:
            depth += (source[at]=='{')-(source[at]=='}');at+=1
        return source[start:at]
    native=(root/'xr_3da/KichR4NativeDraw.cpp').read_text()
    hlsl=(root/'resources/shaders/r4/bridge_deferred.hlsl').read_text()
    # The noise expression is separable by channel: execute its production HLSL
    # as scalar C++, with an independent R2 blend oracle. Real HLSL compiles separately.
    noise=Path(folder)/'noise_shader.cpp'
    noise.write_text('#include <cassert>\n#include <cmath>\n#include <initializer_list>\nfloat lerp(float a,float b,float t){return a+(b-a)*t;}\n'+
        function(hlsl,'float3 PostprocessNoise(').replace('float3','float')+
        '\nint main(){for(float c:{0.f,.05f,.5f,1.f,2.f})for(float n:{0.f,.25f,.5f,1.f})for(float a:{0.f,.25f,.5f,1.f}){'
        'assert(std::fabs(PostprocessNoise(c,n,a)-(c*n*2*a+c*(1-a)))<1e-6f);'
        'assert(PostprocessNoise(0,n,a)==0);assert(PostprocessNoise(c,.5f,a)==c);assert(PostprocessNoise(c,n,0)==c);}}\n')
    binary=Path(folder)/'noise_shader'
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-O2','-Wall','-Wextra','-Werror',str(noise),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
    wallmark=(root/'tests/r4_deferred/wallmark_tests.cpp').read_text()
    wallmark=wallmark.replace('// PRODUCTION_WALLMARK_SCOPE',function(native,'uint32_t KichR4_NativeWallmark('))
    wallmark=wallmark.replace('// PRODUCTION_WALLMARK_SHADER',function(hlsl,'float WallmarkChannel(')+'\n'+function(hlsl,'float WallmarkAlpha(')+'\n'+function(hlsl,'float AuthoredWallmarkShift('))
    source=Path(folder)/'wallmarks.cpp';source.write_text(wallmark)
    binary=Path(folder)/'wallmarks'
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'tests/r4_deferred'),str(source),str(root/'xr_3da/RenderSnapshot.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
    engine=(root/'xr_3da/xrRender/WallmarksEngine.cpp').read_text()
    flush=function(engine,'ICF void FlushStream(')
    assert flush.index('KichR4NativeWallmarkScope native_wallmark') < flush.index('RCache.Render(')
    assert 'kich_r4_wallmark::Capture(wallmark_scope,screen,wallmark_color,cmd);' in native
    source=(root/'xr_3da/xrRender/light.cpp').read_text()
    start=source.index('void light::CaptureR4() const')
    function=source[start:source.index('\nlight::light',start)].strip()
    unit=Path(folder)/'light_hook.cpp'
    unit.write_text((root/'tests/r4_deferred/light_hook_tests.cpp').read_text().replace('// PRODUCTION_HOOK',function))
    for guards in ([],['-D_EDITOR']):
        binary=Path(folder)/'light_hook'
        subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-Wall','-Wextra','-Werror',*guards,'-I'+str(root/'tests/r4_deferred'),str(unit),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
subprocess.run(['python3','-B',str(root/'tests/r4_deferred/run_shadow_tests.py')],check=True)
subprocess.run(['python3','-B',str(root/'tests/r4_deferred/run_sun_tests.py')],check=True)
subprocess.run(['python3','-B',str(root/'tests/r4_deferred/embed_shaders.py'),'--check'],check=True)
