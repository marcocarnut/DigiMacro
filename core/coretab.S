/*
 * The core's table: one rjmp per call an app may make, at CORE_TABLE, just
 * below the boundary. An app calls the slot, never the function, so a core
 * can be laid out any way inside as long as the slots stay put.
 *
 * '$' separates statements on one line in avr-as, which is what lets the
 * list in layout.h expand into code here and into addresses in coreapi.S.
 */
#include "layout.h"

    .section .coretab,"ax",@progbits
    .global coretab
coretab:
#define X(n, f) .if (. - coretab) != 2 * (n) $ .error "CORE_CALLS out of order" $ .endif $ rjmp f $
    CORE_CALLS(X)
    .if (. - coretab) != 2 * CORE_USED
    .error "CORE_USED does not match CORE_CALLS"
    .endif
    .rept CORE_SLOTS - CORE_USED        /* not yet: return at once */
    ret
    .endr

/*
 * Every interrupt but V-USB's, passed on to the app's header: the app's
 * timer is the app's, and so is anything else it wants. Two rjmps more
 * than a direct vector, which costs a few cycles; the core never turns any
 * of these on itself, and turns them all off before an update (quiet()).
 */
    .section .text.vectors,"ax",@progbits
/* __app_vectors is the linker's (the Makefile's --defsym): an rjmp to a
   plain number would be taken as an offset, not an address. */
#define FWD(n) .global __vector_##n $ __vector_##n: rjmp __app_vectors + 2 * ((n) - 1) $
    FWD(1)          FWD(3)  FWD(4)  FWD(5)  FWD(6)  FWD(7)
    FWD(8)  FWD(9)  FWD(10) FWD(11) FWD(12) FWD(13) FWD(14)
