#!/usr/bin/env python3
"""Execute unchanged production CPU rasterizer and native volume projection.
The engine/Win32 service seam is a fixture; no D3D/GPU results are claimed.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='kich-e13-hom-') as directory:
    work=Path(directory)
    units=[]
    for name in ('occRasterizer','occRasterizer_core'):
        text=(ROOT/'xr_3da/xrRender'/f'{name}.cpp').read_text()
        text=text.replace('#include "stdafx.h"','#include "raster_fixture.h"')
        unit=work/f'{name}.cpp';unit.write_text(text);units.append(str(unit))
    flags=[os.environ.get('CXX','g++'),'-std=c++11','-O2','-fno-strict-aliasing','-Wall','-Wextra','-Werror',
           '-I'+str(HERE),'-I'+str(ROOT/'xr_3da/xrRender'),'-I'+str(ROOT/'xr_3da')]
    binary=work/'occlusion'
    subprocess.run([*flags,*units,str(HERE/'occlusion_tests.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
    adapter=(ROOT/'xr_3da/KichR4NativeHOM.cpp').read_text()
    adapter='\n'.join(line for line in adapter.splitlines() if not line.startswith('#include '))
    adapter_path=work/'native_hom.cpp';adapter_path.write_text('#include "native_fixture.h"\n#include "KichR4Occlusion.h"\n'+adapter)
    binary=work/'native'
    subprocess.run([*flags,*units,str(adapter_path),str(HERE/'native_tests.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
    tree=ET.parse(ROOT/'xr_3da/XR_3DA.vcxproj');ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'}
    sources=tree.findall('.//m:ClCompile',ns)
    for expected in ('KichR4NativeHOM.cpp','xrRender\\HOM.cpp','xrRender\\occRasterizer.cpp','xrRender\\occRasterizer_core.cpp'):
        hits=[node for node in sources if node.get('Include')==expected]
        assert len(hits)==1,expected
        assert hits[0].find('m:PrecompiledHeader',ns).text=='NotUsing'
        assert 'KichR4NativePrelude.h' in hits[0].find('m:ForcedIncludeFiles',ns).text
    print('PASS: HOM/Raster sources registered once in XR_3DA with native frontend compilation contract')
