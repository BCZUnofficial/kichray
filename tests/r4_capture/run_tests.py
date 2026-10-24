#!/usr/bin/env python3
"""Exercise production CPU capture and engine adapter without a graphics driver."""
from pathlib import Path
import os
import subprocess
import tempfile
import re

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

def engine_source():
    source = (ROOT/"xr_3da/KichR4CaptureEngine.cpp").read_text()
    return source.replace('#include "stdafx.h"', '#include "fake_engine.h"').replace('#include "SkeletonCustom.h"', '')

def legacy_reference():
    header=(ROOT/"xr_3da/SkeletonX.h").read_text()
    structures=[]
    for name in ("vertBoned2W", "vertRender"):
        start=header.index("struct "+name)
        structures.append(header[start:header.index("\n};",start)+3])
    source=(ROOT/"xr_3da/xrCPU_Pipe/xrSkin2W.cpp").read_text()
    function=source[source.index("void __stdcall xrSkin2W_x86"):]
    hardware=(ROOT/"xr_3da/xrRender/FSkinned.cpp").read_text()
    expression=re.search(r"u8\(clampr\(iFloor\(w \* 255\.f \+ \.5f\), 0, 255\)\)",hardware).group(0)
    vector=(ROOT/"xrCore/_vector3d.h").read_text()
    start=vector.index("IC SelfRef lerp(");start=vector.index("{",start)
    lerp=vector[start:vector.index("\n\t}",start)+3].replace("T invt", "float invt")
    prelude='''#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include "KichR4TestScene.h"
#define __stdcall
typedef uint16_t u16;typedef uint8_t u8;typedef uint32_t u32;
struct Fvector {float x,y,z;void set(const Fvector& p){*this=p;}
    Fvector& lerp(const Fvector& p1,const Fvector& p2,float t) LERP_BODY};
struct Fmatrix {kich_r4_scene::Matrix data;
    void transform_tiny(Fvector& out,const Fvector& p){const float* m=data.m;out={p.x*m[0]+p.y*m[4]+p.z*m[8]+m[12],p.x*m[1]+p.y*m[5]+p.z*m[9]+m[13],p.x*m[2]+p.y*m[6]+p.z*m[10]+m[14]};}
    void transform_dir(Fvector& out,const Fvector& p){const float* m=data.m;out={p.x*m[0]+p.y*m[4]+p.z*m[8],p.x*m[1]+p.y*m[5]+p.z*m[9],p.x*m[2]+p.y*m[6]+p.z*m[10]};}};
struct CBoneInstance {Fmatrix mRenderTransform;};
inline int iFloor(float w){return static_cast<int>(floorf(w));}
inline int clampr(int w,int low,int high){return w<low?low:w>high?high:w;}
'''
    return prelude.replace('LERP_BODY',lerp)+'\n'.join(structures)+'\n'+function+'\ninline float LegacyHardwareWeight(float w){return float('+expression+')/255.f;}\n'

def main():
    cxx = os.environ.get("CXX", "g++")
    with tempfile.TemporaryDirectory(prefix="kich-b43b-") as folder:
        adapter = Path(folder)/"engine.cpp"
        adapter.write_text(engine_source())
        (Path(folder)/"legacy_skin_reference.h").write_text(legacy_reference())
        flags = [cxx,"-std=c++11","-O2","-Wall","-Wextra","-Werror","-I"+str(HERE),"-I"+folder,"-I"+str(ROOT/"xr_3da")]
        for name in ("data", "geometry", "engine", "session", "skin", "light_visibility", "lighting_frame", "projector"):
            output = Path(folder)/name
            sources = [str(HERE/(name+"_tests.cpp")),str(ROOT/"xr_3da/KichR4Capture.cpp"),str(ROOT/"xr_3da/RenderSnapshot.cpp")]
            if name == "engine": sources.append(str(adapter))
            subprocess.run([*flags,*sources,"-o",str(output)],check=True)
            subprocess.run([str(output)],check=True)
    print("PASS: B4.3b CPU model / hands / HUD capture and native-stream submission contracts")

if __name__ == "__main__":
    main()
