# DigiMacro v2: a core below 0x1000, and the app above it.
#
#   make            the core, the spike's apps A, B and C, and spikeA.hex
#                   (core + app A: the image for Micronucleus or a programmer)
#   make page       spike-page.html: the spike page with A, B and C inside --
#                   one file to open in Chrome anywhere, no server needed
#   make test       the core's request handling, built for the host
#   make size       what each part takes
#   make upload     spikeA.hex onto a Digispark, through Micronucleus
#
# Everything lands in build/v2/. v1, the Arduino build, is build.sh, and
# tagged v1.0.
#
# Two links. The core's is ordinary, with its table pinned just below the
# boundary. An app's has no C runtime, starts at the boundary, keeps its
# variables above the core's, and reaches the core only through the table's
# addresses (app/coreapi.S). core/layout.h is the only thing they share.

AVR    ?= $(HOME)/.arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/
CC      = $(AVR)avr-gcc
OBJCOPY = $(AVR)avr-objcopy
SIZE    = $(AVR)avr-size
NM      = $(AVR)avr-nm
MCU     = attiny85
O       = build/v2
VUSB    = core/vusb
MICRONUCLEUS ?= $(HOME)/.arduino15/packages/digistump/tools/micronucleus/2.6/micronucleus

# The boundary can be moved from here, to measure: make APPSTART=0x1100
LAYOUT  = $(if $(APPSTART),-DAPP_START=$(APPSTART))
OPT    ?= -Os -flto
CFLAGS  = -mmcu=$(MCU) -DF_CPU=16500000L $(OPT) -Wall -std=gnu11 \
          -ffunction-sections -fdata-sections -Icore -Iapp -I$(VUSB) $(LAYOUT)
ASFLAGS = -mmcu=$(MCU) -DF_CPU=16500000L -Icore -Iapp -I$(VUSB) \
          -x assembler-with-cpp $(LAYOUT)

# Numbers from layout.h, for the linker
val = $(shell printf '%d' $$(( $$(echo '$(1)' | $(CC) -E -P -Icore $(LAYOUT) -include layout.h - | tail -1) )))
APP_START  := $(call val,APP_START)
APP_END    := $(call val,APP_END)
CORE_TABLE := $(call val,CORE_TABLE)
APP_RAM    := $(call val,APP_RAM)
APP_VECS   := $(call val,APP_START + 2 * APP_VECTORS)
APP_HDR    := $(call val,APP_MAGIC - APP_START)
hexaddr     = $$(printf 0x%x $(1))

all: $(O)/core.hex $(O)/appA.hex $(O)/appB.hex $(O)/appC.hex $(O)/spikeA.hex

$(O):
	mkdir -p $@

# ---- the core --------------------------------------------------------

CORE_OBJ = $(O)/core.o $(O)/coretab.o $(O)/usbdrv.o $(O)/usbdrvasm.o $(O)/osccal.o

$(O)/core.o: core/core.c core/core.h core/layout.h | $(O)
	$(CC) $(CFLAGS) -c -o $@ $<
$(O)/coretab.o: core/coretab.S core/layout.h | $(O)
	$(CC) $(ASFLAGS) -c -o $@ $<
$(O)/usbdrv.o: $(VUSB)/usbdrv.c | $(O)
	$(CC) $(CFLAGS) -c -o $@ $<
$(O)/usbdrvasm.o: $(VUSB)/usbdrvasm.S | $(O)
	$(CC) $(ASFLAGS) -c -o $@ $<
$(O)/osccal.o: $(VUSB)/osccal.c | $(O)
	$(CC) $(CFLAGS) -c -o $@ $<

