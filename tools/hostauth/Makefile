# digimacro-host: give a DigiMacro board the key to its secrets, once it
# has proved it is the board, so that {secret}s play on this computer.
#
#   make                the client, for Linux
#   make install        into ~/.local/bin; PREFIX=/usr/local for everyone
#   make static         digimacro-host-linux-x86_64, statically linked, for
#                       handing to anyone on any distribution
#   make windows        digimacro-host.exe, cross-compiled with MinGW-w64
#                       (Debian and Ubuntu: gcc-mingw-w64-x86-64). One file,
#                       nothing to install on the Windows side.
#   make clean

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PREFIX  ?= $(HOME)/.local
WINCC   ?= x86_64-w64-mingw32-gcc

digimacro-host: digimacro-host.c xtea.h
	$(CC) $(CFLAGS) -o $@ $<

static: digimacro-host-linux-x86_64
digimacro-host-linux-x86_64: digimacro-host.c xtea.h
	$(CC) $(CFLAGS) -static -s -o $@ $<

# Static, so the .exe needs no MinGW runtime DLLs beside it.
windows: digimacro-host.exe
digimacro-host.exe: digimacro-host.c xtea.h
	$(WINCC) $(CFLAGS) -static -s -o $@ $< -lhid -lsetupapi

install: digimacro-host
	install -D -m 755 digimacro-host $(DESTDIR)$(PREFIX)/bin/digimacro-host

clean:
	rm -f digimacro-host digimacro-host.exe digimacro-host-linux-x86_64

.PHONY: static windows install clean
