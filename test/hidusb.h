/* Stub of the library header, for the host build. Shadows DigiHidUSB/src
   because the test compiles with -I. ahead of anything else. */
#pragma once
#include <stdint.h>
#define HID_REPORT_KBD 1
#define HID_REPORT_VND 2
void    hidBegin(void);
void    hidPoll(void);
uint8_t hidReady(void);
uint8_t hidAwake(void);
uint8_t hidLeds(void);
void    hidAllow(uint16_t ms);
uint8_t hidAllowed(void);
#define DM_PASS_ADDR   0x1F4
#define DM_PASS_LEN    12
#define DM_SERIAL_ADDR 0x1E0
#define DM_ST_SOFTLOCK 0x08
#define DM_ST_HARDLOCK 0x10
void    hidKey(uint8_t modifier, uint8_t key);
void    hidTypeChar(char c, uint8_t mod);
void    hidType_P(const char *s);
