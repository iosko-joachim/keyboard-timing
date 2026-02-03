/*
 * terminal_windows.c - Keyboard timing via SetWindowsHookEx (Windows)
 *
 * Captures global key events using a low-level keyboard hook.
 * No special permissions needed (but must run in same session).
 *
 * Build: cl /O2 /W4 /Fe:terminal_windows.exe terminal_windows.c user32.lib kernel32.lib
 * Usage: terminal_windows.exe [output.csv]
 *        Press Ctrl+C to stop and save.
 */

#include <windows.h>
#include <stdio.h>
#include <time.h>

#define MAX_EVENTS 100000
#define DEFAULT_OUTPUT "output\\c_terminal_windows.csv"

typedef struct {
    int seq;
    double timestamp_ms;
    double event_timestamp_ms;
    const char *event_type;
    int keycode;
    int scancode;
    char character[16];
    char modifiers[64];
    int is_repeat;
} KeyEvent;

static KeyEvent events[MAX_EVENTS];
static int event_count = 0;
static volatile int running = 1;

static LARGE_INTEGER qpc_freq;
static LARGE_INTEGER qpc_start;
static HHOOK hook = NULL;

static double qpc_to_ms(LARGE_INTEGER now) {
    return (double)(now.QuadPart - qpc_start.QuadPart) * 1000.0 / (double)qpc_freq.QuadPart;
}

static void build_modifier_string(char *buf, size_t len) {
    buf[0] = '\0';
    int first = 1;

    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        strncat(buf, "shift", len - strlen(buf) - 1);
        first = 0;
    }
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
        if (!first) strncat(buf, "+", len - strlen(buf) - 1);
        strncat(buf, "ctrl", len - strlen(buf) - 1);
        first = 0;
    }
    if (GetAsyncKeyState(VK_MENU) & 0x8000) {
        if (!first) strncat(buf, "+", len - strlen(buf) - 1);
        strncat(buf, "alt", len - strlen(buf) - 1);
        first = 0;
    }
    if (first) {
        strncpy(buf, "none", len - 1);
        buf[len - 1] = '\0';
    }
}

static const char *vk_to_char(DWORD vk, DWORD scancode) {
    static char buf[16];
    /* Try to get the character from the virtual key */
    BYTE keyboard_state[256] = {0};
    GetKeyboardState(keyboard_state);
    WCHAR wchar[4] = {0};
    int result = ToUnicode(vk, scancode, keyboard_state, wchar, 4, 0);
    if (result == 1 && wchar[0] >= 32 && wchar[0] < 127) {
        buf[0] = (char)wchar[0];
        buf[1] = '\0';
        return buf;
    }

    /* Named keys */
    switch (vk) {
        case VK_RETURN:    return "return";
        case VK_TAB:       return "tab";
        case VK_SPACE:     return "space";
        case VK_BACK:      return "backspace";
        case VK_ESCAPE:    return "escape";
        case VK_LSHIFT:    return "shift_l";
        case VK_RSHIFT:    return "shift_r";
        case VK_LCONTROL:  return "ctrl_l";
        case VK_RCONTROL:  return "ctrl_r";
        case VK_LMENU:     return "alt_l";
        case VK_RMENU:     return "alt_r";
        case VK_LWIN:      return "win_l";
        case VK_RWIN:      return "win_r";
        case VK_CAPITAL:   return "capslock";
        case VK_DELETE:    return "delete";
        case VK_INSERT:    return "insert";
        case VK_HOME:      return "home";
        case VK_END:       return "end";
        case VK_PRIOR:     return "pageup";
        case VK_NEXT:      return "pagedown";
        case VK_LEFT:      return "left";
        case VK_RIGHT:     return "right";
        case VK_UP:        return "up";
        case VK_DOWN:      return "down";
    }

    snprintf(buf, sizeof(buf), "vk_0x%02lx", (unsigned long)vk);
    return buf;
}

static LRESULT CALLBACK keyboard_hook(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0 || event_count >= MAX_EVENTS) {
        return CallNextHookEx(hook, nCode, wParam, lParam);
    }

    KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lParam;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double ts_ms = qpc_to_ms(now);
    double event_ts_ms = (double)kb->time;  /* GetTickCount-based, ~15ms resolution */

    const char *event_type_str = NULL;
    int is_repeat = 0;

    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
        event_type_str = "key_down";
        /* Bit 7 of transition state: 1 = key was already down (repeat) */
        is_repeat = (kb->flags & LLKHF_UP) ? 0 : 0;
        /* For low-level hook, repeat detection needs state tracking */
    } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
        event_type_str = "key_up";
    } else {
        return CallNextHookEx(hook, nCode, wParam, lParam);
    }

    KeyEvent *e = &events[event_count];
    e->seq = event_count + 1;
    e->timestamp_ms = ts_ms;
    e->event_timestamp_ms = event_ts_ms;
    e->event_type = event_type_str;
    e->keycode = (int)kb->vkCode;
    e->scancode = (int)kb->scanCode;
    strncpy(e->character, vk_to_char(kb->vkCode, kb->scanCode),
            sizeof(e->character) - 1);
    e->character[sizeof(e->character) - 1] = '\0';
    build_modifier_string(e->modifiers, sizeof(e->modifiers));
    e->is_repeat = is_repeat;

    event_count++;

    fprintf(stderr, "\r[%d] %s %s (vk=0x%02lx sc=%ld) t=%.3fms",
            e->seq, event_type_str, e->character,
            (unsigned long)kb->vkCode, (long)kb->scanCode, ts_ms);

    return CallNextHookEx(hook, nCode, wParam, lParam);
}

static BOOL WINAPI console_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT) {
        running = 0;
        PostThreadMessage(GetCurrentThreadId(), WM_QUIT, 0, 0);
        return TRUE;
    }
    return FALSE;
}

static void write_csv(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s for writing\n", path);
        return;
    }

    OSVERSIONINFOA osvi;
    ZeroMemory(&osvi, sizeof(osvi));
    osvi.dwOSVersionInfoSize = sizeof(osvi);

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const char *arch = "unknown";
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
        arch = "x86_64";
    else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64)
        arch = "arm64";
    else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
        arch = "x86";

    time_t now_utc = time(NULL);
    struct tm *utc = gmtime(&now_utc);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S.000000Z", utc);

    fprintf(f, "# platform=Windows-%s\n", arch);
    fprintf(f, "# language=c\n");
    fprintf(f, "# mode=terminal\n");
    fprintf(f, "# clock_source=QueryPerformanceCounter\n");
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
    fprintf(stderr, "\nWrote %d events to %s\n", event_count, path);
}

int main(int argc, char *argv[]) {
    const char *output_path = (argc > 1) ? argv[1] : DEFAULT_OUTPUT;

    QueryPerformanceFrequency(&qpc_freq);
    QueryPerformanceCounter(&qpc_start);

    SetConsoleCtrlHandler(console_handler, TRUE);

    hook = SetWindowsHookExA(WH_KEYBOARD_LL, keyboard_hook, NULL, 0);
    if (!hook) {
        fprintf(stderr, "Error: Failed to set keyboard hook (error %lu)\n", GetLastError());
        return 1;
    }

    fprintf(stderr, "Keyboard timing (C/terminal/Windows) - Press keys, Ctrl+C to stop\n");
    fprintf(stderr, "Output: %s\n", output_path);

    MSG msg;
    while (running && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(hook);
    write_csv(output_path);

    return 0;
}
