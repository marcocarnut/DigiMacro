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
    rjmp    hidPassSet
    rjmp    hidUnlocked
    rjmp    hidUnlock
    rjmp    hidSetPass
    rjmp    hidLockState
    rjmp    hidHostState
    rjmp    hidHostForget
    rjmp    hidHostAuth
    .if (. - __vectors) != 2 * APP_SLOTS
    .error "the header's table does not match APP_SLOTS"
    .endif
    .byte   APP_MAGIC0, APP_MAGIC1
    .word   __data_load_end - APP_START /* the CRC goes here, after the build */
    .byte   APP_VER, CORE_API

    .section .init0,"ax",@progbits
__app_init:
    push    r17

    .section .init9,"ax",@progbits
    pop     r17
    rjmp    app_setup
