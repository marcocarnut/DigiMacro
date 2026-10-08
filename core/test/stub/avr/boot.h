#pragma once
#define GET_LOW_FUSE_BITS 0
#define GET_HIGH_FUSE_BITS 3
#define GET_EXTENDED_FUSE_BITS 2
#define GET_LOCK_BITS 1
extern uint8_t FUSES[4];
#define boot_lock_fuse_bits_get(w) FUSES[w]
