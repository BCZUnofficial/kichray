#!/usr/bin/env python3
"""Production regressions for the AI-P9 audit; external geometry is simulated."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from run_action_recovery_tests import extract

HERE = Path(__file__).resolve().parent
GAME = HERE.parents[1] / 'xr_3da/xrGame'


def source(name):
    return (GAME / name).read_text(encoding='cp1251')


def main():
    fire = source('ai/stalker/ai_stalker_fire.cpp')
    actions = source('stalker_combat_actions.cpp')
    danger = source('danger_manager.cpp')
    chunks = {
        'grenade': [extract(actions, signature) for signature in (
            'void CStalkerGrenadeEscape::reset(', 'void CStalkerGrenadeEscape::update(')] +
            [extract(source('stalker_danger_grenade_actions.cpp'), signature) for signature in (
            'void CStalkerActionDangerGrenadeTakeCover::initialize()',
            'void CStalkerActionDangerGrenadeTakeCover::execute()')],
        'assignment': [extract(source('agent_enemy_manager.cpp'), 'void CAgentEnemyManager::assign_enemies()')],
        'danger': [danger[danger.index('struct CDangerPredicate'):danger.index('struct CFindPredicate')],
                   extract(danger, 'void CDangerManager::remove_links(')],
        'pickup': [extract(source('item_manager.cpp'), 'bool CItemManager::useful(')] +
                  [extract(fire, signature) for signature in (
                      'bool CAI_Stalker::update_combat_pickup(',
                      'void CAI_Stalker::update_best_item_info()',
                      'bool CAI_Stalker::item_to_kill()',
                      'bool CAI_Stalker::remember_item_to_kill()')] +
                  [extract(actions, 'void CStalkerActionGetItemToKill::execute()')],
    }
    # Also guard the second insertion path: vector growth must invalidate selection.
    insertion = extract(danger, 'void CDangerManager::add(const CDangerObject&')
    assert insertion.index('m_selected = 0;') < insertion.index('m_objects.push_back(object);')
    with tempfile.TemporaryDirectory(prefix='stalker-audit-') as directory:
        folder = Path(directory)
        (folder / 'grenade_state.generated.h').write_text(
            extract(source('stalker_combat_actions.h'), 'struct CStalkerGrenadeEscape') + ';\n')
        for name, methods in chunks.items():
            (folder / ('audit_' + name + '.generated.h')).write_text('\n'.join(methods))
            binary = folder / name
            subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-O1', '-g',
                '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
                '-I' + str(GAME), '-I' + str(folder),
                str(HERE / ('audit_' + name + '_tests.cpp')), '-o', str(binary)], check=True)
            env = os.environ.copy()
            env['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
            env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
            subprocess.run([str(binary)], check=True, timeout=5, env=env)


if __name__ == '__main__':
    main()
