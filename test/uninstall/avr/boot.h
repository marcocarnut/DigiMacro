#pragma once
#include <stdint.h>
void boot_page_erase(uint16_t a);
void boot_page_fill(uint16_t a, uint16_t w);
void boot_page_write(uint16_t a);
#define boot_spm_busy_wait() ((void)0)
