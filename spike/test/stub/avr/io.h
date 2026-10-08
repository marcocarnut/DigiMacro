/* The registers core.c touches, as plain variables. */
#pragma once
#include <stdint.h>
extern uint8_t SREG, OCR1C, OCR1A, TCCR1, TIMSK, OSCCAL, TCCR0B, TCNT0,
               WDTCR, MCUSR, GIMSK, GIFR;
#define CTC1 7
#define CS13 3
#define OCIE1A 6
#define CS00 0
#define WDCE 4
#define WDE 3
#define WDIF 7
#define WDIE 6
#define E2END 511
#define FLASHEND 8191
