/* XTEA, 32 cycles, encrypt only: v is two little-endian 32-bit words,
   encrypted in place under the sixteen bytes of key -- as app/xtea.S does
   on the board and web/table.js on the page. digimacro-host's, and the PC
   tests' in place of the assembly (test/xtea.h), so that what checks the
   firmware is what the client runs. Checked against the published vector
   and the spike's (build/cipher-spike). */
#ifndef XTEA_REF_H
#define XTEA_REF_H
#include <stdint.h>

static uint32_t xtea_le(const uint8_t *b)
{
    return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24;
}

static void xtea(uint8_t v[8], const uint8_t key[16])
{
    uint32_t v0 = xtea_le(v), v1 = xtea_le(v + 4), sum = 0, k[4];
    for (int i = 0; i < 4; i++) k[i] = xtea_le(key + 4 * i);
    for (int i = 0; i < 32; i++) {
        v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + k[sum & 3]);
        sum += 0x9E3779B9;
        v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + k[(sum >> 11) & 3]);
    }
    for (int i = 0; i < 4; i++) { v[i] = v0 >> (8 * i); v[i + 4] = v1 >> (8 * i); }
}
#endif
