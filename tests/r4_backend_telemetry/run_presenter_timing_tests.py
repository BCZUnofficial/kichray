#!/usr/bin/env python3
"""Execute actual EXE timing dispatch with deterministic API replies, no GPU."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
source=(ROOT/'xr_3da/KichR4NativePresenter.cpp').read_text()
bodies=source[source.index('void ForgetTiming('):source.index('struct TelemetryGuard {')]
deferred=(ROOT/'xr_3da/KichR4DeferredSession.h').read_text()
mark=next(line.strip() for line in deferred.splitlines() if 'void Mark(KichR4FrameSession&' in line)
fixture=(HERE/'presenter_timing_fixture.h').read_text().replace('// ACTUAL_DEFERRED_PROFILE_MARK',mark)
with tempfile.TemporaryDirectory(prefix='kich-r4-presenter-timing-') as temp:
    cpp=Path(temp)/'presenter.cpp';exe=Path(temp)/'presenter'
    cpp.write_text(fixture+bodies+(HERE/'presenter_timing_cases.cpp').read_text())
    subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-Wno-unused-function','-I'+str(ROOT),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
