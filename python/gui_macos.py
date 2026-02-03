#!/usr/bin/env python3
"""
gui_macos.py - Keyboard timing via Tkinter (macOS)

Opens a window and captures key events via Tkinter bindings.
No special permissions needed.

Usage: python gui_macos.py [output.csv]
       Press Escape to stop and save.
"""

import sys
import os
import time
import platform
import tkinter as tk
from datetime import datetime, timezone

DEFAULT_OUTPUT = "output/python_gui_macos.csv"

# Apple Virtual Keycode mapping (subset - Tk provides keysym strings too)
KEYCODE_MAP = {
    0x00: "a", 0x01: "s", 0x02: "d", 0x03: "f", 0x04: "h",
    0x05: "g", 0x06: "z", 0x07: "x", 0x08: "c", 0x09: "v",
    0x0B: "b", 0x0C: "q", 0x0D: "w", 0x0E: "e", 0x0F: "r",
    0x10: "y", 0x11: "t", 0x12: "1", 0x13: "2", 0x14: "3",
    0x15: "4", 0x16: "6", 0x17: "5", 0x18: "9", 0x19: "7",
    0x1A: "8", 0x1B: "0", 0x1C: "o", 0x1D: "u", 0x1E: "i",
    0x1F: "p", 0x20: "l", 0x21: "j", 0x22: "k", 0x23: "n",
    0x24: "return", 0x26: "m", 0x30: "tab", 0x31: "space",
    0x33: "backspace", 0x35: "escape",
}


class KeyboardTimerGUI:
    def __init__(self, output_path):
        self.output_path = output_path
        self.events = []
        self.start_ns = time.perf_counter_ns()
        self.keys_down = set()  # Track held keys for repeat detection

        self.root = tk.Tk()
        self.root.title("Keyboard Timing - Python/GUI/macOS")
        self.root.geometry("500x300")

        self.label = tk.Label(
            self.root,
            text=(
                "Keyboard Timing (Python/GUI/macOS)\n\n"
                "Press keys to record timing.\n"
                "Press Escape to stop and save.\n\n"
                "Events: 0"
            ),
            font=("Helvetica", 14),
            justify="left",
            anchor="nw",
        )
        self.label.pack(fill="both", expand=True, padx=20, pady=20)

        self.root.bind("<KeyPress>", self._on_key_down)
        self.root.bind("<KeyRelease>", self._on_key_up)
        self.root.focus_force()

    def _timestamp_ms(self):
        return (time.perf_counter_ns() - self.start_ns) / 1e6

    @staticmethod
    def _modifier_string(state):
        parts = []
        if state & 0x0001:
            parts.append("shift")
        if state & 0x0004:
            parts.append("ctrl")
        # macOS Tk: Option=0x0010 (Mod1), Command=0x0008 (Mod2) - varies by Tk version
        if state & 0x0080:
            parts.append("alt")
        if state & 0x0008:
            parts.append("cmd")
        return "+".join(parts) if parts else "none"

    def _get_character(self, event):
        """Get character representation from Tk event."""
        if event.char and event.char.isprintable():
            return event.char
        # Fallback to keysym
        keysym = event.keysym.lower()
        if keysym in ("return", "tab", "space", "backspace", "escape",
                       "shift_l", "shift_r", "control_l", "control_r",
                       "alt_l", "alt_r", "meta_l", "meta_r"):
            return keysym
        return KEYCODE_MAP.get(event.keycode, f"0x{event.keycode:02x}")

    def _on_key_down(self, event):
        ts_ms = self._timestamp_ms()
        event_ts_ms = float(event.time) if event.time else 0.0

        # Detect autorepeat: key_down without preceding key_up
        is_repeat = 1 if event.keycode in self.keys_down else 0
        self.keys_down.add(event.keycode)

        if event.keysym == "Escape":
            self._save_and_quit()
            return

        character = self._get_character(event)
        record = {
            "seq": len(self.events) + 1,
            "timestamp_ms": ts_ms,
            "event_timestamp_ms": event_ts_ms,
            "event_type": "key_down",
            "keycode": event.keycode,
            "scancode": 0,
            "character": character,
            "modifiers": self._modifier_string(event.state),
            "is_repeat": is_repeat,
        }
        self.events.append(record)
        self._update_display(record)

    def _on_key_up(self, event):
        ts_ms = self._timestamp_ms()
        event_ts_ms = float(event.time) if event.time else 0.0

        self.keys_down.discard(event.keycode)

        character = self._get_character(event)
        record = {
            "seq": len(self.events) + 1,
            "timestamp_ms": ts_ms,
            "event_timestamp_ms": event_ts_ms,
            "event_type": "key_up",
            "keycode": event.keycode,
            "scancode": 0,
            "character": character,
            "modifiers": self._modifier_string(event.state),
            "is_repeat": 0,
        }
        self.events.append(record)
        self._update_display(record)

    def _update_display(self, record):
        self.label.config(
            text=(
                "Keyboard Timing (Python/GUI/macOS)\n\n"
                "Press keys to record timing.\n"
                "Press Escape to stop and save.\n\n"
                f"Events: {len(self.events)}\n"
                f"Last: [{record['seq']}] {record['event_type']} "
                f"{record['character']} t={record['timestamp_ms']:.3f}ms"
            )
        )

    def _save_and_quit(self):
        self.write_csv()
        self.root.destroy()

    def write_csv(self):
        os.makedirs(os.path.dirname(self.output_path) or ".", exist_ok=True)
        with open(self.output_path, "w") as f:
            plat = platform.platform()
            utc_now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")
            f.write(f"# platform={plat}\n")
            f.write("# language=python\n")
            f.write("# mode=gui\n")
            f.write("# clock_source=time.perf_counter_ns\n")
            f.write(f"# start_time_utc={utc_now}\n")

            f.write("seq,timestamp_ms,event_timestamp_ms,event_type,keycode,scancode,character,modifiers,is_repeat\n")

            for e in self.events:
                f.write(
                    f"{e['seq']},{e['timestamp_ms']:.3f},{e['event_timestamp_ms']:.3f},"
                    f"{e['event_type']},{e['keycode']},{e['scancode']},"
                    f"{e['character']},{e['modifiers']},{e['is_repeat']}\n"
                )

        print(f"Wrote {len(self.events)} events to {self.output_path}", file=sys.stderr)

    def run(self):
        print("Keyboard timing (Python/GUI/macOS) - Press keys, Escape to stop", file=sys.stderr)
        print(f"Output: {self.output_path}", file=sys.stderr)
        self.root.mainloop()


def main():
    output_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_OUTPUT
    app = KeyboardTimerGUI(output_path)
    app.run()


if __name__ == "__main__":
    main()
