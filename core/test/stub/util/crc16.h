#pragma once
#include <stdint.h>
static inline uint16_t _crc16_update(uint16_t crc, uint8_t a)
{
    crc ^= a;
    for (int i = 0; i < 8; i++) crc = crc & 1 ? (crc >> 1) ^ 0xA001 : crc >> 1;
    return crc;
}
