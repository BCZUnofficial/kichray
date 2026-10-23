#!/usr/bin/env python3
"""Run actual hearing admission/snapshot methods without an audio device or game."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
from run_action_recovery_tests import extract
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent

def main():
    text=(ROOT/'xr_3da/xrGame/sound_memory_manager.cpp').read_text(encoding='cp1251')
    methods=[extract(text,signature) for signature in (
        'IC bool is_sound_type(',
        'void CSoundMemoryManager::feel_sound_new(',
        'void CSoundMemoryManager::add(const CObject*')]
    with tempfile.TemporaryDirectory(prefix='stalker-sound-reaction-') as directory:
        temp=Path(directory)
        (temp/'sound_reaction.generated.h').write_text('\n\n'.join(methods))
        binary=temp/'sound_reaction'
        subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-O1','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',
            '-I'+str(temp),str(HERE/'sound_reaction_tests.cpp'),'-o',str(binary)],check=True)
        env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0:halt_on_error=1'
        env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
        subprocess.run([str(binary)],check=True,env=env)
if __name__=='__main__':main()
