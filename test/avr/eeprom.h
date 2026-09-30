#pragma once
#include <stdint.h>
#include <stddef.h>
extern uint8_t EE[512];
static inline uint8_t  eeprom_read_byte(const uint8_t *a){ return EE[(size_t)a]; }
static inline void     eeprom_update_byte(uint8_t *a, uint8_t v){ EE[(size_t)a]=v; }
static inline uint16_t eeprom_read_word(const uint16_t *a){ size_t o=(size_t)a; return EE[o]|(EE[o+1]<<8); }
static inline void     eeprom_update_word(uint16_t *a, uint16_t v){ size_t o=(size_t)a; EE[o]=v; EE[o+1]=v>>8; }
