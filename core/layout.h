/*
 * The contract between the core and the app: where each lives, how each
 * calls the other, and what the core asks of an app before running it.
 *
 * Both halves are built against this file, and only this file joins them.
 * Once a core is on a board it can never change, so everything here is
 * forever for that board: add at the end, never move or renumber.
 *
 * The core is kept to what nothing else can do: be a USB device that
 * enumerates and says who it is, read flash and fuses, write the app's
 * pages -- and check the password, since it is what guards those pages even
 * when there is no app to ask. Everything else -- time, the window, setting
 * the password, typing, the protocol -- is the app's, where it can be fixed
 * by an update.
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
#define APP_START       0x0F80          /* two pages under half way: room for
                                           bugfixes in the core, and in the
                                           app, which had run out */
#endif

/* Where the app must stop: the end of the chip, since a v2 board has no
   bootloader. The spike's builds stop at 0x1A00 instead (`make spike`),
   leaving Micronucleus alone, which lets them go onto a stock Digispark and
   be rescued by it. DigiMacro itself does not fit below 0x1A00. */
#ifndef APP_END
#define APP_END         0x2000
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
#define APP_VECTORS     3               /* the chip's interrupts 1..14,
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
#ifndef CORE_API                        /* the core updater says 1: see Makefile */
#define CORE_API        2               /* 2: the boundary at 0x0F80, not 0x1000 */
#endif

/* slot, name -- add at the end, never renumber */
#define CORE_CALLS(X)                   \
    X(0, corePoll)                      \
    X(1, hidReady)                      \
    X(2, hidAwake)                      \
    X(3, hidSend)                       \
    X(4, hidRestart)                    \
    X(5, hidReport)                     \
    X(6, hidPassSet)                    \
    X(7, hidUnlock)                     \
    X(8, hidUnlocked)                   \
    X(9, hidLock)
#define CORE_USED       10

/* ---- RAM ------------------------------------------------------------
 *
 * The core's variables go from the bottom of RAM; the app's start here;
 * the stack, shared, comes down from the top. The core's build checks
 * that it ends below this. */
#define APP_RAM         0x0150

/* ---- the update window ----------------------------------------------
 *
 * Micronucleus takes an update only in the seconds after a board is
 * plugged in; this keeps that. GPIOR2 counts tenths of a second, stopping
 * at 255: the core zeroes it at boot, the app's clock counts it, and the
 * app zeroes it again to open the window when its owner asks to update.
 * While an app runs, the first page is taken only within the window -- and
 * only with the password, if one is set. An app that never counts leaves
 * the window open: it fails open, never shut. With no app there is no
 * window, only the password. */
#define DM_WINDOW       GPIOR2
#define DM_WINDOW_TENTHS 60             /* six seconds */

/* ---- the verbs the core answers ------------------------------------- */

#define DM_FUNC_INFO    0x40    /* who this is; the app adds its part */
#define DM_FUNC_UNLOCK  0x62    /* count, password: the core's with no app,
                                   the app's (calling the core) with one */
#define DM_FUNC_PAGE    0x65    /* address, 32 bytes: half a page. The
                                   second half writes it. */
#define DM_FUNC_FREAD   0x66    /* address, count -> flash */
#define DM_FUNC_COMMIT  0x67    /* check the new app, and start it */

/* INFO, as the core fills it. [5..6] are the app's, and so is [7], v1's
   state byte, but for the two bits about the password: see core.h. */
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
#define DM_CORE_HALTED  0x04    /* an app is there, but the watchdog reset
                                   the board while it ran, so it was not
                                   started again: core only, until a replug
                                   or an update */

#endif /* LAYOUT_H */
