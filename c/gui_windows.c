/*
 * gui_windows.c - Keyboard timing via Win32 window (Windows)
 *
 * Opens a window and captures WM_KEYDOWN/WM_KEYUP messages.
 * No special permissions needed.
 *
 * Build: cl /O2 /W4 /Fe:gui_windows.exe gui_windows.c user32.lib kernel32.lib gdi32.lib
 * Usage: gui_windows.exe [output.csv]
 *        Press Escape to stop and save.
 */

#include <windows.h>
#include <stdio.h>
#include <time.h>

#define MAX_EVENTS 100000
#define DEFAULT_OUTPUT "output\\c_gui_windows.csv"

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

static LARGE_INTEGER qpc_freq;
static LARGE_INTEGER qpc_start;
static const char *output_path = DEFAULT_OUTPUT;
static HWND main_hwnd = NULL;

static double qpc_to_ms(LARGE_INTEGER now) {
    return (double)(now.QuadPart - qpc_start.QuadPart) * 1000.0 / (double)qpc_freq.QuadPart;
}

static void build_modifier_string(char *buf, size_t len) {
    buf[0] = '\0';
    int first = 1;

    if (GetKeyState(VK_SHIFT) & 0x8000) {
        strncat(buf, "shift", len - strlen(buf) - 1);
        first = 0;
    }
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        if (!first) strncat(buf, "+", len - strlen(buf) - 1);
        strncat(buf, "ctrl", len - strlen(buf) - 1);
        first = 0;
    }
    if (GetKeyState(VK_MENU) & 0x8000) {
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
    BYTE keyboard_state[256] = {0};
    GetKeyboardState(keyboard_state);
    WCHAR wchar[4] = {0};
    int result = ToUnicode(vk, scancode, keyboard_state, wchar, 4, 0);
    if (result == 1 && wchar[0] >= 32 && wchar[0] < 127) {
        buf[0] = (char)wchar[0];
        buf[1] = '\0';
        return buf;
    }

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
        case VK_CAPITAL:   return "capslock";
        case VK_DELETE:    return "delete";
        case VK_LEFT:      return "left";
        case VK_RIGHT:     return "right";
        case VK_UP:        return "up";
        case VK_DOWN:      return "down";
    }

    snprintf(buf, sizeof(buf), "vk_0x%02lx", (unsigned long)vk);
    return buf;
}

static void write_csv(void) {
    FILE *f = fopen(output_path, "w");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s for writing\n", output_path);
        return;
    }

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
    fprintf(f, "# mode=gui\n");
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
    fprintf(stderr, "Wrote %d events to %s\n", event_count, output_path);
}

static void record_key_event(WPARAM vk, LPARAM lParam, const char *event_type_str) {
    if (event_count >= MAX_EVENTS) return;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double ts_ms = qpc_to_ms(now);

    /* GetMessageTime() returns the time the message was posted (GetTickCount-based) */
    double event_ts_ms = (double)GetMessageTime();

    int scancode = (lParam >> 16) & 0xFF;
    int is_repeat = 0;
    if (event_type_str[4] == 'd') {  /* "key_down" */
        is_repeat = (lParam & (1 << 30)) ? 1 : 0;  /* bit 30: previous key state */
    }

    KeyEvent *e = &events[event_count];
    e->seq = event_count + 1;
    e->timestamp_ms = ts_ms;
    e->event_timestamp_ms = event_ts_ms;
    e->event_type = event_type_str;
    e->keycode = (int)vk;
    e->scancode = scancode;
    strncpy(e->character, vk_to_char((DWORD)vk, (DWORD)scancode),
            sizeof(e->character) - 1);
    e->character[sizeof(e->character) - 1] = '\0';
    build_modifier_string(e->modifiers, sizeof(e->modifiers));
    e->is_repeat = is_repeat;

    event_count++;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (wParam == VK_ESCAPE) {
                write_csv();
                PostQuitMessage(0);
                return 0;
            }
            record_key_event(wParam, lParam, "key_down");
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            record_key_event(wParam, lParam, "key_up");
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));

            char text[512];
            snprintf(text, sizeof(text),
                "Keyboard Timing (C/GUI/Windows)\r\n\r\n"
                "Press keys to record timing.\r\n"
                "Press Escape to stop and save.\r\n\r\n"
                "Events: %d\r\n"
                "Output: %s",
                event_count, output_path);

            RECT text_rc = {20, 20, rc.right - 20, rc.bottom - 20};
            DrawTextA(hdc, text, -1, &text_rc, DT_LEFT | DT_TOP | DT_WORDBREAK);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            write_csv();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hPrev;
    (void)nShow;

    /* Parse command line for output path */
    if (lpCmd && lpCmd[0] != '\0') {
        output_path = lpCmd;
    }

    QueryPerformanceFrequency(&qpc_freq);
    QueryPerformanceCounter(&qpc_start);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "KeyTimingWindow";
    RegisterClassA(&wc);

    main_hwnd = CreateWindowA(
        "KeyTimingWindow",
        "Keyboard Timing - C/GUI/Windows",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 300,
        NULL, NULL, hInstance, NULL
    );

    if (!main_hwnd) {
        fprintf(stderr, "Error: Failed to create window (error %lu)\n", GetLastError());
        return 1;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
