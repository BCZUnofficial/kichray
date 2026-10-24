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
    def function(source,signature):
        start=source.index(signature);at=source.index('{',start)+1;depth=1
        while depth:
            depth += (source[at]=='{')-(source[at]=='}');at+=1
        return source[start:at]
    native=(root/'xr_3da/KichR4NativeDraw.cpp').read_text()
    hlsl=(root/'resources/shaders/r4/bridge_deferred.hlsl').read_text()
    wallmark=(root/'tests/r4_deferred/wallmark_tests.cpp').read_text()
    wallmark=wallmark.replace('// PRODUCTION_WALLMARK_SCOPE',function(native,'uint32_t KichR4_NativeWallmark('))
    wallmark=wallmark.replace('// PRODUCTION_WALLMARK_SHADER',function(hlsl,'float WallmarkChannel(')+'\n'+function(hlsl,'float WallmarkAlpha('))
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
subprocess.run(['python3','-B',str(root/'tests/r4_deferred/embed_shaders.py'),'--check'],check=True)
