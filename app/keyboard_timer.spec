# -*- mode: python ; coding: utf-8 -*-
"""
PyInstaller spec for python_gui_windows.exe

Run via: python build_exe.py
Or directly: pyinstaller keyboard_timer.spec
"""

import os

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(SPECPATH)))

a = Analysis(
    [os.path.join(PROJECT_ROOT, 'python', 'gui_windows.py')],
    pathex=[os.path.join(PROJECT_ROOT, 'python')],
    binaries=[],
    datas=[],
    hiddenimports=[],
    hookspath=[],
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
)

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='python_gui_windows',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,  # GUI app, no console window
    icon=None,
)
