#!/usr/bin/env python3
"""P0-S11 R4 performance summary of a log_record session.

Usage: python tools/r4_perf_report.py <log_stable.log> [--windows]

Reads the recorder's 2 s report windows ([R4 E1.7 status/cpu/cpu_phase/gpu],
[R4 E1.3 work], [R4 E1.7 details], [R4 P0-S11 queue]) and prints the session
means, the slowest windows and, with --windows, every window. Windows without
FPS (menu, pause) are left out of the means. Times are CPU wall clock per
frame unless named GPU; nested stages are not additive.
"""
import re
import sys
from statistics import mean

NUMBER = re.compile(r'([A-Za-z_0-9]+)=(-?[0-9.]+)(?:/(-?[0-9.]+))?')


def fields(line):
    out = {}
    for key, value, peak in NUMBER.findall(line):
        try:
            out[key] = float(value)
            if peak:
                out[key + '_max'] = float(peak)
        except ValueError:
            pass
    return out


def windows(path):
    rows, current = [], None
    with open(path, encoding='cp1251', errors='replace') as log:
        for line in log:
            if '[R4 E1.7 status]' in line:
                if current:
                    rows.append(current)
                f = fields(line)
                current = {'frame': f.get('frame', 0), 'fps': f.get('fps_recent', 0)}
            elif current is None:
                continue
            elif '[R4 E1.7 cpu]' in line:
                f = fields(line)
                current.update(cycle=f.get('cycle_mean_ms', 0), cycle_max=f.get('cycle_max_ms', 0), slow33=f.get('frames_ge_33_333ms', 0))
            elif '[R4 E1.7 cpu_phase]' in line:
                name = re.search(r'name=(\w+)', line).group(1)
                f = fields(line)
                current['phase_' + name] = f.get('mean_ms', 0)
                current['phase_' + name + '_max'] = f.get('max_ms', 0)
            elif '[R4 E1.7 frame]' in line:
                current['low1'] = fields(line).get('present_interval_1pct_low_fps', 0)
            elif '[R4 E1.3 work]' in line:
                f = fields(line)
                current.update(plan=f.get('frame_plan_ms', 0), preflight=f.get('texture_preflight_ms', 0))
            elif '[R4 E1.7 details]' in line:
                f = fields(line)
                current.update(detail_bounds=f.get('bounds_ms', 0), sun_select=f.get('sun_select_ms', 0), local_select=f.get('local_select_ms', 0))
            elif '[R4 P0-S11 queue]' in line:
                for key, value in fields(line).items():
                    current['q_' + key] = value
            elif '[R4 P0-S11 submit]' in line:
                for key, value in fields(line).items():
                    current['s_' + key] = value
            elif '[R4 E1.7 gpu]' in line and 'gpu_shadow' not in current:
                f = fields(line)
                current.update(gpu_shadow=f.get('resource_shadow_ms', 0), gpu_gbuffer=f.get('gbuffer_ms', 0),
                               gpu_lighting=f.get('lighting_ms', 0), gpu_combine=f.get('combine_ms', 0), gpu_forward=f.get('forward_ms', 0))
    if current:
        rows.append(current)
    return rows


COLUMNS = [('fps', 'fps'), ('low1', '1%low'), ('cycle', 'cycle'), ('cycle_max', 'cyc_max'), ('phase_frame_move', 'move'),
           ('phase_native_submit', 'submit'), ('q_details_ms', 'details'), ('q_detail_sync_ms', 'd_sync'), ('q_detail_prepare_ms', 'd_prep'),
           ('q_submit_ms', 'q_subm'), ('q_renderables_ms', 'q_rend'), ('preflight', 'preflt'), ('plan', 'plan'), ('sun_select', 'sun_sel'),
           ('detail_bounds', 'd_bnds'), ('gpu_shadow', 'g_shad'), ('gpu_gbuffer', 'g_gbuf'), ('gpu_lighting', 'g_lit')]
# P0-S11 [R4 P0-S11 submit]: consecutive CPU stages of the native submit.
SUBMIT = [('s_setup_ms', 'setup'), ('s_sun_128m_ms', 'sun128'), ('s_sun_512m_ms', 'sun512'), ('s_sun_contact_ms', 'contact'),
          ('s_local_shadows_ms', 'local'), ('s_gbuffer_ms', 'gbuffer'), ('s_lighting_ms', 'light'), ('s_combine_ms', 'combine'),
          ('s_forward_ms', 'forward'), ('s_post_hud_ms', 'post'), ('s_other_ms', 'other'), ('s_sun_512m_reused', 'reused'),
          ('s_local_faces', 'faces')]


def show(rows, title):
    print(title)
    print('  frame ' + ' '.join('%7s' % label for _, label in COLUMNS))
    for r in rows:
        print('  %5d ' % r.get('frame', 0) + ' '.join('%7.2f' % r.get(key, 0) for key, _ in COLUMNS))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    rows = [r for r in windows(sys.argv[1]) if r.get('fps', 0) > 0]
    if not rows:
        sys.exit('no recorder windows with FPS (was log_record on?)')
    print('windows=%d frames~%d' % (len(rows), sum(r.get('fps', 0) * 2 for r in rows)))
    print('session means (per frame, ms; GPU from one sample per window):')
    for key, label in COLUMNS:
        values = [r[key] for r in rows if key in r]
        if values:
            print('  %-8s mean=%8.3f min=%8.3f max=%8.3f' % (label, mean(values), min(values), max(values)))
    submit = [(key, label) for key, label in SUBMIT if any(key in r for r in rows)]
    if submit:
        print('native submit stages (per frame, ms; sun512 includes reused frames; reused/faces are counts):')
        for key, label in submit:
            values = [r[key] for r in rows if key in r]
            print('  %-8s mean=%8.3f min=%8.3f max=%8.3f' % (label, mean(values), min(values), max(values)))
    show(sorted(rows, key=lambda r: r.get('cycle', 0), reverse=True)[:10], 'slowest windows by mean cycle:')
    if '--windows' in sys.argv:
        show(rows, 'all windows:')


if __name__ == '__main__':
    main()
