#!/usr/bin/env python3
"""Compile real old/new lifecycle bodies with fake engine services; compare traces.

Requires Python 3, Git history containing BASE, and a native g++ compiler.
No files are installed or modified in the project; build outputs use a temp dir.
These checks do not execute a graphics driver or replace an MSVC game test.
"""
from pathlib import Path
import difflib
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TEST = Path(__file__).resolve().parent
BASE = "6d832035330a004a2ffabce8d124f17409dcc563"
TARGETS = {
    "xr_3da/Device_create.cpp": ["_SetupStates", "_Create", "Create"],
    "xr_3da/Device_destroy.cpp": ["_Destroy", "Destroy", "Reset"],
    "xr_3da/Device_overdraw.cpp": ["overdrawBegin", "overdrawEnd"],
    "xr_3da/device.cpp": ["Begin", "Clear", "End", "PreCache"],
}


def method(source, name):
    match = re.search(r"(?:void|BOOL) CRenderDevice::" + re.escape(name) + r"\([^)]*\)\s*\{", source)
    if not match:
        raise RuntimeError(f"Missing production method: {name}")
    # Lexical brace scan: ignore braces in comments and string/character literals.
    i = source.index("{", match.start())
    depth, state = 0, "code"
    while i < len(source):
        ch, nxt = source[i], source[i:i+2]
        if state == "line":
            if ch == "\n": state = "code"
        elif state == "block":
            if nxt == "*/": state = "code"; i += 1
        elif state in ('"', "'"):
            if ch == "\\": i += 1
            elif ch == state: state = "code"
        elif nxt == "//": state = "line"; i += 1
        elif nxt == "/*": state = "block"; i += 1
        elif ch in ('"', "'"): state = ch
        elif ch == "{": depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():i+1]
        i += 1
    raise RuntimeError(f"Unbalanced method: {name}")


def unit(old):
    bodies = []
    for path, names in TARGETS.items():
        content = subprocess.check_output(["git", "show", BASE+":"+path], cwd=ROOT).decode() if old else (ROOT/path).read_text()
        bodies.extend(method(content, name) for name in names)
    # Snapshot internals have their own production tests. This seam isolates
    # and compares device/GPU ownership traces with capture disabled.
    return '#include "fake_engine.h"\ninline void RenderSnapshot_EngineBegin(){}\ninline void RenderSnapshot_EngineEnd(){}\ninline void RenderSnapshot_EngineDeviceEvent(const char*){}\ninline void RenderSnapshot_Abort(const char*){}\n' + ("" if old else '#include "'+str(ROOT/'xr_3da/KichRenderDevice_DX9.cpp')+'"\n') + "\n\n".join(bodies)


def main():
    compiler = os.environ.get("CXX", "g++")
    configurations = [([], "Release"), (["DEBUG"], "Debug"),
                      (["DEDICATED_SERVER"], "Dedicated"),
                      (["DEBUG", "DEDICATED_SERVER"], "Debug Dedicated"),
                      (["_EDITOR"], "Editor guards")]
    with tempfile.TemporaryDirectory(prefix="kich-b1-tests-") as folder:
        work = Path(folder)
        for flags, label in configurations:
            outputs = []
            for old in (True, False):
                name = "old" if old else "b1"
                src, exe = work/(name+".cpp"), work/name
                src.write_text(unit(old))
                cmd = [compiler, "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                       "-Wno-unknown-pragmas", "-Wno-strict-aliasing", "-I"+str(TEST), "-I"+str(TEST/'fakes')]
                cmd.extend("-D"+f for f in flags)
                if not old: cmd.append("-DTEST_B1")
                cmd.extend([str(src), str(TEST/'scenarios.cpp'), "-o", str(exe)])
                subprocess.run(cmd, check=True)
                result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
                outputs.append(result.stdout)
                print(label, name, result.stderr.strip())
            if outputs[0] != outputs[1]:
                print("".join(difflib.unified_diff(outputs[0].splitlines(True), outputs[1].splitlines(True), fromfile="legacy", tofile="B1")))
                raise SystemExit(f"Lifecycle call order changed: {label}")
            print(label, "PASS: all 26 lifecycle traces match the legacy engine")
    print("PASS: 130 old/new lifecycle traces compared across 5 configurations")


if __name__ == "__main__":
    main()
