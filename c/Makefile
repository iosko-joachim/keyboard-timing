CC = clang
CFLAGS = -Wall -Wextra -O2
OBJCFLAGS = $(CFLAGS) -fobjc-arc

MINGW_CC = x86_64-w64-mingw32-gcc
MINGW_CFLAGS = -Wall -Wextra -O2

OUTPUTDIR = ../output

.PHONY: all clean outputdir windows

all: outputdir terminal_macos gui_macos

windows: outputdir terminal_windows.exe gui_windows.exe

outputdir:
	@mkdir -p $(OUTPUTDIR)

terminal_macos: terminal_macos.c
	$(CC) $(CFLAGS) -o $@ $< \
		-framework CoreGraphics \
		-framework CoreFoundation \
		-framework Carbon

gui_macos: gui_macos.m
	$(CC) $(OBJCFLAGS) -o $@ $< \
		-framework Cocoa

terminal_windows.exe: terminal_windows.c
	$(MINGW_CC) $(MINGW_CFLAGS) -o $@ $< \
		-luser32 -lkernel32

gui_windows.exe: gui_windows.c
	$(MINGW_CC) $(MINGW_CFLAGS) -o $@ $< \
		-mwindows -lgdi32 -luser32 -lkernel32

clean:
	rm -f terminal_macos gui_macos terminal_windows.exe gui_windows.exe
