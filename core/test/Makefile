# The core's request handling, compiled for the host: see core_test.c.
CC ?= cc
OUT = $(or $(TMPDIR),/tmp)/digimacro-core-test

# Twice: with the app region to the end of the chip, as v2 ships, and
# stopping below Micronucleus, as the spike's builds do.
test: $(OUT) $(OUT)-1a00 $(OUT)-osccal
	$(OUT)
	$(OUT)-1a00
	$(OUT)-osccal

# V-USB's oscillator calibration, against a model of the chip's oscillator
$(OUT)-osccal: osccal_test.c ../vusb/osccal.c stub/avr/io.h
	$(CC) -std=gnu11 -Wall -Istub -o $@ osccal_test.c

$(OUT)-1a00: core_test.c ../core.c ../core.h ../layout.h ../../app/hidusb.c ../../app/hidusb.h stub/*.h stub/*/*.h
	$(CC) -std=gnu11 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Istub -I.. -I../../app -DAPP_END=0x1A00 -o $@ core_test.c

$(OUT): core_test.c ../core.c ../core.h ../layout.h ../../app/hidusb.c ../../app/hidusb.h stub/*.h stub/*/*.h
	$(CC) -std=gnu11 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Istub -I.. -I../../app -o $@ core_test.c

clean:
	rm -f $(OUT) $(OUT)-1a00 $(OUT)-osccal

.PHONY: test clean
