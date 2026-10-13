# DigiMacro v2: a core below 0x1000, and the app above it.
#
#   make            DigiMacro: digimacro.hex (the app, for updates over USB)
#                   and digimacro-full.hex (core and app, for a programmer --
#                   it runs past Micronucleus); options as v1's build.sh had
#                   them:
#                   make BUTTON=1 RC=1 LEDECHO=1 TOUCHDEBUG=1
#   make release    DigiMacro as it ships, into web/firmware/: the app for
#                   the page to update a v2 board with, the core alone for
#                   the page to put on a board through Micronucleus, the
#                   full image for a programmer, and a json naming the core
#                   API the app needs.
#                   Commit the result: the page carries it.
#   make spike      the core, the spike's apps A, B and C, and spikeA.hex,
#                   all below Micronucleus, in build/v2-spike/
#   make page       spike-page.html: the spike page with A, B and C inside --
#                   one file to open in Chrome anywhere, no server needed
#   make irspike    the IR spike: irspike.hex, an app that types what a
#                   remote sends, edge by edge (app/irspike.c), and
#                   irspike-full.hex, core and all, for a programmer
#   make uninstall  uninstall.hex: an app that wipes the EEPROM and hands
#                   the board back to Micronucleus, which it carries
#                   (app/uninstall.c)
#   make coreupdate spike: coreupdate.hex, an app that puts the core just
#                   built in place of a board's, in one step
#                   (app/coreupdate.S); make simcoreupdate tries it on a
#                   simulated board with v3.0's core
#   make test       the core's request handling, built for the host
#   make size       what each part takes
#
# Everything lands in build/v2/. A board gets v2 from the page (Set up a new
# board, or Move to v2), or digimacro-full.hex from a programmer. v1, the
# Arduino build, lives at tag v1.0.
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
O       = build/v2$(if $(MICRONUCLEUS_SAFE),-spike)
VUSB    = core/vusb

# The boundary can be moved from here, to measure: make APPSTART=0x1100.
# The spike's builds keep below Micronucleus, at 0x1A00, in their own
# directory: `make spike` comes back here with MICRONUCLEUS_SAFE set.
LAYOUT  = $(if $(APPSTART),-DAPP_START=$(APPSTART)) \
          $(if $(MICRONUCLEUS_SAFE),-DAPP_END=0x1A00)
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
PAGE       := $(call val,PAGE)
APP_RAM    := $(call val,APP_RAM)
APP_VECS   := $(call val,APP_START + 2 * APP_VECTORS)
APP_HDR    := $(call val,APP_MAGIC - APP_START)
hexaddr     = $$(printf 0x%x $(1))

all: $(O)/digimacro.hex $(O)/digimacro-full.hex
ifdef MICRONUCLEUS_SAFE
spike: $(O)/core.hex $(O)/appA.hex $(O)/appB.hex $(O)/appC.hex $(O)/spikeA.hex
page: $(O)/spike-page.html
else
spike page:
	$(MAKE) MICRONUCLEUS_SAFE=1 $@
endif

$(O):
	mkdir -p $@

# ---- the core --------------------------------------------------------

CORE_OBJ = $(O)/core.o $(O)/coretab.o $(O)/usbdrv.o $(O)/usbdrvasm.o $(O)/osccal.o

# Measured, flag by flag, on avr-gcc 7.3: these three take 72 bytes off the
# core, and nothing else tried did (newer gcc makes it bigger). The core's
# alone, and at the link too, where -flto compiles it.
COREOPT = -fno-move-loop-invariants -fno-inline-small-functions \
          -fno-optimize-sibling-calls

$(O)/core.o: core/core.c core/core.h core/layout.h $(VUSB)/usbconfig.h | $(O)
	$(CC) $(CFLAGS) $(COREOPT) -c -o $@ $<
$(O)/coretab.o: core/coretab.S core/layout.h | $(O)
	$(CC) $(ASFLAGS) -c -o $@ $<
