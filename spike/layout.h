/*
 * The contract between the core and the app: where each lives, how each
 * calls the other, and what the core asks of an app before running it.
 *
 * Both halves are built against this file, and only this file joins them.
 * Once a core is on a board it can never change, so everything here is
 * forever for that board: add at the end, never move or renumber.
 *
 * The core is kept to what nothing else can do: be a USB device that
 * enumerates and says who it is, read flash and fuses, and write the app's
 * pages. Everything else -- time, the window, the password, typing, the
 * protocol -- is the app's, where it can be fixed by an update.
 *
 * Included from C and from assembler, hence only #defines.
 */
#ifndef LAYOUT_H
#define LAYOUT_H

/* ---- flash ---------------------------------------------------------- */

#define PAGE            64              /* SPM_PAGESIZE on the ATtiny85 */

/* The boundary. The core lives below it and can never be written from
   USB; the app lives above it and can be replaced whole. */
#ifndef APP_START
#define APP_START       0x1240
#endif

/* Where the app must stop. 0x1A00 leaves Micronucleus alone, which lets
   the spike go onto a stock Digispark by Micronucleus and still be
   rescued by it; a board with no bootloader could go to 0x2000. */
#ifndef APP_END
#define APP_END         0x1A00
#endif

/* ---- the app's header, at APP_START ---------------------------------
 *
 * A table of rjmps the core calls through, then what the core checks
 * before it will run any of it:
 *
 *   magic 'D','M'
 *   length    bytes from APP_START to the end of the image; a CRC-16 of
 *             those bytes follows them, little-endian
 *   version   the app's own, for INFO
 *   api       CORE_API as the app was built: a core runs no app built
 *             for another
 */
#define APP_SETUP       0               /* once, at boot, before USB */
#define APP_LOOP        1               /* over and over */
#define APP_REQUEST     2               /* a request the core does not
                                           answer alone: see core.h */
#define APP_MAYUPDATE   3               /* may the core take pages? */
#define APP_VECTORS     4               /* the chip's interrupts 1..14,
                                           in order; PCINT0's is V-USB's
                                           and its slot is never used */
#define APP_NVECTORS    14
#define APP_SLOTS       (APP_VECTORS + APP_NVECTORS)

#define APP_MAGIC       (APP_START + 2 * APP_SLOTS)
#define APP_LENGTH      (APP_MAGIC + 2)
#define APP_VERSION     (APP_MAGIC + 4)
#define APP_API         (APP_MAGIC + 5)
#define APP_CODE        (APP_MAGIC + 6) /* where the app's own code starts */

#define APP_MAGIC0      0x44            /* 'D', as a number for the assembler */
#define APP_MAGIC1      0x4D            /* 'M' */

/* ---- the core's table, just below the boundary ----------------------
 *
 * The app calls the core through these rjmps, never directly, so that an
 * app does not care how the core inside was laid out -- only that it is
 * this API. Sixteen slots; the unused ones return at once. */
#define CORE_SLOTS      16
#define CORE_TABLE      (APP_START - 2 * CORE_SLOTS)
#define CORE_API        1

/* slot, name -- add at the end, never renumber */
#define CORE_CALLS(X)                   \
    X(0, hidPoll)                       \
    X(1, hidReady)                      \
    X(2, hidAwake)                      \
    X(3, hidSend)                       \
    X(4, hidRestart)                    \
    X(5, hidReport)
#define CORE_USED       6

/* ---- RAM ------------------------------------------------------------
 *
 * The core's variables go from the bottom of RAM; the app's start here;
 * the stack, shared, comes down from the top. The core's build checks
 * that it ends below this. */
#define APP_RAM         0x0150

/* ---- the verbs the core answers ------------------------------------- */

#define DM_FUNC_INFO    0x40    /* who this is; the app adds its part */
#define DM_FUNC_PAGE    0x65    /* address, 32 bytes: half a page. The
                                   second half writes it. */
#define DM_FUNC_FREAD   0x66    /* address, count -> flash */
#define DM_FUNC_COMMIT  0x67    /* check the new app, and start it */

/* INFO, as the core fills it. [5..7] are the app's: EEPROM size and v1's
   state byte, both about the app's business. */
#define DM_INFO_PROTO   3       /* [3] DM_PROTOCOL_VERSION */
#define DM_INFO_COREVER 4       /* [4] DM_CORE_VERSION */
#define DM_INFO_FUSES   8       /* [8..11] low, high, extended, lock */
#define DM_INFO_CORE    12      /* [12] core state, below */
#define DM_INFO_APPVER  13      /* [13] the app's version, 0 if none */
#define DM_INFO_API     14      /* [14] CORE_API */
#define DM_INFO_START   15      /* [15..16] APP_START */
#define DM_INFO_END     17      /* [17..18] APP_END */
#define DM_INFO_PAGE    19      /* [19] PAGE */

#define DM_CORE_APP     0x01    /* a valid app is running */
#define DM_CORE_UPDATE  0x02    /* taking pages: the app is gone */

#endif /* LAYOUT_H */
