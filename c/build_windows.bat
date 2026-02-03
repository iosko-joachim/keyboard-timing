@echo off
REM Build script for Windows keyboard timing programs (MSVC)
REM Run from a "Developer Command Prompt for VS" or after calling vcvarsall.bat

if not exist "..\output" mkdir "..\output"

echo Building terminal_windows.exe...
cl /O2 /W4 /Fe:terminal_windows.exe terminal_windows.c user32.lib kernel32.lib

echo Building gui_windows.exe...
cl /O2 /W4 /Fe:gui_windows.exe gui_windows.c user32.lib kernel32.lib gdi32.lib

echo Done.
