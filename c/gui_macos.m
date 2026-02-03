/*
 * gui_macos.m - Keyboard timing via Cocoa NSApplication (macOS)
 *
 * Opens a window and captures key events via NSEvent.
 * No Accessibility permissions needed (only captures in own window).
 *
 * Build: make gui_macos (see Makefile)
 * Usage: ./gui_macos [output.csv]
 *        Press Escape to stop and save.
 */

#import <Cocoa/Cocoa.h>
#include <mach/mach_time.h>
#include <sys/sysctl.h>
#include <signal.h>
#include <libgen.h>

#define MAX_EVENTS 100000
#define DEFAULT_OUTPUT "output/c_gui_macos.csv"

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
static mach_timebase_info_data_t timebase;
static uint64_t start_time_abs;
static const char *output_path = DEFAULT_OUTPUT;

static double abs_to_ms(uint64_t abs) {
    return (double)(abs * timebase.numer) / (double)(timebase.denom * 1000000ULL);
}

static void build_modifier_string(NSEventModifierFlags flags, char *buf, size_t len) {
    buf[0] = '\0';
    int first = 1;

    if (flags & NSEventModifierFlagShift) {
        strncat(buf, "shift", len - strlen(buf) - 1);
        first = 0;
    }
    if (flags & NSEventModifierFlagControl) {
        if (!first) strncat(buf, "+", len - strlen(buf) - 1);
        strncat(buf, "ctrl", len - strlen(buf) - 1);
        first = 0;
    }
    if (flags & NSEventModifierFlagOption) {
        if (!first) strncat(buf, "+", len - strlen(buf) - 1);
        strncat(buf, "alt", len - strlen(buf) - 1);
        first = 0;
    }
    if (flags & NSEventModifierFlagCommand) {
        if (!first) strncat(buf, "+", len - strlen(buf) - 1);
        strncat(buf, "cmd", len - strlen(buf) - 1);
        first = 0;
    }
    if (first) {
        strncpy(buf, "none", len - 1);
        buf[len - 1] = '\0';
    }
}

static const char *keycode_to_char(unsigned short keycode) {
    static char buf[8];
    /* Common US keyboard layout */
    static const char *map[128] = {
        [0x00] = "a", [0x01] = "s", [0x02] = "d", [0x03] = "f",
        [0x04] = "h", [0x05] = "g", [0x06] = "z", [0x07] = "x",
        [0x08] = "c", [0x09] = "v", [0x0B] = "b", [0x0C] = "q",
        [0x0D] = "w", [0x0E] = "e", [0x0F] = "r", [0x10] = "y",
        [0x11] = "t", [0x12] = "1", [0x13] = "2", [0x14] = "3",
        [0x15] = "4", [0x16] = "6", [0x17] = "5", [0x18] = "9",
        [0x19] = "7", [0x1A] = "8", [0x1B] = "0", [0x1C] = "o",
        [0x1D] = "u", [0x1E] = "i", [0x1F] = "p", [0x20] = "l",
        [0x21] = "j", [0x22] = "k", [0x23] = "n", [0x24] = "return",
        [0x26] = "m", [0x30] = "tab", [0x31] = "space",
        [0x33] = "backspace", [0x35] = "escape",
    };
    if (keycode < 128 && map[keycode]) {
        return map[keycode];
    }
    snprintf(buf, sizeof(buf), "0x%02x", keycode);
    return buf;
}

static void record_event(NSEvent *nsEvent, const char *event_type_str) {
    if (event_count >= MAX_EVENTS) return;

    uint64_t now = mach_absolute_time();
    double ts_ms = abs_to_ms(now - start_time_abs);
    double event_ts_ms = [nsEvent timestamp] * 1000.0;  /* NSEvent timestamp is in seconds since boot */

    unsigned short keycode = [nsEvent keyCode];
    NSEventModifierFlags flags = [nsEvent modifierFlags];
    BOOL is_repeat = [nsEvent isARepeat];

    KeyEvent *e = &events[event_count];
    e->seq = event_count + 1;
    e->timestamp_ms = ts_ms;
    e->event_timestamp_ms = event_ts_ms;
    e->event_type = event_type_str;
    e->keycode = keycode;
    e->scancode = 0;
    strncpy(e->character, keycode_to_char(keycode), sizeof(e->character) - 1);
    e->character[sizeof(e->character) - 1] = '\0';
    build_modifier_string(flags, e->modifiers, sizeof(e->modifiers));
    e->is_repeat = is_repeat ? 1 : 0;

    event_count++;

    fprintf(stderr, "\r[%d] %s %s (keycode=%d) t=%.3fms",
            e->seq, event_type_str, e->character, e->keycode, ts_ms);
}

/* Track modifier state for flagsChanged */
static int modifier_key_down[256] = {0};

