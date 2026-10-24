#!/usr/bin/env python3
"""Exercise production stalker incoming-fire callbacks; no Windows build or game run."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
from run_action_recovery_tests import extract
ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / 'xr_3da/xrGame'
HERE = Path(__file__).resolve().parent


def main():
    source = (GAME / 'ai/stalker/ai_stalker_fire.cpp').read_text(encoding='cp1251')
    methods = [extract(source, signature) for signature in (
        'template <typename T>\nvoid stalker_collect_known_fire(',
        'bool CAI_Stalker::combat_known_friendly_fire(',
        'void CAI_Stalker::on_combat_ally_harm(',
        'void CAI_Stalker::notify_visible_ally_harm()',
        'bool CAI_Stalker::combat_recent_ally_harm()',
        'bool CAI_Stalker::bullet_near_miss_ready()',
        'void CAI_Stalker::on_bullet_near_miss(',
        'u32 CAI_Stalker::combat_near_miss_revision()',
        'void CAI_Stalker::on_combat_heard_shot(',
        'bool CAI_Stalker::combat_alert_look(',
        'bool CAI_Stalker::combat_near_miss_recent(',
        'void CAI_Stalker::process_combat_near_miss()',
        'void CAI_Stalker::update_combat_near_miss()')]
    hit = extract(source, 'void CAI_Stalker::Hit(')
    assert hit.index('const float previous_health') < hit.index('inherited::Hit(&HDS);') < hit.index('notify_visible_ally_harm();')
    assert 'was_alive && g_Alive() && conditions().health() < previous_health' in hit
    start = source.index('struct StalkerKnownFireContact')
    end = source.index('};', start) + 2
    methods.insert(0, source[start:end])
    # The real movement manager copies target posture during update; a callback
    # applied after it would otherwise be overwritten by the next planner tick.
    lifecycle = (GAME / 'ai/stalker/ai_stalker.cpp').read_text(encoding='cp1251')
    death = extract(lifecycle, 'void CAI_Stalker::Die(')
    assert death.index('notify_visible_ally_harm();') < death.index('inherited::Die(who);')
    scheduled = extract(lifecycle, 'void CAI_Stalker::shedule_Update(')
    assert scheduled.index('process_combat_near_miss();') < scheduled.index('memory().update(dt);')
    think = extract(lifecycle, 'void CAI_Stalker::Think()')
    assert think.index('brain().update(') < think.index('update_combat_near_miss();') < think.index('movement().update(')
    assert 'm_combat_ally_harm_known = false;' in extract(lifecycle, 'void CAI_Stalker::reinit()')
    assert 'm_combat_near_miss = stalker_combat_awareness::NearMiss();' in extract(lifecycle, 'void CAI_Stalker::reinit()')
    sight = extract((GAME / 'sight_manager.cpp').read_text(encoding='cp1251'), 'void CSightManager::update()')
    assert 'object().combat_alert_look(peripheral) || object().combat_peripheral_look(peripheral)' in sight
    with tempfile.TemporaryDirectory(prefix='stalker-near-miss-') as directory:
        temp = Path(directory)
        (temp / 'near_miss.generated.h').write_text('\n\n'.join(methods))
        binary = temp / 'near_miss'
        subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-O1', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
            '-I' + str(temp), '-I' + str(GAME), str(HERE / 'near_miss_tests.cpp'), '-o', str(binary)], check=True)
        environment = os.environ.copy()
        environment['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
        environment['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
        subprocess.run([str(binary)], check=True, env=environment)


if __name__ == '__main__':
    main()
