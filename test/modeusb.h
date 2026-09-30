/* Stub of the library header, for the host build. Shadows DigiModeUSB/src
   because the test compiles with -I. ahead of anything else. */
#pragma once
#include <stdint.h>
#define DUSB_HID    0
#define DUSB_VENDOR 1
void dusbBegin(uint8_t);  void dusbPoll(void);
uint8_t dusbMode(void);   void dusbSetMode(uint8_t);
uint8_t dusbReady(void);
void dusbKey(uint8_t, uint8_t);
void dusbTypeChar(char, uint8_t);
void dusbType(const char*); void dusbType_P(const char*);
