#pragma once
#include <stdint.h>
void eeprom_update_byte(uint8_t *a, uint8_t v);
void eeprom_busy_wait(void);
