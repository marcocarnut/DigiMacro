#pragma once
#define wdt_disable() ((void)0)
extern int wdtResets, wdtEnabled;
#define wdt_reset()   (wdtResets++)
#define wdt_enable(t) (wdtEnabled = 1)
#define WDTO_2S 7
#define WDTO_8S 9
