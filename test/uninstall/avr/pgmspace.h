#pragma once
#include <stdint.h>
extern uint8_t FL[8192];
#define pgm_read_word(a) ((uint16_t)(FL[(a)] | FL[(a) + 1] << 8))
