#ifndef CLIMATE_H
#define CLIMATE_H

#include <U8g2lib.h>

struct rtc_time {
  struct {
    byte second;        // Register 0x00: second, 7 bits, bcd 0-59
    byte minute;        // Register 0x01: minute, 7 bits, bcd 0-59
    byte hour;          // Register 0x02: hour,   7 bits, bcd 1-12/0-23 + am/pm + mode
  } t;
  struct {
    byte day;           // Register 0x03: day,    3 bits, bcd 1-7
    byte date;          // Register 0x04: date,   6 bits, bcd 1-31
    byte month;         // Register 0x05: month,  6+1 bits, bcd 1-12 + century
    byte year;          // Register 0x06: year,   8 bits, 0-99
  } d;
};

extern struct rtc_time time;
extern bool inDST;
extern int temperature;
extern int historical_weekly_stats[8];
extern char _buf[128];
extern U8G2_SH1106_128X64_NONAME_1_HW_I2C oled;

void info_screen(int edit_sel);
void oled_drawStr(int x, int y, const char *s);
void oled_drawStr(int x, int y, const __FlashStringHelper *f);

#endif // CLIMATE_H
