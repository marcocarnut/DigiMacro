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
