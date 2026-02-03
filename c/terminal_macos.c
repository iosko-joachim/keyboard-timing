/*
 * terminal_macos.c - Keyboard timing via CGEventTap (macOS)
 *
 * Captures global key events using a CGEventTap.
 * Requires Accessibility permissions in System Settings.
 *
 * Build: make terminal_macos (see Makefile)
 * Usage: ./terminal_macos [output.csv]
 *        Press Ctrl+C to stop and save.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <libgen.h>
#include <sys/sysctl.h>
#include <mach/mach_time.h>
#include <CoreGraphics/CoreGraphics.h>
#include <Carbon/Carbon.h>

#define MAX_EVENTS 100000
#define DEFAULT_OUTPUT "output/c_terminal_macos.csv"

typedef struct {
    int seq;
    double timestamp_ms;
    double event_timestamp_ms;
    const char *event_type;
    int keycode;
    int scancode;
    char character[8];
    char modifiers[64];
    int is_repeat;
} KeyEvent;

static KeyEvent events[MAX_EVENTS];
static int event_count = 0;
static volatile sig_atomic_t running = 1;

static mach_timebase_info_data_t timebase;
static uint64_t start_time_abs;

static double abs_to_ms(uint64_t abs) {
    return (double)(abs * timebase.numer) / (double)(timebase.denom * 1000000ULL);
}

static void build_modifier_string(CGEventFlags flags, char *buf, size_t len) {
    buf[0] = '\0';
    int first = 1;

    if (flags & kCGEventFlagMaskShift) {
        strncat(buf, "shift", len - strlen(buf) - 1);
        first = 0;
    }
    if (flags & kCGEventFlagMaskControl) {
        if (!first) strncat(buf, "+", len - strlen(buf) - 1);
        strncat(buf, "ctrl", len - strlen(buf) - 1);
        first = 0;
    }
    if (flags & kCGEventFlagMaskAlternate) {
        if (!first) strncat(buf, "+", len - strlen(buf) - 1);
        strncat(buf, "alt", len - strlen(buf) - 1);
        first = 0;
    }
    if (flags & kCGEventFlagMaskCommand) {
        if (!first) strncat(buf, "+", len - strlen(buf) - 1);
        strncat(buf, "cmd", len - strlen(buf) - 1);
        first = 0;
    }
    if (first) {
        strncpy(buf, "none", len - 1);
        buf[len - 1] = '\0';
    }
}

static const char *keycode_to_char(int64_t keycode) {
    /* Common US keyboard layout mappings */
    static char buf[8];
    static const char *map[] = {
        [kVK_ANSI_A] = "a", [kVK_ANSI_S] = "s", [kVK_ANSI_D] = "d",
        [kVK_ANSI_F] = "f", [kVK_ANSI_H] = "h", [kVK_ANSI_G] = "g",
        [kVK_ANSI_Z] = "z", [kVK_ANSI_X] = "x", [kVK_ANSI_C] = "c",
        [kVK_ANSI_V] = "v", [kVK_ANSI_B] = "b", [kVK_ANSI_Q] = "q",
        [kVK_ANSI_W] = "w", [kVK_ANSI_E] = "e", [kVK_ANSI_R] = "r",
        [kVK_ANSI_Y] = "y", [kVK_ANSI_T] = "t", [kVK_ANSI_1] = "1",
        [kVK_ANSI_2] = "2", [kVK_ANSI_3] = "3", [kVK_ANSI_4] = "4",
        [kVK_ANSI_6] = "6", [kVK_ANSI_5] = "5", [kVK_ANSI_9] = "9",
        [kVK_ANSI_7] = "7", [kVK_ANSI_8] = "8", [kVK_ANSI_0] = "0",
        [kVK_ANSI_O] = "o", [kVK_ANSI_U] = "u", [kVK_ANSI_I] = "i",
        [kVK_ANSI_P] = "p", [kVK_ANSI_L] = "l", [kVK_ANSI_J] = "j",
        [kVK_ANSI_K] = "k", [kVK_ANSI_N] = "n", [kVK_ANSI_M] = "m",
        [kVK_Space] = "space", [kVK_Return] = "return", [kVK_Tab] = "tab",
        [kVK_Delete] = "backspace", [kVK_Escape] = "escape",
    };
    if (keycode >= 0 && keycode < (int64_t)(sizeof(map) / sizeof(map[0])) && map[keycode]) {
        return map[keycode];
    }
    snprintf(buf, sizeof(buf), "0x%02llx", (unsigned long long)keycode);
    return buf;
}

/* Track which keys are currently pressed for flags-changed events */
static int modifier_key_down[256] = {0};

