#!/usr/bin/env python3
"""
build_exe.py - Build standalone .exe for Windows using PyInstaller.

Must be run on Windows (or in a Windows CI environment).
Requires: pip install pyinstaller

Usage: python build_exe.py
"""

import subprocess
import sys
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
SPEC_FILE = os.path.join(SCRIPT_DIR, "keyboard_timer.spec")
GUI_SCRIPT = os.path.join(PROJECT_ROOT, "python", "gui_windows.py")


def main():
    if sys.platform != "win32":
        print("Error: This script must be run on Windows.", file=sys.stderr)
        print("Use 'make -C c/ windows' for cross-compiling C targets on macOS.", file=sys.stderr)
        sys.exit(1)

    try:
        import PyInstaller  # noqa: F401
    except ImportError:
        print("PyInstaller not found. Installing...", file=sys.stderr)
        subprocess.check_call([sys.executable, "-m", "pip", "install", "pyinstaller"])

    if not os.path.isfile(GUI_SCRIPT):
        print(f"Error: {GUI_SCRIPT} not found.", file=sys.stderr)
        sys.exit(1)

    dist_dir = os.path.join(SCRIPT_DIR, "dist")
    build_dir = os.path.join(SCRIPT_DIR, "build")

    cmd = [
        sys.executable, "-m", "PyInstaller",
        "--distpath", dist_dir,
        "--workpath", build_dir,
        SPEC_FILE,
    ]

    print(f"Building with: {' '.join(cmd)}", file=sys.stderr)
    subprocess.check_call(cmd)

    exe_path = os.path.join(dist_dir, "python_gui_windows.exe")
    if os.path.isfile(exe_path):
        print(f"Success: {exe_path}", file=sys.stderr)
    else:
        print("Warning: .exe not found at expected path.", file=sys.stderr)


if __name__ == "__main__":
    main()
