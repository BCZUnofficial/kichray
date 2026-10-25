#!/usr/bin/env python3
"""Independent SM5 source/packing check via glslang; Microsoft FXC is separate."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
tool=os.environ.get('GLSLANG','glslangValidator')
entries=('VSStatic','VSModel','VSModelShadow','VSModelGPU','VSScreen','VSDetail','PSGeometry','PSShadow','PSSun','PSLocal','PSForwardSun','PSForwardLocal','PSPlain','PSWallmark','PSCelestial','PSCombine','PSSky','PSClouds','PSWater','PSPostprocess','PSDistortion','PSParticleDistortionMask','PSDistortionComposite','PSBloomBuild','PSBloomFilter','PSBloomFilterFast','PSBloomComposite','PSLuminance1','PSLuminance2','PSLuminance3','PSSunShafts','PSSunShaftsComposite','PSHudBloomSource','PSSunShaftsBlur')
source=root/'resources/shaders/r4/bridge_deferred.hlsl'
with tempfile.TemporaryDirectory(prefix='kich-b44-hlsl-') as folder:
    reflection=''
    for entry in entries:
        result=subprocess.run([tool,'-D','-V','--hlsl-iomap','--hlsl-offsets','-l','-q','-S','vert' if entry.startswith('VS') else 'frag','-e',entry,str(source),'-o',str(Path(folder)/(entry+'.spv'))],text=True,capture_output=True)
        if result.returncode:
            raise SystemExit(result.stdout+result.stderr)
        reflection+=result.stdout+'\n'
    offsets={'world':0,'mvp':64,'normal_matrix':128,'inverse_vp':192,'shadow':256,'camera':640,'screen':656,'sun_direction':672,'sun_color':688,'ambient':704,'hemi':720,'fog':736,'light_position':752,'light_direction':768,'light_color':784,'alpha':800,'material':816,'color_scale':832,'color_bias':848,'sun_scale_bias':864,'tree_wave':880,'tree_wind':896,'fog_params':912,'fog_plane':928,'receiver_inverse':944,'env_ambient':1008,'env_color':1024,'sunmask_u':1040,'sunmask_v':1056}
    for name,offset in offsets.items():
        if not re.search(r'\b'+name+r'(?:\[0\])?: offset '+str(offset)+r'\b',reflection):
            raise SystemExit('Missing/wrong HLSL member offset: '+name)
    if not re.search(r'Frame: .*size 1072\b',reflection):
        raise SystemExit('Wrong HLSL cbuffer size')
    if not re.search(r'\bmodel_header: offset 0\b',reflection) or not re.search(r'ModelPose: .*size 16400\b',reflection):
        raise SystemExit('Wrong G3c model palette packing')
    for capacity in (16,32,64,128,256):
        result=subprocess.run([tool,'-D','-DKICH_R4_MODEL_BONE_CAPACITY='+str(capacity),'-V','--hlsl-iomap','--hlsl-offsets','-l','-q','-S','vert','-e','VSModelGPU',str(source),'-o',str(Path(folder)/('model'+str(capacity)+'.spv'))],text=True,capture_output=True)
        if result.returncode or not re.search(r'ModelPose: .*size '+str(16+64*capacity)+r'\b',result.stdout) or not re.search(r'\bmodel_bones(?:\[0\])?: offset 16\b',result.stdout):
            raise SystemExit('Wrong class-sized palette '+str(capacity)+'\n'+result.stdout+result.stderr)
print('PASS: HLSL entries, 1072-byte frame, five header-first palette classes (glslang, not FXC/GPU)')
