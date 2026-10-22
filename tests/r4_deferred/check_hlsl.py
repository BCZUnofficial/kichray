#!/usr/bin/env python3
"""Independent SM5 source/packing check via glslang; Microsoft FXC is separate."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
tool=os.environ.get('GLSLANG','glslangValidator')
entries=('VSStatic','VSModel','VSModelShadow','VSModelGPU','VSScreen','VSDetail','PSGeometry','PSShadow','PSSun','PSLocal','PSForwardSun','PSForwardLocal','PSPlain','PSCombine','PSDistortion','PSParticleDistortionMask','PSDistortionComposite')
source=root/'resources/shaders/r4/bridge_deferred.hlsl'
with tempfile.TemporaryDirectory(prefix='kich-b44-hlsl-') as folder:
    reflection=''
    for entry in entries:
        result=subprocess.run([tool,'-D','-V','--hlsl-iomap','--hlsl-offsets','-l','-q','-S','vert' if entry.startswith('VS') else 'frag','-e',entry,str(source),'-o',str(Path(folder)/(entry+'.spv'))],text=True,capture_output=True)
        if result.returncode:
            raise SystemExit(result.stdout+result.stderr)
        reflection+=result.stdout+'\n'
    offsets={'world':0,'mvp':64,'normal_matrix':128,'inverse_vp':192,'shadow':256,'camera':640,'screen':656,'sun_direction':672,'sun_color':688,'ambient':704,'hemi':720,'fog':736,'light_position':752,'light_direction':768,'light_color':784,'alpha':800}
    for name,offset in offsets.items():
        if not re.search(r'\b'+name+r'(?:\[0\])?: offset '+str(offset)+r'\b',reflection):
            raise SystemExit('Missing/wrong HLSL member offset: '+name)
    if not re.search(r'Frame: .*size 816\b',reflection):
        raise SystemExit('Wrong HLSL cbuffer size')
    if not re.search(r'\bmodel_header: offset 16384\b',reflection) or not re.search(r'ModelPose: .*size 16400\b',reflection):
        raise SystemExit('Wrong G3b model palette packing')
print('PASS: HLSL entries, 816-byte frame and 16400-byte model palette packing (glslang, not FXC/GPU)')
