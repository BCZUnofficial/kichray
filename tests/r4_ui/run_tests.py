#!/usr/bin/env python3
"""Compile actual UI draw functions/helper and input dispatch/commands against fakes.
No MSVC full-engine, real game asset, or GPU validation is claimed.
"""
from pathlib import Path
import os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent

def function(path,signature):
    source=(ROOT/path).read_bytes().decode('cp1251')
    start=source.index(signature);brace=source.index('{',start);depth=1;end=brace+1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[start:end]
with tempfile.TemporaryDirectory(prefix='kich-ui-') as tmp:
    work=Path(tmp)
    helper=(ROOT/'xr_3da/xrGame/ui/RenderSnapshotUI.h').read_text().replace('#include "UIWindow.h"','#include "ui_fake.h"').replace('#include "../../RenderSnapshot.h"','#include "RenderSnapshot.h"')
    (work/'RenderSnapshotUI.h').write_text(helper)
    source='#include "ui_fake.h"\n#include "RenderSnapshotUI.h"\n'
    source+=function('xr_3da/xrGame/ui/UIWindow.cpp','void CUIWindow::Draw()')+'\n'
    source+=function('xr_3da/xrGame/ui/UIStatic.cpp','void CUIStatic::Draw()')+'\n'
    command=(ROOT/'xr_3da/xrRender/xrRender_console.cpp').read_bytes().decode('cp1251')
    source+=command[command.index('class CCC_FrameSnapshot'):command.index('void xrRender_initconsole()')]+'\n'
    key=(ROOT/'xr_3da/Xr_input.cpp').read_bytes().decode('cp1251')
    dispatch=key[key.index('\tfor (u32 i = 0; i < dwElements; i++)'):key.index('\tfor (u32 i = 0; i < COUNT_KB_BUTTONS; i++)')]
    source+='typedef unsigned u32;\nvoid Dispatch(KeyEvent* od,u32 dwElements,unsigned char* KBState,std::vector<Receiver*>& cbStack){unsigned key=0;\n'+dispatch+'}\n'
    source+=(HERE/'ui_tests.cpp').read_text()
    (work/'actual_ui.cpp').write_text(source)
    binary=work/'ui_tests'
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-O2','-Wall','-Wextra','-Werror','-I'+str(HERE),'-I'+str(work),'-I'+str(ROOT/'xr_3da'),str(work/'actual_ui.cpp'),str(ROOT/'xr_3da/RenderSnapshot.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary),str(work)],check=True)
