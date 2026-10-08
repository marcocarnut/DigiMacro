# digimacro-host: send a DigiMacro board its host key, so that macros
# guarded by {host} play on this computer.
#
#   make                the client, for Linux
#   make install        into ~/.local/bin; PREFIX=/usr/local for everyone
#   make windows        digimacro-host.exe, cross-compiled with MinGW-w64
#                       (Debian and Ubuntu: gcc-mingw-w64-x86-64). One file,
#                       nothing to install on the Windows side.
#   make clean

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PREFIX  ?= $(HOME)/.local
WINCC   ?= x86_64-w64-mingw32-gcc

digimacro-host: digimacro-host.c
	$(CC) $(CFLAGS) -o $@ $<

# Static, so the .exe needs no MinGW runtime DLLs beside it.
windows: digimacro-host.exe
digimacro-host.exe: digimacro-host.c
	$(WINCC) $(CFLAGS) -static -s -o $@ $< -lhid -lsetupapi

install: digimacro-host
	install -D -m 755 digimacro-host $(DESTDIR)$(PREFIX)/bin/digimacro-host

clean:
	rm -f digimacro-host digimacro-host.exe

.PHONY: windows install clean
