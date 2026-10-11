#pragma once
#define WDTO_15MS 0
void wdt_reset(void);
void wdt_enable(int t);                 /* the reset: never returns */
