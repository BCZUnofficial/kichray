#!/usr/bin/env python3
"""Compile actual EXE command classes and queue timing functions against a tiny console."""
from pathlib import Path
import os
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
    commands=(ROOT/'xr_3da/xr_ioc_cmd.cpp').read_text()
    begin=commands.index('// E1.7 recording commands.')
    end=commands.index('// End E1.7 recording commands.',begin)
    draw=(ROOT/'xr_3da/KichR4NativeDraw.cpp').read_text()
    clocks='\n'.join(function(draw,prefix) for prefix in ('void BeginQueueClock()', 'double KichR4_NativeQueueTime()'))
    clocks=clocks.replace('std::chrono::steady_clock::now()', 'QueueNow()')
    device=(ROOT/'xr_3da/device.cpp').read_text()
    assert device.index('kich_r4_record::BeginFrame(dwFrame + 1)') < device.index('kich_r4_phases::Tracker* phase_clock')
    assert 'kich_r4_record::Measure(g_render_snapshot_active) && KichR4_NativeActive()' in device
    assert 'BeginQueueClock();' in function(draw,'void KichR4_NativeBegin(')
    with tempfile.TemporaryDirectory(prefix='kich-e17-controls-') as folder:
        tmp=Path(folder)
        (tmp/'record_commands_actual.h').write_text(commands[begin:end])
        (tmp/'queue_clock_actual.h').write_text(clocks)
        output=tmp/'controls'
        subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-O2','-Wall','-Wextra','-Werror',
            '-I'+str(tmp),'-I'+str(ROOT/'xr_3da'),str(HERE/'control_commands_tests.cpp'),str(HERE/'control_peer.cpp'),'-o',str(output)],check=True)
        subprocess.run([str(output)],check=True)

if __name__=='__main__':main()
