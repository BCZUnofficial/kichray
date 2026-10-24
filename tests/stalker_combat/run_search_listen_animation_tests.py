#!/usr/bin/env python3
"""Execute production native search-listen lifecycle against controlled animation inputs."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
from run_action_recovery_tests import extract
HERE = Path(__file__).resolve().parent
GAME = HERE.parents[1] / 'xr_3da/xrGame'

def check_script_dependencies(source):
    # Extracted methods do not compile the engine's includes. Guard the actual
    # translation-unit dependency that caused C2027/C2079 in the MSVC build.
    includes = re.findall(r'^\s*#include\s+"([^"]+)"', source, re.MULTILINE)
    assert includes[0] == 'stdafx.h', 'Keep the configured stdafx.h PCH first'
    assert 'pch_script.h' in includes, 'Enemy callbacks need complete luabind types'
    assert includes.index('pch_script.h') < includes.index('enemy_manager.h'), \
        'Load luabind definitions before instantiating enemy callbacks'

def main():
    source = (GAME / 'stalker_animation_global.cpp').read_text(encoding='cp1251')
    check_script_dependencies(source)
    update = (GAME / 'stalker_animation_manager_update.cpp').read_text(encoding='cp1251')
    methods = '\n\n'.join(extract(source, signature) for signature in (
        'u32 CStalkerAnimationManager::search_listen_slot()',
        'bool CStalkerAnimationManager::search_listen_allowed()',
        'bool CStalkerAnimationManager::start_search_listen(',
        'bool CStalkerAnimationManager::search_listen_active()',
        'void CStalkerAnimationManager::stop_search_listen()',
        'bool CStalkerAnimationManager::play_search_listen()',
    )) + '\n\n' + extract(update, 'void CStalkerAnimationManager::update_impl()')
    with tempfile.TemporaryDirectory(prefix='stalker-search-listen-') as directory:
        temp = Path(directory)
        (temp / 'search_listen_animation.generated.h').write_text(methods)
        for head_parts in (False, True):
            binary = temp / ('test-head' if head_parts else 'test-full')
            flags = ['-DUSE_HEAD_BONE_PART_FAKE'] if head_parts else []
            subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
                '-I'+str(temp), *flags, str(HERE / 'search_listen_animation_tests.cpp'), '-o', str(binary)], check=True)
            env = os.environ.copy()
            env['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
            env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
            subprocess.run([str(binary)], check=True, env=env)

if __name__ == '__main__': main()
