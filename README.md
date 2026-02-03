# keyboard-timing

Cross-platform keyboard event timing with high-precision timestamps. Records key press/release events with sub-millisecond accuracy for typing analysis, latency measurement, and input research.

## Implementations

|                | macOS                  | Windows                  |
|----------------|------------------------|--------------------------|
| **C GUI**      | `c/gui_macos.m`        | `c/gui_windows.c`        |
| **C Terminal** | `c/terminal_macos.c`   | `c/terminal_windows.c`   |
| **Python GUI** | `python/gui_macos.py`  | `python/gui_windows.py`  |
| **Python Terminal** | `python/terminal_macos.py` | `python/terminal_windows.py` |

- **GUI** variants open a window and capture key events within it. No special permissions needed.
- **Terminal** variants use global keyboard hooks (system-wide capture). macOS requires Accessibility permissions.

## Clock Sources

| Platform | Language | Clock Source                |
|----------|----------|----------------------------|
| macOS    | C        | `mach_absolute_time`       |
| Windows  | C        | `QueryPerformanceCounter`  |
| Both     | Python   | `time.perf_counter_ns`     |

## Build

### macOS (native)

```sh
make -C c/
```

Builds `c/terminal_macos` and `c/gui_macos`.

### Windows (cross-compile on macOS)

```sh
brew install mingw-w64
make -C c/ windows
```

Builds `c/terminal_windows.exe` (console) and `c/gui_windows.exe` (GUI, no console window).

### Windows (native, MSVC)

From a Developer Command Prompt for Visual Studio:

```bat
cd c
build_windows.bat
```

### Python

```sh
python -m venv venv
source venv/bin/activate        # macOS/Linux
pip install -r requirements.txt # only needed for terminal_macos.py (pyobjc)
python python/gui_macos.py
```

### Standalone Windows .exe (PyInstaller)

On a Windows machine:

```sh
pip install pyinstaller
python app/build_exe.py
```

Produces `app/dist/python_gui_windows.exe`.

## Usage

All variants follow the same pattern:

```sh
# C
./c/gui_macos [output.csv]
./c/terminal_macos [output.csv]

# Python
python python/gui_macos.py [output.csv]
python python/terminal_macos.py [output.csv]
```

- Press keys to record timing events.
- Press **Escape** (GUI) or **Ctrl+C** (Terminal) to stop and save.
- Default output goes to `output/`.

## Output Format

CSV with metadata headers:

```
# platform=macOS-26.2-arm64
# language=c
# mode=gui
# clock_source=mach_absolute_time
# start_time_utc=2026-02-01T14:00:28.000000Z
seq,timestamp_ms,event_timestamp_ms,event_type,keycode,scancode,character,modifiers,is_repeat
1,6713.312,2384935612.885,key_down,126,0,0x7e,none,0
2,6814.699,2384935724.896,key_up,126,0,0x7e,none,0
```

## Project Structure

```
c/                  C implementations + Makefile
python/             Python implementations
app/                PyInstaller build scripts
output/             CSV output (gitignored)
requirements.txt    Python dependencies
```

## License

MIT