$(O)/usbdrv.o: $(VUSB)/usbdrv.c $(VUSB)/usbconfig.h | $(O)
	$(CC) $(CFLAGS) $(COREOPT) -c -o $@ $<
$(O)/usbdrvasm.o: $(VUSB)/usbdrvasm.S $(VUSB)/usbconfig.h | $(O)
	$(CC) $(ASFLAGS) -c -o $@ $<
$(O)/osccal.o: $(VUSB)/osccal.c $(VUSB)/usbconfig.h | $(O)
	$(CC) $(CFLAGS) $(COREOPT) -c -o $@ $<

# The core must end below its table, and its variables below the app's.
$(O)/core.elf: $(CORE_OBJ)
	$(CC) $(CFLAGS) $(COREOPT) -Wl,--gc-sections -Wl,-u,coretab \
	    -Wl,--section-start=.coretab=$(call hexaddr,$(CORE_TABLE)) \
	    -Wl,--defsym=__app_vectors=$(APP_VECS) -o $@ $^
	@end=$$(( 0x$$($(NM) $@ | awk '$$3=="__data_load_end"{print $$1}') )); \
	 ram=$$(( 0x$$($(NM) $@ | awk '$$3=="_end"{print $$1}') - 0x800000 )); \
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

# ---- the simulated board ----------------------------------------------

# Secrets end to end on a simulated ATtiny85 with a USB host: the real
# firmware, app/xtea.S included, which the PC tests cannot run. Needs the
# harness and patched simavr from ../simavr-softusb (its PLAN.md);
# test/run.sh runs this when they are there.
SOFTUSB ?= ../simavr-softusb
SIMLIB   = $(SOFTUSB)/simavr/simavr/obj-$(shell cc -dumpmachine)/libsimavr.a
$(O)/sim_secret: test/sim/secret.c tools/hostauth/xtea.h $(SIMLIB) | $(O)
	cc -O2 -g -Wall -I$(SOFTUSB)/simavr/simavr/sim -I$(SOFTUSB)/harness -o $@ $< \
	    $(addprefix $(SOFTUSB)/harness/,board.c softusb.c dm.c touchpad.c) $(SIMLIB) -lelf -lm
simtest: $(O)/sim_secret $(O)/core.elf $(O)/digimacro.hex
	node test/webtable.js --emit-secret > $(O)/sealed.txt
	$(O)/sim_secret $(O)/core.elf $(O)/digimacro.hex $(O)/sealed.txt

# ---- DigiMacro -------------------------------------------------------

# The sketch, as C++ -- it was an Arduino sketch, and still is one in v1.0
DMDEFS  = $(if $(BUTTON),-DHAVE_TOUCH=0) $(if $(RC),-DTOUCH_PULLUP=0) \
          $(if $(LEDECHO),-DLED_ECHO=1) $(if $(TOUCHDEBUG),-DTOUCH_DEBUG=1)
# Shared prologues and epilogues: 34 bytes the secrets needed. The firmware
# only -- the core is built as it was, and stays byte for byte the same.
DMOPT   = -mcall-prologues
CXXFLAGS = $(filter-out -std=gnu11,$(CFLAGS)) -x c++ -std=gnu++11 \
          -fno-exceptions -fno-rtti -fno-threadsafe-statics $(DMOPT) $(DMDEFS)
APPVER_DM := $(shell sed -n 's/^\#define DM_FW_VERSION *\([0-9]*\).*/\1/p' app/hidusb.h)

# Its header carries DM_FW_VERSION, which lives in hidusb.h
$(O)/apphdrDM.o: app/hidusb.h

$(O)/digimacro.o: DigiMacro.ino app/hidusb.h core/core.h core/layout.h | $(O)
	$(CC) $(CXXFLAGS) -c -o $@ $<
$(O)/xtea.o: app/xtea.S | $(O)
	$(CC) $(ASFLAGS) -c -o $@ $<
