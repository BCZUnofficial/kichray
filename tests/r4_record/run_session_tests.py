#!/usr/bin/env python3
"""Exercise production bounded recording session, including Win32 x87 precision."""
from pathlib import Path
import os
import platform
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent

def main():
    variants=[('native',[])]
    if platform.system()=='Linux' and platform.machine().lower() in ('x86_64','amd64','i386','i686'):
        variants.append(('x87-pc24',['-mfpmath=387','-DKICH_TEST_X87']))
    with tempfile.TemporaryDirectory(prefix='kich-e17-session-') as folder:
        for name,flags in variants:
            output=Path(folder)/name
            subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-O2','-Wall','-Wextra','-Werror',*flags,
                str(HERE/'session_tests.cpp'),'-o',str(output)],check=True)
            subprocess.run([str(output)],check=True)

if __name__=='__main__':main()
