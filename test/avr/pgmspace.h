#pragma once
#include <string.h>
#define PROGMEM
#define PSTR(x) (x)
#define pgm_read_byte(p) (*(const char *)(p))
#define pgm_read_word(p) (*(p))
#define strcmp_P  strcmp
#define strncmp_P strncmp