$(O)/digimacro.o: app/xtea.h
$(O)/digimacro.elf: $(O)/apphdrDM.o $(O)/digimacro.o $(O)/hidusb.o $(O)/coreapi.o $(O)/xtea.o
	$(CC) $(CFLAGS) $(DMOPT) $(APPLINK) -o $@ $^
	@printf 'app: %d bytes of %d\n' $$(( 0x$$($(NM) $@ | awk '$$3=="__data_load_end"{print $$1}') - $(APP_START) )) $$(( $(APP_END) - $(APP_START) ))
$(O)/digimacro.hex: $(O)/digimacro.elf app/apphex.py
	$(apphex)
$(O)/digimacro-full.hex: $(O)/core.hex $(O)/digimacro.hex
	python3 app/apphex.py join $@ $^

CORE_VER := $(shell sed -n 's/^\#define DM_CORE_VERSION *\([0-9]*\).*/\1/p' core/core.h)
CORE_API := $(call val,CORE_API)

release: $(O)/digimacro.hex $(O)/digimacro-full.hex $(O)/uninstall.hex
	@[ -z "$(strip $(DMDEFS))" ] || { echo "release is the stock firmware: no options"; exit 1; }
	cp $(O)/uninstall.hex web/firmware/digimacro-uninstall.hex
	cp $(O)/digimacro.hex web/firmware/digimacro-v2.hex
	cp $(O)/digimacro-full.hex web/firmware/digimacro-v2-full.hex
	cp $(O)/core.hex web/firmware/digimacro-v2-core.hex
	printf '{ "app": %s, "core": %s, "api": %s }\n' $(APPVER_DM) $(CORE_VER) $(CORE_API) \
	    > web/firmware/digimacro-v2.json
	@cat web/firmware/digimacro-v2.json

# What gets published, in build/release/: the page as one file, firmware
# and all, and the host-key client for Linux (static, so it runs on any
# distribution) and for Windows.
site:
	python3 tools/site.py build/release/index.html
hosts:
	$(MAKE) -C tools/hostauth static windows
	mkdir -p build/release
	cp tools/hostauth/digimacro-host-linux-x86_64 tools/hostauth/digimacro-host.exe build/release/

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

# ---- the IR spike ------------------------------------------------------

APPVER_IR = 1

irspike: $(O)/irspike.hex $(O)/irspike-full.hex
$(O)/irspike.o: app/irspike.c app/hidusb.h core/core.h core/layout.h | $(O)
	$(CC) $(CFLAGS) -c -o $@ $<
$(O)/irspike.elf: $(O)/apphdrIR.o $(O)/irspike.o $(O)/hidusb.o $(O)/coreapi.o
	$(CC) $(CFLAGS) $(APPLINK) -o $@ $^
	@printf 'irspike: %d bytes, RAM to 0x%03X of 0x25F\n' \
	    $$(( 0x$$($(NM) $@ | awk '$$3=="__data_load_end"{print $$1}') - $(APP_START) )) \
	    $$(( 0x$$($(NM) $@ | awk '$$3=="_end"{print $$1}') - 0x800000 ))
$(O)/irspike.hex: $(O)/irspike.elf app/apphex.py
	$(apphex)
$(O)/irspike-full.hex: $(O)/core.hex $(O)/irspike.hex
	python3 app/apphex.py join $@ $^

# ---- the uninstaller ---------------------------------------------------

# Micronucleus, back where a Digispark keeps it: the uninstaller's image runs
# from APP_START to the end of it, and the core checks its CRC over the lot.
APPVER_UN = 1
MN_HEX    = app/micronucleus/t85_default.hex
MN_START  = 0x1A00

uninstall: $(O)/uninstall.hex
$(O)/uninstall.o: app/uninstall.c core/layout.h | $(O)
	$(CC) $(CFLAGS) -DMN_START=$(MN_START) -c -o $@ $<
$(O)/uninstall.elf: $(O)/apphdrUN.o $(O)/uninstall.o
	$(CC) $(CFLAGS) $(APPLINK) -o $@ $^
