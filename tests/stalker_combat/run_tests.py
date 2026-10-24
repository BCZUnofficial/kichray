#!/usr/bin/env python3
"""Run CPU-only stalker regression checks; this does not build or launch xrGame."""
from pathlib import Path
import argparse
import subprocess
import sys

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", help="also verify the change stays inside the stalker AI scope")
    args = parser.parse_args()
    for name in ("memory", "cover", "cover_integration", "current_cover", "action_recovery", "fire", "awareness", "search_listen_animation", "search_progress", "bullet_near_miss", "near_miss", "sound_reaction", "diagnostics", "lua"):
        runner = HERE / f"run_{name}_tests.py"
        subprocess.run([sys.executable, str(runner)], check=True)
    if args.base:
        subprocess.run([sys.executable, str(HERE / "check_scope.py"), "--base", args.base], check=True)
    print("PASS: stalker combat regression suite (Windows build and in-game scenarios remain separate)")


if __name__ == "__main__":
    main()
