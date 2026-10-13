/* An app that hangs: its loop never returns, so it never calls corePoll()
   and the core's watchdog has to step in (test/sim/hang.c). */
    .text
    .global app_setup, app_loop, app_request
app_setup:
    ret
app_loop:
    rjmp    app_loop
app_request:
    clr     r24
    ret
