#!/usr/bin/env python3
"""Execute production stream submission, R2 policy and shadow hooks with a fake backend.
The recorder, dictionary helpers and extracted production branches remain unchanged.
No graphics-driver or visible-pixel claim is made by this portable suite.
"""
from pathlib import Path
import os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
def function(file,signature):
    source=(ROOT/file).read_text();start=source.index(signature);body=source.index('{',start);depth=1;end=body+1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
functions=[function('xr_3da/xrRender/WallmarksEngine.cpp','ICF void FlushStream(')]
source=(ROOT/'xr_3da/xrRender/Light_DB.cpp').read_text();start=source.index('#if RENDER == R_R2\nvoid CLight_DB::add_light');end=source.index('\n#endif',start)
functions.append(source[source.index('void CLight_DB::add_light',start):end])
functions += [function('xr_3da/xrRender_R2/r2_rendertarget_phase_smap_S.cpp','static void snapshot_smap_spot('),function('xr_3da/xrRender_R2/r2_rendertarget_phase_smap_D.cpp','static void snapshot_smap_direct(')]
with tempfile.TemporaryDirectory(prefix='kich-g3b1-wallmark-') as directory:
    work=Path(directory);unit=work/'wallmark_light.cpp'
    unit.write_text((ROOT/'tests/r4_telemetry/wallmark_light_tests.cpp').read_text().replace('// PRODUCTION_FUNCTIONS','\n'.join(functions)))
    binary=work/'wallmark_light'
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-O2','-Wall','-Wextra','-Werror','-pthread','-I'+str(ROOT/'xr_3da'),str(unit),str(ROOT/'xr_3da/RenderSnapshot.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary),directory],check=True)
