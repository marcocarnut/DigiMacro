/* Stub of the library header, for the host build. Shadows DigiHidUSB/src
   because the test compiles with -I. ahead of anything else. */
#pragma once
#include <stdint.h>
void    hidBegin(void);
void    hidPoll(void);
uint8_t hidReady(void);
uint8_t hidAwake(void);
void    hidAllow(uint16_t ms);
uint8_t hidAllowed(void);
#define DM_PASS_ADDR   0x1F4
#define DM_PASS_LEN    12
#define DM_SERIAL_ADDR 0x1E0
#define DM_ST_SOFTLOCK 0x08
#define DM_ST_HARDLOCK 0x10
#define DM_ST_HOST     0x20
#define DM_ST_HOSTKEY  0x40
#define DM_HOST_ADDR   0x1E4
#define DM_HOST_LEN    8
void    hidKey(uint8_t modifier, uint8_t key);
void    hidTypeChar(char c, uint8_t mod);

/* The core's password (core/core.h), which the sketch asks but no longer
   keeps. Each test that compiles the sketch provides them. */
#define DM_PASS_WRONG     0
#define DM_PASS_RIGHT     1
#define DM_PASS_UNCHECKED 2
uint8_t hidPassSet(void);
uint8_t hidUnlocked(void);
uint8_t hidUnlock(const uint8_t *p, uint8_t n);
void    hidLock(void);

/* ...and what the sketch provides the app library in their place. */
void    hidPassWrong(void);
void    hidPassRight(void);
void    hidPassChanged(void);
