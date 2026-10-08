/*
 * The app's header, at APP_START: the table the core calls through, then
 * what the core checks before running any of it (layout.h).
 *
 * The app is linked with no C runtime of its own -- the core already set up
 * the stack and the registers -- so its first slot runs the .init sections
 * by hand: whatever copies .data and clears .bss lands in .init4, and .init9
 * hands over to app_setup(). libgcc's copy loop uses r17, which the core's
 * C code expects kept, hence the push and the pop around it.
 */
#include "layout.h"

    .section .vectors,"ax",@progbits
    .global __vectors
__vectors:
    rjmp    __app_init          /* APP_SETUP */
    rjmp    app_loop            /* APP_LOOP */
    rjmp    app_request         /* APP_REQUEST */
    /* APP_VECTORS: the chip's interrupts 1..14, as the core passes them on.
       An app's ISR() overrides the weak default, which only returns. */
#define VEC(n) .weak __vector_##n $ .set __vector_##n, __app_reti $ rjmp __vector_##n $
    VEC(1)  VEC(2)  VEC(3)  VEC(4)  VEC(5)  VEC(6)  VEC(7)
    VEC(8)  VEC(9)  VEC(10) VEC(11) VEC(12) VEC(13) VEC(14)
    .if (. - __vectors) != 2 * APP_SLOTS
    .error "the header's table does not match APP_SLOTS"
    .endif
    .byte   APP_MAGIC0, APP_MAGIC1
    .word   __data_load_end - APP_START /* the CRC goes here, after the build */
    .byte   APP_VER, CORE_API

__app_reti:
    reti

    .section .init0,"ax",@progbits
__app_init:
    push    r17

    .section .init9,"ax",@progbits
    pop     r17
    rjmp    app_setup
