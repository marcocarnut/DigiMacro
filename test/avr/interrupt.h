/* Stub: an interrupt is a function the harness calls, and the registers it
   reads are bytes the harness sets. One translation unit per test, so
   static is enough. */
#pragma once
#include <stdint.h>
#define ISR(vector, ...)  void vector(void)
#define INT0_vect         isr_int0
#ifndef F_CPU
#define F_CPU             16500000UL
#endif
static uint8_t TCNT0, TIFR, TCCR0A, TCCR0B, MCUCR, GIMSK;
#define TOV0  1
#define CS00  0
#define CS02  2
#define ISC01 1
#define INT0  6
