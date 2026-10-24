#!/usr/bin/env python3
"""Run the bounded recorder with the actual FrameMove dispatch wrapper/registrator."""
from pathlib import Path
import os
import platform
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]

def function(source, prefix):
    begin=source.index(prefix)
    opening=source.index('{',begin)
    depth=1
    end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}')
        end+=1
    return source[begin:end]

def main():
    source=(ROOT/'xr_3da/device.cpp').read_text()
    wrapper=function(source,'void __fastcall KichR4_MeasuredFrame(')
    wrapper=wrapper.replace('CallbackScope<>','CallbackScope<FakeClock>')
    frame=function(source,'void CRenderDevice::FrameMove()')
    assert 'kich_r4_record::Enabled() && KichR4_NativeActive()' in frame
    assert 'record_updates ? KichR4_MeasuredFrame : rp_Frame' in frame
    assert 'seqFrame.Process(frame_callback)' in frame
    assert 'Device.seqFrame.Process(f)' in function(source,'void ProcessLoading(RP_FUNC* f)\n')
    assert 'g_bEnableStatGather' not in frame
    presenter=(ROOT/'xr_3da/KichR4NativePresenter.cpp').read_text()
    assert presenter.index('ReportDue(end.QuadPart)') < presenter.index('kich_r4_update::EmitReport(native.record)')
    for name in ('IGame_Level.cpp','IGame_Persistent.cpp','x_ray.cpp','xrSheduler.cpp'):
        assert b'kich_r4_update::Scope<>' in (ROOT/'xr_3da'/name).read_bytes()
    with tempfile.TemporaryDirectory(prefix='kich-e18-update-') as folder:
        tmp=Path(folder);(tmp/'update_callback_actual.h').write_text(wrapper)
        # Windows unsigned long is 32-bit; Linux LP64 must use the same constant
        # width for the legacy signed-Prio/REG_PRIORITY_INVALID comparison.
        registrator=(ROOT/'xr_3da/pure.h').read_text().replace('ul\n','u\n')
        (tmp/'pure_actual_win32.h').write_text(registrator)
        variants=[('native',[]),('no-rtti',['-fno-rtti'])]
        if platform.machine().lower() in ('x86_64','amd64','i386','i686'):
            variants.append(('x87-pc24',['-mfpmath=387','-DKICH_TEST_X87']))
        for suffix,extra in variants:
            output=tmp/suffix
            subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-O2','-Wall','-Wextra','-Werror','-pthread',*extra,
                '-I'+str(tmp),'-I'+str(ROOT/'xr_3da'),str(HERE/'update_tests.cpp'),'-o',str(output)],check=True)
            subprocess.run([str(output)],check=True)

if __name__=='__main__':main()
