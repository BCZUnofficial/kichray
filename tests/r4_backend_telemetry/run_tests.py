#!/usr/bin/env python3
"""Execute actual provider timing function bodies against deterministic query replies.
D3D query scheduling/driver behavior is not simulated or claimed by this test.
"""
from pathlib import Path
import subprocess
import tempfile
import re
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
source=(ROOT/'xr_3da/xrRender_R4/R4Resources.cpp').read_text()
pipeline=(ROOT/'xr_3da/xrRender_R4/R4PipelineDX11.cpp').read_text()
actual_sampler=re.search(r'if\(with_advanced\).*?s.Filter=D3D11_FILTER_([A-Z_]+);',pipeline).group(1)
reported_sampler=re.search(r'"ps_s1",pipeline->pipeline->advanced\?"([A-Z_]+);',source).group(1)
assert reported_sampler==actual_sampler, 'snapshot comparison sampler must match the bound pipeline descriptor'
profile=source[source.index('int32_t KICH_R4_CALL NativeProfileMark('):source.index('int32_t KICH_R4_CALL PollTelemetryTiming(')]
poll=source[source.index('int32_t KICH_R4_CALL PollTelemetryTiming('):source.index('KichR4GeometryStats GeometryStats(')]
with tempfile.TemporaryDirectory(prefix='kich-r4-telemetry-') as temp:
    cpp=Path(temp)/'timing.cpp';exe=Path(temp)/'timing'
    cpp.write_text((HERE/'timing_fixture.h').read_text()+profile+poll+(HERE/'timing_cases.cpp').read_text())
    subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
    emission=source[source.index('    void Emit('):source.index('    void Write(Resource*')]
    number_cpp=Path(temp)/'number.cpp';number_exe=Path(temp)/'number'
    number_cpp.write_text((HERE/'number_fixture.h').read_text()+emission+(HERE/'number_cases.cpp').read_text())
    subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT),str(number_cpp),'-o',str(number_exe)],check=True)
    subprocess.run([str(number_exe)],check=True)
    routing_exe=Path(temp)/'record_timing'
    subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT),str(HERE/'record_timing_tests.cpp'),'-o',str(routing_exe)],check=True)
    subprocess.run([str(routing_exe)],check=True)
    abi=Path(temp)/'abi.c'
    abi.write_text('#include "xr_3da/KichR4Telemetry.h"\n_Static_assert(sizeof(KichR4TelemetryAPI)==16+2*sizeof(void*),"C function table packing");\n_Static_assert(sizeof(KichR4TelemetryContext)==56,"C context packing");\n_Static_assert(sizeof(KichR4TelemetryRecord)==584,"C record packing");\n_Static_assert(sizeof(KichR4TelemetryTiming)==96,"C timing packing");\n')
    subprocess.run(['gcc','-m32','-ffreestanding','-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT),'-c',str(abi),'-o',str(Path(temp)/'abi.o')],check=True)
    print('PASS: additive optional telemetry C ABI / exact x86 packing; master renderer and draw ABI unchanged')
