#include <Wire.h>

#include "climate.h"

void rtc_read(byte addr, void *ptr, byte n) {
  // LOGF("rtc_read addr=0x%x ptr=0x%x n=%u\n", addr, ptr, n);
  byte *buf = (byte *)ptr;
  Wire.beginTransmission(0x68);   // Start I2C protocol with DS3231 address
  Wire.write(addr);               // Send register address
  Wire.endTransmission(false);    // I2C restart
  Wire.requestFrom(0x68, (int)n); // Request n bytes from DS3231 and release I2C bus at end of reading
  while (n > 0) {
    *buf = Wire.read();
    // LOGF("  byte=0x%x\n", *buf);
    buf++;
    n--;
  }
}

void rtc_write(byte addr, void *ptr, byte n) {
  byte *buf = (byte *)ptr;
  Wire.beginTransmission(0x68);  // Start I2C protocol with DS3231 address
  Wire.write(addr);              // Send register address
  while (n > 0) {
    Wire.write(*buf);
    buf++;
    n--;
  }
  Wire.endTransmission();        // Stop transmission and release the I2C bus
  delay(200);                    // Wait 200ms
}

void rtc_read_all() {
  rtc_read(0x00, &time, sizeof(time));
}

void rtc_write_all() {
  rtc_write(0x00, &time, sizeof(time));
}

byte rtc_cur_day()    { return time.d.day; }
byte rtc_cur_date()   { return (time.d.date >> 4)   * 10 + (time.d.date & 0x0F); }
byte rtc_cur_month()  { return ((time.d.month & 0x10) >> 4) * 10 + (time.d.month & 0x0F); }
byte rtc_cur_year2()   { return (time.d.year >> 4)   * 10 + (time.d.year & 0x0F); }
byte rtc_cur_hour()   { return (time.t.hour >> 4)   * 10 + (time.t.hour & 0x0F); }
byte rtc_cur_minute() { return (time.t.minute >> 4) * 10 + (time.t.minute & 0x0F); }
byte rtc_cur_second() { return (time.t.second >> 4) * 10 + (time.t.second & 0x0F); }

void rtc_set_day(byte d)    { time.d.day = d & 0x7; }
void rtc_set_date(byte d)   { time.d.date = ((d / 10) << 4) + (d % 10); }
void rtc_set_month(byte m)  { time.d.month = 0x80 | (((m / 10) << 4) + (m % 10)); }
void rtc_set_year2(byte y)   { time.d.year = ((y / 10) << 4) + (y % 10); }
void rtc_set_hour(byte h)   { time.t.hour = ((h / 10)  << 4) + (h % 10); }
void rtc_set_minute(byte m) { time.t.minute = ((m / 10) << 4) + (m % 10); }
void rtc_set_second(byte s) { time.t.second = ((s / 10) << 4) + (s % 10); }