static void record_flags_changed(NSEvent *nsEvent) {
    if (event_count >= MAX_EVENTS) return;

    uint64_t now = mach_absolute_time();
    double ts_ms = abs_to_ms(now - start_time_abs);
    double event_ts_ms = [nsEvent timestamp] * 1000.0;

    unsigned short keycode = [nsEvent keyCode];
    NSEventModifierFlags flags = [nsEvent modifierFlags];

    const char *event_type_str;
    if (keycode < 256 && modifier_key_down[keycode]) {
        event_type_str = "key_up";
        modifier_key_down[keycode] = 0;
    } else {
        event_type_str = "key_down";
        if (keycode < 256) modifier_key_down[keycode] = 1;
    }

    KeyEvent *e = &events[event_count];
    e->seq = event_count + 1;
    e->timestamp_ms = ts_ms;
    e->event_timestamp_ms = event_ts_ms;
    e->event_type = event_type_str;
    e->keycode = keycode;
    e->scancode = 0;
    strncpy(e->character, keycode_to_char(keycode), sizeof(e->character) - 1);
    e->character[sizeof(e->character) - 1] = '\0';
    build_modifier_string(flags, e->modifiers, sizeof(e->modifiers));
    e->is_repeat = 0;

    event_count++;

    fprintf(stderr, "\r[%d] %s %s (keycode=%d) t=%.3fms",
            e->seq, event_type_str, e->character, e->keycode, ts_ms);
}

static void write_csv(void) {
    FILE *f = fopen(output_path, "w");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s for writing\n", output_path);
        return;
    }

    char platform_ver[256];
    size_t plen = sizeof(platform_ver);
    sysctlbyname("kern.osproductversion", platform_ver, &plen, NULL, 0);

    char machine[256];
    size_t mlen = sizeof(machine);
    sysctlbyname("hw.machine", machine, &mlen, NULL, 0);

    time_t now_utc = time(NULL);
    struct tm *utc = gmtime(&now_utc);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S.000000Z", utc);

    fprintf(f, "# platform=macOS-%s-%s\n", platform_ver, machine);
    fprintf(f, "# language=c\n");
    fprintf(f, "# mode=gui\n");
    fprintf(f, "# clock_source=mach_absolute_time\n");
    fprintf(f, "# start_time_utc=%s\n", time_str);

    fprintf(f, "seq,timestamp_ms,event_timestamp_ms,event_type,keycode,scancode,character,modifiers,is_repeat\n");

    for (int i = 0; i < event_count; i++) {
        KeyEvent *e = &events[i];
        fprintf(f, "%d,%.3f,%.3f,%s,%d,%d,%s,%s,%d\n",
                e->seq, e->timestamp_ms, e->event_timestamp_ms,
                e->event_type, e->keycode, e->scancode,
                e->character, e->modifiers, e->is_repeat);
    }

    fclose(f);
    fprintf(stderr, "\nWrote %d events to %s\n", event_count, output_path);
}

/* Custom NSWindow subclass to capture key events */
@interface TimingWindow : NSWindow
@end

@implementation TimingWindow

- (BOOL)canBecomeKeyWindow {
    return YES;
}

- (void)keyDown:(NSEvent *)event {
    if ([event keyCode] == 0x35) { /* Escape */
        write_csv();
        [NSApp terminate:nil];
        return;
    }
    record_event(event, "key_down");
}

- (void)keyUp:(NSEvent *)event {
    record_event(event, "key_up");
}

- (void)flagsChanged:(NSEvent *)event {
    record_flags_changed(event);
}

@end

/* Custom NSView to draw instructions */
@interface TimingView : NSView
@end

@implementation TimingView

- (void)drawRect:(NSRect)dirtyRect {
    [[NSColor whiteColor] setFill];
    NSRectFill(dirtyRect);

    NSDictionary *attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:16],
        NSForegroundColorAttributeName: [NSColor blackColor],
    };

    NSString *text = [NSString stringWithFormat:
        @"Keyboard Timing (C/GUI/macOS)\n\n"
        @"Press keys to record timing.\n"
        @"Press Escape to stop and save.\n\n"
        @"Events: %d\n"
        @"Output: %s",
        event_count, output_path];

    [text drawAtPoint:NSMakePoint(20, dirtyRect.size.height - 40) withAttributes:attrs];
}

@end

static void signal_handler(int sig) {
    (void)sig;
    write_csv();
    _exit(0);
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc > 1) {
            output_path = argv[1];
        } else {
            /* Resolve output path relative to binary directory */
            static char resolved[1024];
            char *dir = dirname((char *)argv[0]);
            snprintf(resolved, sizeof(resolved), "%s/../output/c_gui_macos.csv", dir);
            output_path = resolved;
        }

        signal(SIGTERM, signal_handler);
        signal(SIGINT, signal_handler);

        mach_timebase_info(&timebase);
        start_time_abs = mach_absolute_time();

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSRect frame = NSMakeRect(200, 200, 500, 300);
        TimingWindow *window = [[TimingWindow alloc]
            initWithContentRect:frame
                      styleMask:(NSWindowStyleMaskTitled |
                                 NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskResizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];

        [window setTitle:@"Keyboard Timing - C/GUI/macOS"];

        TimingView *view = [[TimingView alloc] initWithFrame:frame];
        [window setContentView:view];

        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        /* Periodically refresh the view to update event count */
        [NSTimer scheduledTimerWithTimeInterval:0.5
                                        repeats:YES
                                          block:^(NSTimer *timer) {
            (void)timer;
            [view setNeedsDisplay:YES];
        }];

        fprintf(stderr, "Keyboard timing (C/GUI/macOS) - Press keys, Escape to stop\n");
        fprintf(stderr, "Output: %s\n", output_path);

        [NSApp run];
    }
    return 0;
}