# The core must end below its table, and its variables below the app's.
$(O)/core.elf: $(CORE_OBJ)
	$(CC) $(CFLAGS) -Wl,--gc-sections -Wl,-u,coretab \
	    -Wl,--section-start=.coretab=$(call hexaddr,$(CORE_TABLE)) \
	    -Wl,--defsym=__app_vectors=$(APP_VECS) -o $@ $^
	@end=$$(( 0x$$($(NM) $@ | awk '$$3=="__data_load_end"{print $$1}') )); \
	 ram=$$(( 0x$$($(NM) $@ | awk '$$3=="__bss_end"{print $$1}') - 0x800000 )); \
	 printf 'core: %d bytes of flash (table at %d), RAM to 0x%03X (app from 0x%03X)\n' \
	     $$end $(CORE_TABLE) $$ram $(APP_RAM); \
	 [ $$end -le $(CORE_TABLE) ] || { echo "core overruns its table"; exit 1; }; \
	 [ $$ram -le $(APP_RAM) ] || { echo "core RAM overruns the app's"; exit 1; }

$(O)/core.hex: $(O)/core.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

# ---- what every app links --------------------------------------------

$(O)/coreapi.o: app/coreapi.S core/layout.h | $(O)
	$(CC) $(ASFLAGS) -c -o $@ $<
$(O)/hidusb.o: app/hidusb.c app/hidusb.h core/core.h core/layout.h | $(O)
	$(CC) $(CFLAGS) -c -o $@ $<
$(O)/apphdr%.o: app/apphdr.S core/layout.h | $(O)
	$(CC) $(ASFLAGS) -DAPP_VER=$(APPVER_$*) -c -o $@ $<

# An app's link: no runtime, at the boundary, variables above the core's.
APPLINK = -nostartfiles -Wl,--gc-sections \
          -Wl,--section-start=.text=$(call hexaddr,$(APP_START)) \
          -Wl,--section-start=.data=$(call hexaddr,$$(( 0x800000 + $(APP_RAM) )))

# ...and its image: the CRC stamped after it, and the region checked.
define apphex
	$(OBJCOPY) -O binary -R .eeprom $< $(@:.hex=.bin)
	python3 app/apphex.py app $(APP_START) $(APP_HDR) $(@:.hex=.bin) $@
	@[ $$(( $(APP_START) + $$(stat -c %s $(@:.hex=.bin)) + 2 )) -le $(APP_END) ] \
	    || { echo "$@ runs past APP_END"; exit 1; }
endef

# ---- the spike's apps --------------------------------------------------

APPVER_A  = 1
APPVER_B  = 2
APPVER_C  = 3
APPDEFS_A = '-DAPP_LINE="DigiMacro v2 spike, app A"' -DAPP_BLINK=500
APPDEFS_B = '-DAPP_LINE="DigiMacro v2 spike, app B: replaced over WebHID"' -DAPP_BLINK=125
APPDEFS_C = -DAPP_BLINK=60 -DAPP_HANG=10000

$(O)/spike%.o: app/spike.c app/hidusb.h core/core.h core/layout.h | $(O)
	$(CC) $(CFLAGS) $(APPDEFS_$*) -c -o $@ $<
$(O)/app%.elf: $(O)/apphdr%.o $(O)/spike%.o $(O)/hidusb.o $(O)/coreapi.o
	$(CC) $(CFLAGS) $(APPLINK) -o $@ $^
$(O)/app%.hex: $(O)/app%.elf app/apphex.py
	$(apphex)

$(O)/spike%.hex: $(O)/core.hex $(O)/app%.hex
	python3 app/apphex.py join $@ $^

# A script, not something to build: without this, make's built-in rules
# would happily make apphex.py from apphex.py.o.
app/apphex.py: ;

$(O)/spike-page.html: app/spike.html $(O)/appA.hex $(O)/appB.hex $(O)/appC.hex
	python3 -c 'import json; \
	    apps = {n: open("$(O)/app%s.hex" % n).read() for n in "ABC"}; \
	    page = open("app/spike.html").read().replace("/*APPS*/{}", json.dumps(apps)); \
	    open("$@", "w").write(page)'
	@echo "$@: open it in Chrome"

page: $(O)/spike-page.html

# ---- the rest --------------------------------------------------------

size: $(O)/core.elf $(O)/appA.elf $(O)/hidusb.o
	$(SIZE) $^

upload: $(O)/spikeA.hex
	$(MICRONUCLEUS) --run $<

test:
	$(MAKE) -C core/test

clean:
	rm -rf $(O)
	$(MAKE) -C core/test clean

.PHONY: all page size upload test clean
.SECONDARY:
