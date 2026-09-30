#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
extern uint8_t PORTB, DDRB;
/* The ATtiny85's three general-purpose I/O registers. Real ones live below
   0x20, so the compiler reaches them with SBI/CBI; here they are just bytes. */
extern uint8_t GPIOR0, GPIOR1, GPIOR2;
extern unsigned long fake_ms;
static inline unsigned long millis(void){ return fake_ms++; }

/* PINB is driven by the harness rather than being a plain variable, so a
   discharge can be simulated. touch_once() calls _delay_us() exactly once,
   right before its counting loop, so hooking that arms the countdown. */
uint8_t fake_pinb(void);
void    fake_charge(void);
#define PINB fake_pinb()