static CGEventRef event_callback(CGEventTapProxy proxy, CGEventType type,
                                  CGEventRef event, void *refcon) {
    (void)proxy;
    (void)refcon;

    if (event_count >= MAX_EVENTS) return event;

    uint64_t now = mach_absolute_time();
    double ts_ms = abs_to_ms(now - start_time_abs);

    uint64_t event_ts_ns = CGEventGetTimestamp(event);
    double event_ts_ms = (double)event_ts_ns / 1e6;

    int64_t keycode = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
    CGEventFlags flags = CGEventGetFlags(event);
    int64_t autorepeat = CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat);

    const char *event_type_str = NULL;

    if (type == kCGEventKeyDown) {
        event_type_str = "key_down";
    } else if (type == kCGEventKeyUp) {
        event_type_str = "key_up";
    } else if (type == kCGEventFlagsChanged) {
        /* Modifier key: determine down/up by tracking state */
        if (keycode >= 0 && keycode < 256) {
            if (modifier_key_down[keycode]) {
                event_type_str = "key_up";
                modifier_key_down[keycode] = 0;
            } else {
                event_type_str = "key_down";
                modifier_key_down[keycode] = 1;
            }
        } else {
            event_type_str = "flags_changed";
        }
    } else {
        return event;
    }

    KeyEvent *e = &events[event_count];
    e->seq = event_count + 1;
    e->timestamp_ms = ts_ms;
    e->event_timestamp_ms = event_ts_ms;
    e->event_type = event_type_str;
    e->keycode = (int)keycode;
    e->scancode = 0;  /* macOS has no raw HID scancodes */
    strncpy(e->character, keycode_to_char(keycode), sizeof(e->character) - 1);
    e->character[sizeof(e->character) - 1] = '\0';
    build_modifier_string(flags, e->modifiers, sizeof(e->modifiers));
    e->is_repeat = (int)autorepeat;

    event_count++;

    fprintf(stderr, "\r[%d] %s %s (keycode=%d) t=%.3fms",
            e->seq, event_type_str, e->character, e->keycode, ts_ms);

    return event;
}

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
    CFRunLoopStop(CFRunLoopGetMain());
}

static void write_csv(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s for writing\n", path);
        return;
    }

    /* Metadata header */
    char platform[256];
    size_t plen = sizeof(platform);
    sysctlbyname("kern.osproductversion", platform, &plen, NULL, 0);

    char machine[256];
    size_t mlen = sizeof(machine);
    sysctlbyname("hw.machine", machine, &mlen, NULL, 0);

    time_t now_utc = time(NULL);
    struct tm *utc = gmtime(&now_utc);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S.000000Z", utc);

    fprintf(f, "# platform=macOS-%s-%s\n", platform, machine);
    fprintf(f, "# language=c\n");
    fprintf(f, "# mode=terminal\n");
    fprintf(f, "# clock_source=mach_absolute_time\n");
    fprintf(f, "# start_time_utc=%s\n", time_str);

    /* CSV header */
    fprintf(f, "seq,timestamp_ms,event_timestamp_ms,event_type,keycode,scancode,character,modifiers,is_repeat\n");

    for (int i = 0; i < event_count; i++) {
        KeyEvent *e = &events[i];
        fprintf(f, "%d,%.3f,%.3f,%s,%d,%d,%s,%s,%d\n",
                e->seq, e->timestamp_ms, e->event_timestamp_ms,
                e->event_type, e->keycode, e->scancode,
                e->character, e->modifiers, e->is_repeat);
    }

    fclose(f);
    fprintf(stderr, "\nWrote %d events to %s\n", event_count, path);
}

static const char *resolved_output = NULL;

int main(int argc, char *argv[]) {
    const char *output_path;
    if (argc > 1) {
        output_path = argv[1];
    } else {
        static char resolved[1024];
        char *dir = dirname(argv[0]);
        snprintf(resolved, sizeof(resolved), "%s/../output/c_terminal_macos.csv", dir);
        output_path = resolved;
    }
    resolved_output = output_path;

    mach_timebase_info(&timebase);
    start_time_abs = mach_absolute_time();

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    CGEventMask mask = (1 << kCGEventKeyDown) |
                       (1 << kCGEventKeyUp) |
                       (1 << kCGEventFlagsChanged);

    CFMachPortRef tap = CGEventTapCreate(
        kCGSessionEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionListenOnly,
        mask,
        event_callback,
        NULL
    );

    if (!tap) {
        fprintf(stderr, "Error: Failed to create event tap.\n");
        fprintf(stderr, "Grant Accessibility permission in System Settings > Privacy & Security.\n");
        return 1;
    }

    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
    CGEventTapEnable(tap, true);

    fprintf(stderr, "Keyboard timing (C/terminal/macOS) - Press keys, Ctrl+C to stop\n");
    fprintf(stderr, "Output: %s\n", output_path);

    while (running) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, true);
    }

    write_csv(output_path);

    CFRelease(source);
    CFRelease(tap);

    return 0;
}
