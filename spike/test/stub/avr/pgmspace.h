#pragma once
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#include <string.h>
#define memcpy_P memcpy
