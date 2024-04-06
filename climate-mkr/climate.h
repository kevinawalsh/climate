#ifndef CLIMATE_H
#define CLIMATE_H

#include <U8g2lib.h>

// DS3231 is configured as 24 hour mode to ease comparisons.
struct rtc_time {
  struct {
    byte second;  // Register 0x00: second, 7 bits, bcd 0-59
    byte minute;  // Register 0x01: minute, 7 bits, bcd 0-59
    byte hour;    // Register 0x02: hour,   7 bits, bcd 1-12/0-23 + am/pm + mode
  } t;
  struct {
    byte day;    // Register 0x03: day,    3 bits, bcd 1-7
    byte date;   // Register 0x04: date,   6 bits, bcd 1-31
    byte month;  // Register 0x05: month,  6+1 bits, bcd 1-12 + century
    byte year;   // Register 0x06: year,   8 bits, 0-99
  } d;
};

struct rtc_temp {
  byte msb;           // Register 0x11: temperature msb, 8 bits
  // byte lsb;           // Register 0x12: temperature lsb, 8 bits
};

extern byte fan_speed;

#define INVALID_TEMP (-3000)

extern struct rtc_time time;
extern int baseline10, temperature10, old_temperature10, circuit_temperature10;
extern int sim_baseline10, sim_temperature10, sim_old_temperature10;
extern int sim_day, sim_time;
extern int variation_index;
extern char _buf[128];
extern U8G2_SH1106_128X64_NONAME_1_HW_I2C oled;

struct colors {
  float txx; // -30.0 to +30.0 is valid, anything else is invalid
  byte rgb[3];
};
extern struct colors manual_settings[3];

void info_screen(int edit_sel);
void manual_screen();
void sim_screen();
void oled_drawStr(int x, int y, const char *s);
void oled_drawStr(int x, int y, const __FlashStringHelper *f);

char *fmt_time(char *str);
char *fmt_date(char *str);

void rtc_read_all();
void rtc_write_all();
void rtc_read(byte addr, void *ptr, byte n);

byte rtc_cur_day();     // 1 - 7
byte rtc_cur_date();    // 1 - 31
byte rtc_cur_month();   // 1 - 12
byte rtc_cur_year2();   // 0 - 99
byte rtc_cur_hour();    // 0 - 23
byte rtc_cur_minute();  // 0 - 59
byte rtc_cur_second();  // 0 - 59

void rtc_set_day(byte d);     // 1 - 7
void rtc_set_date(byte d);    // 1 - 31
void rtc_set_month(byte m);   // 1 - 12
void rtc_set_year2(byte y);   // 0 - 99
void rtc_set_hour(byte h);    // 0 - 23
void rtc_set_minute(byte m);  // 0 - 59
void rtc_set_second(byte s);  // 0 - 59

struct hist_data {
  float maxavg;
  float future_txx;
  byte maxdaily[24]; // 1950 - 1970 daily max, plus padding
};
extern const struct hist_data historical[] PROGMEM;
extern const char bedford_historical[] PROGMEM;

extern int month_offset[];
extern char wifi_ssid[];
extern int opening_time, closing_time;

#endif  // CLIMATE_H
