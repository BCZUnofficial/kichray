#!/usr/bin/env python3
"""Execute actual bullet flight-segment capture/delivery with controlled collision results."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
from run_action_recovery_tests import extract
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

def main():
    source = (ROOT / 'xr_3da/xrGame/Level_Bullet_Manager.cpp').read_text(encoding='cp1251')
    with tempfile.TemporaryDirectory(prefix='stalker-bullet-near-miss-') as directory:
        temp = Path(directory)
        (temp / 'bullet_near_miss.generated.h').write_text('\n\n'.join(extract(source, signature) for signature in (
            'void CBulletManager::QueueNearMissSegment(', 'void CBulletManager::CommitNearMissSegments()',
            'bool CBulletManager::CalcBullet(')))
        binary = temp / 'test'
        subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-O1', '-Wall', '-Wextra', '-Werror',
            '-Wno-unknown-pragmas', '-Wno-unused-parameter', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
            '-I'+str(temp), str(HERE / 'bullet_near_miss_tests.cpp'), '-o', str(binary)], check=True)
        env = os.environ.copy()
        env['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
        env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
        subprocess.run([str(binary)], check=True, env=env)

if __name__ == '__main__': main()
