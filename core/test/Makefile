# The core's request handling, compiled for the host: see core_test.c.
CC ?= cc
OUT = $(or $(TMPDIR),/tmp)/digimacro-core-test

test: $(OUT)
	$(OUT)

$(OUT): core_test.c ../core.c ../core.h ../layout.h ../../app/hidusb.c ../../app/hidusb.h stub/*.h stub/*/*.h
	$(CC) -std=gnu11 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Istub -I.. -I../../app -o $@ core_test.c

clean:
	rm -f $(OUT)

.PHONY: test clean
