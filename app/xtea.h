/* XTEA, 32 cycles, encrypt only (app/xtea.S): v is two little-endian
   32-bit words, encrypted in place under the sixteen bytes of key. */
#ifndef XTEA_H
#define XTEA_H
#include <stdint.h>
#ifdef __cplusplus
extern "C"
#endif
void xtea(uint8_t v[8], const uint8_t key[16]);
#endif
