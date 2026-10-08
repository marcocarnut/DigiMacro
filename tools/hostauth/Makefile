# digimacro-host: send a DigiMacro board its host key, so that macros
# guarded by {host} play on this computer.
#
#   make                the client
#   make install        into ~/.local/bin; PREFIX=/usr/local for everyone
#   make clean

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PREFIX  ?= $(HOME)/.local

digimacro-host: digimacro-host.c
	$(CC) $(CFLAGS) -o $@ $<

install: digimacro-host
	install -D -m 755 digimacro-host $(DESTDIR)$(PREFIX)/bin/digimacro-host

clean:
	rm -f digimacro-host

.PHONY: install clean