$(O)/uninstall.hex: $(O)/uninstall.elf app/apphex.py $(MN_HEX)
	$(OBJCOPY) -O binary -R .eeprom $< $(@:.hex=.bin)
	@printf 'uninstall: %d bytes of app before Micronucleus at %s\n' \
	    $$(stat -c %s $(@:.hex=.bin)) $(MN_START)
	python3 app/apphex.py app $(APP_START) $(APP_HDR) $(@:.hex=.bin) $@ $(MN_HEX)

# ---- spike: the core updater -------------------------------------------

# The core as the updater carries it: its pages up to its end, then its
# table, and how many pages that is, for app/coreupdate.S.
APPVER_CU = 1

coreupdate: $(O)/coreupdate.hex
$(O)/coreimg.bin: $(O)/core.elf $(O)/core.hex
	$(OBJCOPY) -I ihex -O binary --gap-fill 0xFF $(O)/core.hex $(O)/core-flash.bin
	@n=$$(( (0x$$($(NM) $< | awk '$$3=="__data_load_end"{print $$1}') + $(PAGE) - 1) / $(PAGE) )); \
	 echo "#define NCORE $$n" > $(O)/coreimg.h; \
	 head -c $$(( n * $(PAGE) )) $(O)/core-flash.bin > $@; \
	 tail -c $$(( $(APP_START) - $(CORE_TABLE) )) $(O)/core-flash.bin >> $@; \
	 echo "coreupdate: carries $$n pages of core and its table"
$(O)/coreupdate.o: app/coreupdate.S core/layout.h $(O)/coreimg.bin | $(O)
	$(CC) $(ASFLAGS) -I$(O) -Wa,-I$(O) -c -o $@ $<
$(O)/coreupdate.elf: $(O)/apphdrCU.o $(O)/coreupdate.o
	$(CC) $(CFLAGS) $(APPLINK) -o $@ $^
$(O)/coreupdate.hex: $(O)/coreupdate.elf app/apphex.py
	$(apphex)

# Tried on a simulated board: v3.0's core, the updater, and what is left.
$(O)/sim_coreupdate: test/sim/coreupdate.c $(SIMLIB) | $(O)
	cc -O2 -g -Wall -I$(SOFTUSB)/simavr/simavr/sim -I$(SOFTUSB)/harness -o $@ $< \
	    $(addprefix $(SOFTUSB)/harness/,board.c softusb.c dm.c touchpad.c) $(SIMLIB) -lelf -lm
simcoreupdate: $(O)/sim_coreupdate $(O)/core.elf $(O)/coreupdate.hex
	git show v3.0:web/firmware/digimacro-v2-core.hex > $(O)/core-v3.0.hex
	$(O)/sim_coreupdate $(O)/core.elf $(O)/core-v3.0.hex $(O)/coreupdate.hex $(O)/core-flash.bin

# A script, not something to build: without this, make's built-in rules
# would happily make apphex.py from apphex.py.o.
app/apphex.py: ;

$(O)/spike-page.html: app/spike.html $(O)/appA.hex $(O)/appB.hex $(O)/appC.hex
	python3 -c 'import json; \
	    apps = {n: open("$(O)/app%s.hex" % n).read() for n in "ABC"}; \
	    page = open("app/spike.html").read().replace("/*APPS*/{}", json.dumps(apps)); \
	    open("$@", "w").write(page)'
	@echo "$@: open it in Chrome"

# ---- the rest --------------------------------------------------------

size: $(O)/core.elf $(O)/digimacro.elf $(O)/hidusb.o
	$(SIZE) $^

test: $(O)/uninstall.hex
	$(MAKE) -C core/test
	cc -std=gnu11 -Wall -Wno-int-to-pointer-cast -Itest/uninstall -Icore \
	    -DMN_START=$(MN_START) -o $(O)/uninstall_test test/uninstall/uninstall_test.c
	$(O)/uninstall_test $(O)/uninstall.hex $(MN_HEX)

clean:
	rm -rf $(O)
	$(MAKE) -C core/test clean

.PHONY: all release site hosts spike page irspike uninstall coreupdate size test simtest simcoreupdate clean
.SECONDARY:
