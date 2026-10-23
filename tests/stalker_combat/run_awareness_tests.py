#!/usr/bin/env python3
"""Run production search, peripheral glance and close-pitch methods against controlled world inputs."""
from pathlib import Path
import os
import subprocess
import tempfile
import sys
sys.dont_write_bytecode = True
from run_action_recovery_tests import extract
ROOT=Path(__file__).resolve().parents[2]
GAME=ROOT/'xr_3da/xrGame'
HERE=Path(__file__).resolve().parent

def source():
    header=(GAME/'stalker_combat_actions.h').read_text(encoding='cp1251')
    start=header.index('class CStalkerActionSearchEnemy :')
    result=[header[start:header.index('\n};',start)+3]]
    for name, signatures in (
        ('stalker_combat_actions.cpp', ['CStalkerActionSearchEnemy::CStalkerActionSearchEnemy(',
            'void CStalkerActionSearchEnemy::initialize()', 'void CStalkerActionSearchEnemy::finalize()',
            'void CStalkerActionSearchEnemy::stop_sneak(', 'bool CStalkerActionSearchEnemy::try_sneak(',
            'bool CStalkerActionSearchEnemy::update_sneak(', 'void CStalkerActionSearchEnemy::execute()']),
        ('agent_enemy_manager.cpp',['float stalker_known_enemy_distance(']),
        ('agent_member_manager.cpp', ['bool CAgentMemberManager::claim_scout(',
            'void CAgentMemberManager::release_scout(', 'bool CAgentMemberManager::other_scout(']),
        ('ai/stalker/ai_stalker_fire.cpp', ['bool CAI_Stalker::combat_peripheral_look(',
            'void CAI_Stalker::update_combat_peripheral()']),
        ('sight_manager.cpp',['void CSightManager::SetFirePointLookAngles('])):
        text=(GAME/name).read_text(encoding='cp1251')
        result += [extract(text, signature) for signature in signatures]
    return '\n\n'.join(result)

def main():
    with tempfile.TemporaryDirectory(prefix='stalker-awareness-') as directory:
        temp=Path(directory)
        (temp/'awareness.generated.h').write_text(source())
        binary=temp/'awareness'
        subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-O1','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',
            '-I'+str(temp),'-I'+str(GAME),str(HERE/'awareness_tests.cpp'),'-o',str(binary)],check=True)
        environment=os.environ.copy();environment['ASAN_OPTIONS']='detect_leaks=0:halt_on_error=1'
        environment['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
        subprocess.run([str(binary)],check=True,env=environment)
if __name__=='__main__':main()
