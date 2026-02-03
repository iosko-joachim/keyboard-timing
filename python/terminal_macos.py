#!/usr/bin/env python3
"""
terminal_macos.py - Keyboard timing via pyobjc CGEventTap (macOS)

Captures global key events using Quartz CGEventTap.
Requires Accessibility permissions in System Settings.

Usage: python terminal_macos.py [output.csv]
       Press Ctrl+C to stop and save.
"""

import sys
import os
import time
import signal
import platform
from datetime import datetime, timezone

from Quartz import (
    CGEventTapCreate,
    CGEventTapEnable,
    CGEventGetTimestamp,
    CGEventGetIntegerValueField,
    CGEventGetFlags,
    CFMachPortCreateRunLoopSource,
    CFRunLoopGetCurrent,
    CFRunLoopAddSource,
    CFRunLoopRunInMode,
    CFRunLoopStop,
    kCGSessionEventTap,
    kCGHeadInsertEventTap,
    kCGEventTapOptionListenOnly,
    kCGEventKeyDown,
    kCGEventKeyUp,
    kCGEventFlagsChanged,
    kCFRunLoopCommonModes,
    kCFRunLoopDefaultMode,
    kCGKeyboardEventAutorepeat,
    kCGKeyboardEventKeycode,
    kCGEventFlagMaskShift,
    kCGEventFlagMaskControl,
    kCGEventFlagMaskAlternate,
    kCGEventFlagMaskCommand,
)

DEFAULT_OUTPUT = "output/python_terminal_macos.csv"

# Apple Virtual Keycode to character mapping (US layout)
KEYCODE_MAP = {
    0x00: "a", 0x01: "s", 0x02: "d", 0x03: "f", 0x04: "h",
    0x05: "g", 0x06: "z", 0x07: "x", 0x08: "c", 0x09: "v",
    0x0B: "b", 0x0C: "q", 0x0D: "w", 0x0E: "e", 0x0F: "r",
    0x10: "y", 0x11: "t", 0x12: "1", 0x13: "2", 0x14: "3",
    0x15: "4", 0x16: "6", 0x17: "5", 0x18: "9", 0x19: "7",
    0x1A: "8", 0x1B: "0", 0x1C: "o", 0x1D: "u", 0x1E: "i",
    0x1F: "p", 0x20: "l", 0x21: "j", 0x22: "k", 0x23: "n",
    0x24: "m",
    0x24: "return", 0x30: "tab", 0x31: "space", 0x33: "backspace",
    0x35: "escape",
    # Modifier keycodes
    0x37: "cmd_l", 0x36: "cmd_r",
    0x38: "shift_l", 0x3C: "shift_r",
    0x3A: "alt_l", 0x3D: "alt_r",
    0x3B: "ctrl_l", 0x3E: "ctrl_r",
    0x39: "capslock",
}


class KeyboardRecorder:
    def __init__(self, output_path):
        self.output_path = output_path
        self.events = []
        self.start_ns = time.perf_counter_ns()
        self.running = True
        self.modifier_key_down = {}
        self.run_loop = None

    def _timestamp_ms(self):
        return (time.perf_counter_ns() - self.start_ns) / 1e6

    @staticmethod
    def _modifier_string(flags):
        parts = []
        if flags & kCGEventFlagMaskShift:
            parts.append("shift")
        if flags & kCGEventFlagMaskControl:
            parts.append("ctrl")
        if flags & kCGEventFlagMaskAlternate:
            parts.append("alt")
        if flags & kCGEventFlagMaskCommand:
            parts.append("cmd")
        return "+".join(parts) if parts else "none"

    @staticmethod
    def _keycode_to_char(keycode):
        return KEYCODE_MAP.get(keycode, f"0x{keycode:02x}")

    def _event_callback(self, proxy, event_type, event, refcon):
        ts_ms = self._timestamp_ms()
        event_ts_ns = CGEventGetTimestamp(event)
        event_ts_ms = event_ts_ns / 1e6

        keycode = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode)
        flags = CGEventGetFlags(event)
        autorepeat = CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat)

        if event_type == kCGEventKeyDown:
            event_type_str = "key_down"
        elif event_type == kCGEventKeyUp:
            event_type_str = "key_up"
        elif event_type == kCGEventFlagsChanged:
            if self.modifier_key_down.get(keycode, False):
                event_type_str = "key_up"
                self.modifier_key_down[keycode] = False
            else:
                event_type_str = "key_down"
                self.modifier_key_down[keycode] = True
        else:
            return event

        record = {
            "seq": len(self.events) + 1,
            "timestamp_ms": ts_ms,
            "event_timestamp_ms": event_ts_ms,
            "event_type": event_type_str,
            "keycode": keycode,
            "scancode": 0,
            "character": self._keycode_to_char(keycode),
            "modifiers": self._modifier_string(flags),
            "is_repeat": int(autorepeat),
        }
        self.events.append(record)

        print(
            f"\r[{record['seq']}] {event_type_str} {record['character']} "
            f"(keycode={keycode}) t={ts_ms:.3f}ms",
            end="", file=sys.stderr, flush=True,
        )

        return event

    def write_csv(self):
        os.makedirs(os.path.dirname(self.output_path) or ".", exist_ok=True)
        with open(self.output_path, "w") as f:
            # Metadata
            plat = platform.platform()
            utc_now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")
            f.write(f"# platform={plat}\n")
            f.write("# language=python\n")
            f.write("# mode=terminal\n")
            f.write("# clock_source=time.perf_counter_ns\n")
            f.write(f"# start_time_utc={utc_now}\n")

            # Header
            f.write("seq,timestamp_ms,event_timestamp_ms,event_type,keycode,scancode,character,modifiers,is_repeat\n")

            for e in self.events:
                f.write(
                    f"{e['seq']},{e['timestamp_ms']:.3f},{e['event_timestamp_ms']:.3f},"
                    f"{e['event_type']},{e['keycode']},{e['scancode']},"
                    f"{e['character']},{e['modifiers']},{e['is_repeat']}\n"
                )

        print(f"\nWrote {len(self.events)} events to {self.output_path}", file=sys.stderr)

    def run(self):
        mask = (1 << kCGEventKeyDown) | (1 << kCGEventKeyUp) | (1 << kCGEventFlagsChanged)

        tap = CGEventTapCreate(
            kCGSessionEventTap,
            kCGHeadInsertEventTap,
            kCGEventTapOptionListenOnly,
            mask,
            self._event_callback,
            None,
        )

        if tap is None:
            print(
                "Error: Failed to create event tap.\n"
                "Grant Accessibility permission in System Settings > Privacy & Security.",
                file=sys.stderr,
            )
            sys.exit(1)

        source = CFMachPortCreateRunLoopSource(None, tap, 0)
        self.run_loop = CFRunLoopGetCurrent()
        CFRunLoopAddSource(self.run_loop, source, kCFRunLoopCommonModes)
        CGEventTapEnable(tap, True)

        print("Keyboard timing (Python/terminal/macOS) - Press keys, Ctrl+C to stop", file=sys.stderr)
        print(f"Output: {self.output_path}", file=sys.stderr)

        def handle_signal(sig, frame):
            self.running = False
            if self.run_loop:
                CFRunLoopStop(self.run_loop)

        signal.signal(signal.SIGINT, handle_signal)
        signal.signal(signal.SIGTERM, handle_signal)

        while self.running:
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, True)

        self.write_csv()


def main():
    output_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_OUTPUT
    recorder = KeyboardRecorder(output_path)
    recorder.run()


if __name__ == "__main__":
    main()
