#pragma once
#include <stdint.h>
#include <stddef.h>
extern uint8_t EE[512];
static inline uint8_t eeprom_read_byte(const uint8_t *a) { return EE[(size_t)a]; }
static inline void eeprom_update_byte(uint8_t *a, uint8_t v) { EE[(size_t)a] = v; }
static inline uint32_t eeprom_read_dword(const uint32_t *a)
{ size_t o = (size_t)a; return EE[o] | EE[o+1] << 8 | EE[o+2] << 16 | (uint32_t)EE[o+3] << 24; }
static inline void eeprom_write_dword(uint32_t *a, uint32_t v)
{ size_t o = (size_t)a; for (int i = 0; i < 4; i++) EE[o+i] = v >> (8*i); }
