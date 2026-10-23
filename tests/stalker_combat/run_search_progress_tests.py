#!/usr/bin/env python3
"""Exercise production hold/detour actions that gate remembered-position search."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
from run_action_recovery_tests import extract
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
GAME=ROOT/'xr_3da/xrGame'

def main():
    source=(GAME/'stalker_combat_actions.cpp').read_text(encoding='cp1251')
    header=(GAME/'stalker_combat_actions.h').read_text(encoding='cp1251')
    parts=[]
    for name in ('HoldPosition','DetourEnemy'):
        start=header.index('class CStalkerAction'+name+' :')
        parts.append(header[start:header.index('\n};',start)+3])
        for signature in ('CStalkerAction'+name+'::CStalkerAction'+name+'(',
                          'void CStalkerAction'+name+'::initialize()',
                          'void CStalkerAction'+name+'::execute()',
                          'void CStalkerAction'+name+'::finalize()'):
            parts.append(extract(source,signature))
    with tempfile.TemporaryDirectory(prefix='stalker-search-progress-') as directory:
        temp=Path(directory);(temp/'search_progress.generated.h').write_text('\n\n'.join(parts))
        binary=temp/'test'
        subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-O1','-Wall','-Wextra','-Werror','-Wno-comment','-Wno-misleading-indentation',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',
            '-I'+str(temp),str(HERE/'search_progress_tests.cpp'),'-o',str(binary)],check=True)
        environment=os.environ.copy();environment['ASAN_OPTIONS']='detect_leaks=0:halt_on_error=1'
        environment['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
        subprocess.run([str(binary)],check=True,env=environment)

if __name__=='__main__':main()
