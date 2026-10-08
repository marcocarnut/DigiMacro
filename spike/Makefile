# The spike: a core below the boundary, two trivial apps above it.
#
#   make            core.hex, appA.hex, appB.hex, and spikeA.hex (core + app A,
#                   the image that goes onto a board)
#   make test       the core's request handling, built for the host
#   make size       what each part takes
#   make upload     spikeA.hex onto a Digispark, through Micronucleus
#
# Two links. The core's is ordinary, with its table pinned just below the
# boundary. The app's has no C runtime, starts at the boundary, keeps its
# variables above the core's, and reaches the core only through the table's
# addresses (coreapi.S). layout.h is the only thing they share.

AVR    ?= $(HOME)/.arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/
CC      = $(AVR)avr-gcc
OBJCOPY = $(AVR)avr-objcopy
SIZE    = $(AVR)avr-size
NM      = $(AVR)avr-nm
MCU     = attiny85
VUSB    = ../DigiHidUSB/src/vusb

# The boundary can be moved from here, to measure: make APPSTART=0x1000
LAYOUT  = $(if $(APPSTART),-DAPP_START=$(APPSTART))
OPT    ?= -Os -flto
CFLAGS  = -mmcu=$(MCU) -DF_CPU=16500000L $(OPT) -Wall -std=gnu11 \
          -ffunction-sections -fdata-sections -I. -I$(VUSB) $(LAYOUT)
ASFLAGS = -mmcu=$(MCU) -DF_CPU=16500000L -I. -I$(VUSB) -x assembler-with-cpp $(LAYOUT)

# Numbers from layout.h, for the linker
val = $(shell printf '%d' $$(( $$(echo $(1) | $(CC) -E -P $(LAYOUT) -include layout.h - | tail -1) )))
APP_START  := $(call val,APP_START)
APP_END    := $(call val,APP_END)
CORE_TABLE := $(call val,CORE_TABLE)
APP_RAM    := $(call val,APP_RAM)

CORE_OBJ = core.o coretab.o usbdrv.o usbdrvasm.o osccal.o

all: core.hex appA.hex appB.hex spikeA.hex

core.o: core.c core.h layout.h
coretab.o: coretab.S layout.h
	$(CC) $(ASFLAGS) -c -o $@ $<
usbdrv.o: $(VUSB)/usbdrv.c
	$(CC) $(CFLAGS) -c -o $@ $<
usbdrvasm.o: $(VUSB)/usbdrvasm.S
	$(CC) $(ASFLAGS) -c -o $@ $<
osccal.o: $(VUSB)/osccal.c
	$(CC) $(CFLAGS) -c -o $@ $<

# The core must end below its table, and its variables below the app's.
core.elf: $(CORE_OBJ)
	$(CC) $(CFLAGS) -Wl,--gc-sections -Wl,-u,coretab \
	    -Wl,--section-start=.coretab=$$(printf 0x%x $(CORE_TABLE)) -o $@ $^
	@end=$$(( 0x$$($(NM) $@ | awk '$$3=="__data_load_end"{print $$1}') )); \
	 ram=$$(( 0x$$($(NM) $@ | awk '$$3=="__bss_end"{print $$1}') - 0x800000 )); \
	 printf 'core: %d bytes of flash (table at %d), RAM to 0x%03X (app from 0x%03X)\n' \
	     $$end $(CORE_TABLE) $$ram $(APP_RAM); \
	 [ $$end -le $(CORE_TABLE) ] || { echo "core overruns its table"; exit 1; }; \
	 [ $$ram -le $(APP_RAM) ] || { echo "core RAM overruns the app's"; exit 1; }

core.hex: core.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

app%.o: app.c core.h layout.h
	$(CC) $(CFLAGS) $(APPDEFS_$*) -c -o $@ $<
apphdr%.o: apphdr.S layout.h
	$(CC) $(ASFLAGS) -DAPP_VER=$(APPVER_$*) -c -o $@ $<
coreapi.o: coreapi.S layout.h
	$(CC) $(ASFLAGS) -c -o $@ $<

APPVER_A  = 1
APPVER_B  = 2
APPDEFS_A = '-DAPP_LINE="DigiMacro v2 spike, app A"' -DAPP_BLINK=500
APPDEFS_B = '-DAPP_LINE="DigiMacro v2 spike, app B: replaced over WebHID"' -DAPP_BLINK=125

app%.elf: apphdr%.o app%.o coreapi.o
	$(CC) $(CFLAGS) -nostartfiles -Wl,--gc-sections \
	    -Wl,--section-start=.text=$$(printf 0x%x $(APP_START)) \
	    -Wl,--section-start=.data=$$(printf 0x%x $$(( 0x800000 + $(APP_RAM) ))) -o $@ $^

app%.hex: app%.elf apphex.py
	$(OBJCOPY) -O binary -R .eeprom $< app$*.bin
	python3 apphex.py app $(APP_START) app$*.bin $@
	@[ $$(( $(APP_START) + $$(stat -c %s app$*.bin) + 2 )) -le $(APP_END) ] \
	    || { echo "app$* runs past APP_END"; exit 1; }

# A script, not something to build: without this, make's built-in rules
# would happily make apphex.py from apphex.py.o, from app.c.
apphex.py: ;

spike%.hex: core.hex app%.hex
	python3 apphex.py join $@ $^

size: core.elf appA.elf
	$(SIZE) core.elf appA.elf

upload: spikeA.hex
	micronucleus --run $<

test:
	$(MAKE) -C test

clean:
	rm -f *.o *.elf *.hex *.bin
	$(MAKE) -C test clean

.PHONY: all size upload test clean
.PRECIOUS: app%.o apphdr%.o app%.elf
