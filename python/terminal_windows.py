#!/usr/bin/env python3
"""
terminal_windows.py - Keyboard timing via ctypes SetWindowsHookEx (Windows)

Captures global key events using a low-level keyboard hook.
No special permissions needed.

Usage: python terminal_windows.py [output.csv]
       Press Ctrl+C to stop and save.
"""

import sys
import os
import time
import signal
import ctypes
import ctypes.wintypes
import platform
from datetime import datetime, timezone

DEFAULT_OUTPUT = "output/python_terminal_windows.csv"

# Windows constants
WH_KEYBOARD_LL = 13
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_SYSKEYDOWN = 0x0104
WM_SYSKEYUP = 0x0105
HC_ACTION = 0

# Virtual key codes for common keys
VK_NAMES = {
    0x08: "backspace", 0x09: "tab", 0x0D: "return", 0x1B: "escape",
    0x20: "space", 0x25: "left", 0x26: "up", 0x27: "right", 0x28: "down",
    0x2D: "insert", 0x2E: "delete", 0x21: "pageup", 0x22: "pagedown",
    0x23: "end", 0x24: "home",
    0xA0: "shift_l", 0xA1: "shift_r",
    0xA2: "ctrl_l", 0xA3: "ctrl_r",
    0xA4: "alt_l", 0xA5: "alt_r",
    0x5B: "win_l", 0x5C: "win_r",
    0x14: "capslock",
}


class KBDLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [
        ("vkCode", ctypes.wintypes.DWORD),
        ("scanCode", ctypes.wintypes.DWORD),
        ("flags", ctypes.wintypes.DWORD),
        ("time", ctypes.wintypes.DWORD),
        ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong)),
    ]


# Callback type
HOOKPROC = ctypes.CFUNCTYPE(
    ctypes.c_long,
    ctypes.c_int,
    ctypes.wintypes.WPARAM,
    ctypes.wintypes.LPARAM,
)

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32


def vk_to_char(vk, scancode):
    """Convert virtual key code to character string."""
    if vk in VK_NAMES:
        return VK_NAMES[vk]
    # Printable ASCII range
    if 0x30 <= vk <= 0x39:  # 0-9
        return chr(vk)
    if 0x41 <= vk <= 0x5A:  # A-Z
        return chr(vk).lower()
    return f"vk_0x{vk:02x}"


def get_modifiers():
    """Get current modifier key state."""
    parts = []
    if user32.GetAsyncKeyState(0x10) & 0x8000:  # VK_SHIFT
        parts.append("shift")
    if user32.GetAsyncKeyState(0x11) & 0x8000:  # VK_CONTROL
        parts.append("ctrl")
    if user32.GetAsyncKeyState(0x12) & 0x8000:  # VK_MENU (Alt)
        parts.append("alt")
    return "+".join(parts) if parts else "none"


class KeyboardRecorder:
    def __init__(self, output_path):
        self.output_path = output_path
        self.events = []
        self.start_ns = time.perf_counter_ns()
        self.running = True
        self.hook = None

    def _timestamp_ms(self):
        return (time.perf_counter_ns() - self.start_ns) / 1e6

    def _hook_callback(self, nCode, wParam, lParam):
        if nCode < 0:
            return user32.CallNextHookEx(self.hook, nCode, wParam, lParam)

        kb = ctypes.cast(lParam, ctypes.POINTER(KBDLLHOOKSTRUCT)).contents
        ts_ms = self._timestamp_ms()
        event_ts_ms = float(kb.time)

        if wParam in (WM_KEYDOWN, WM_SYSKEYDOWN):
            event_type_str = "key_down"
        elif wParam in (WM_KEYUP, WM_SYSKEYUP):
            event_type_str = "key_up"
        else:
            return user32.CallNextHookEx(self.hook, nCode, wParam, lParam)

        record = {
            "seq": len(self.events) + 1,
            "timestamp_ms": ts_ms,
            "event_timestamp_ms": event_ts_ms,
            "event_type": event_type_str,
            "keycode": kb.vkCode,
            "scancode": kb.scanCode,
            "character": vk_to_char(kb.vkCode, kb.scanCode),
            "modifiers": get_modifiers(),
            "is_repeat": 0,
        }
        self.events.append(record)

        print(
            f"\r[{record['seq']}] {event_type_str} {record['character']} "
            f"(vk=0x{kb.vkCode:02x} sc={kb.scanCode}) t={ts_ms:.3f}ms",
            end="", file=sys.stderr, flush=True,
        )

        return user32.CallNextHookEx(self.hook, nCode, wParam, lParam)

    def write_csv(self):
        os.makedirs(os.path.dirname(self.output_path) or ".", exist_ok=True)
        with open(self.output_path, "w") as f:
            plat = platform.platform()
            utc_now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")
            f.write(f"# platform={plat}\n")
            f.write("# language=python\n")
            f.write("# mode=terminal\n")
            f.write("# clock_source=time.perf_counter_ns\n")
            f.write(f"# start_time_utc={utc_now}\n")

            f.write("seq,timestamp_ms,event_timestamp_ms,event_type,keycode,scancode,character,modifiers,is_repeat\n")

            for e in self.events:
                f.write(
                    f"{e['seq']},{e['timestamp_ms']:.3f},{e['event_timestamp_ms']:.3f},"
                    f"{e['event_type']},{e['keycode']},{e['scancode']},"
                    f"{e['character']},{e['modifiers']},{e['is_repeat']}\n"
                )

        print(f"\nWrote {len(self.events)} events to {self.output_path}", file=sys.stderr)

    def run(self):
        # Keep a reference to the callback to prevent garbage collection
        callback = HOOKPROC(self._hook_callback)

        self.hook = user32.SetWindowsHookExA(
            WH_KEYBOARD_LL, callback, kernel32.GetModuleHandleW(None), 0
        )
        if not self.hook:
            print(
                f"Error: Failed to set keyboard hook (error {kernel32.GetLastError()})",
                file=sys.stderr,
            )
            sys.exit(1)

        print("Keyboard timing (Python/terminal/Windows) - Press keys, Ctrl+C to stop", file=sys.stderr)
        print(f"Output: {self.output_path}", file=sys.stderr)

        def handle_signal(sig, frame):
            self.running = False

        signal.signal(signal.SIGINT, handle_signal)

        msg = ctypes.wintypes.MSG()
        while self.running:
            result = user32.GetMessageA(ctypes.byref(msg), None, 0, 0)
            if result <= 0:
                break
            user32.TranslateMessage(ctypes.byref(msg))
            user32.DispatchMessageA(ctypes.byref(msg))

        user32.UnhookWindowsHookEx(self.hook)
        self.write_csv()


def main():
    output_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_OUTPUT
    recorder = KeyboardRecorder(output_path)
    recorder.run()


if __name__ == "__main__":
    main()
