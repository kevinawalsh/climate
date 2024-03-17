#include <Wire.h>
#include <U8glib.h>
#include <DS18B20.h>
#include "SdFat.h"

#define VERSION "0.1"

#define DEFAULT_OPENING_TIME (7*60+30)  // default: lights on at 7:30 AM
#define DEFAULT_CLOSING_TIME (19*60+30)  // default: lights off at 7:30 AM

// Arduino Hardware Configuration
// ------------------------------

// OneWire bus for Dallas Semiconductor DS18B20 Temperature Sensor
#define ONEWIRE_PIN 9 // pin 9 is OneWire data, with external pullup

// MicroSD Card
#define SPI_SPEED SD_SCK_MHZ(4)
#define MICROSD_CS_PIN  10 // pin 10 for MicroSD Card CS (ChipSelect)
#define MICROSD_DI_PIN  11 // pin 11 for MicroSD Card DI (MOSI / Data Input)
#define MICROSD_DO_PIN  12 // pin 12 for MicroSD Card DO (MISO / Data Output)
#define MICROSD_SCK_PIN 13 // pin 13 for MicroSD Card SCK (Serial Clock)

// Push-buttons
#define BUTTON_OK_PIN A0 // pin A0 for push-button, with internal pullup
#define BUTTON_UP_PIN A1 // pin A1 for push-button, with internal pullup
#define BUTTON_DN_PIN A2 // pin A2 for push-button, with internal pullup

// i2c bus for 128x64 OLED display and DS3231/DS1307 Real Time Clock module
#define I2C_BUS_SDA_PIN A4 // pin A4 for i2c bus SDA (serial data)
#define I2C_BUS_SCL_PIN A5 // pin A5 for i2c bus SCL (serial clock)

// RGB LED Drivers
// FIXME: Use shift register or charlieplexing for fewer pins.
// #define STRIP1_RGB_PIN  ?  // pins ?, ?, ? for R, G, B of strip 1
// #define STRIP1_RGB_PING ?  // pins ?, ?, ? for R, G, B of strip 1
// #define STRIP1_RGB_PINB ?  // pins ?, ?, ? for R, G, B of strip 1
#define STRIP2_RGB_PIN  3  // pins 3, 4, 5 for R, G, B of strip 2
#define STRIP2_RGB_PING 4  // pins 3, 4, 5 for R, G, B of strip 2
#define STRIP2_RGB_PINB 5  // pins 3, 4, 5 for R, G, B of strip 2
#define STRIP3_RGB_PIN  6  // pins 3, 4, 5 for R, G, B of strip 3
#define STRIP3_RGB_PING 7  // pins 3, 4, 5 for R, G, B of strip 3
#define STRIP3_RGB_PINB 8  // pins 3, 4, 5 for R, G, B of strip 3

// Debugging and Constants
// -----------------------

#define DEBUG 1
#if DEBUG
#define DBG(code) code
#else
#define DBG(code) do {} while (0)
#endif
#define LOG(msg) DBG(Serial.print(msg))
#define FLOG(msg) DBG(Serial.print(F(msg)))
#define LOGF(msg...) DBG(serial_printf(msg))

#define MAGIC 0xab

#define MODE_NORMAL 0
#define MODE_MANUAL 1
#define MODE_DIAGNOSTICS 2

#define STATUS_RTC_WORKING   (1 << 0) // DS3231 RTC module appears to be working
#define STATUS_SENSOR_FOUND  (1 << 1) // DS18B20 Temperature Sensor was found
#define STATUS_SDCARD_FOUND  (1 << 2) // MicroSD Card was found
#define STATUS_DAILY_FOUND   (1 << 3) // daily.csv file found
#define STATUS_DAILY_READY   (1 << 4) // daily.csv appears readable and in-tact
#define STATUS_READY         (0x1f)
#define STATUS_ERROR         (1 << 5) // something is wrong

#define DEGREES "\xb0" // degree symbol in u8g_font_6x10

#define RESET_CONFIRM_TIMEOUT 10000

// DS3231 RTC register map and usage
// ---------------------------------

// DS3231 is configured as 24 hour mode to ease comparisons.
struct rtc_time {
  struct {
    byte second;        // Register 0x00: second, 7 bits, bcd 0-59
    byte minute;        // Register 0x01: minute, 7 bits, bcd 0-59
    byte hour;          // Register 0x02: hour,    7 bits, bcd 0-12/0-23 + am/pm + mode
  } t;
  struct {
    byte day;           // Register 0x03: day,     3 bits, bcd 1-7
    byte date;          // Register 0x04: date,    6 bits, bcd 1-31
    byte month;         // Register 0x05: month,   6+1 bits, bcd 1-12 + century
    byte year;          // Register 0x06: year,    8 bits, 0-99
  } d;
};

struct rtc_settings {
  byte rgb12;         // Register 0x07: alarm 1 data, 8 bits, repurposed, rgb manual-mode status for strips 1 and 2, xxRGBRGB
  byte rgb3;          // Register 0x08: alarm 1 data, 8 bits, repurposed, rgb manual-mode status for strips 1 and 2, xxxxxRGB
  byte opening_time;  // Register 0x09: alarm 1 data, 8 bits, repurposed, unsigned opening time, units of 15 minutes after midnight, 0-96
  byte closing_time;  // Register 0x0a: alarm 1 data, 8 bits, repurposed, unsigned closing time, units of 15 minutes after midnight, 0-96
  byte mode;          // Register 0x0b: alarm 2 data, 8 bits, repurposed, 2 bits for mode:
                      //                            0=normal, 1=manual, 2=diagnostics
  byte dstadjust;     // Reigster 0x0c: alarm 2 data, 8 bits, repurposed, 1 bit for dst:
                      //                            0=Do not adjust display for daylight savings time, 1=do adjust for dst
  byte magic;         // Register 0x0d: alarm 2 data, 8 bits, repurposed, magic value
};

struct rtc_temp {
  byte msb;           // Register 0x11: temperature msb, 8 bits
  // byte lsb;           // Register 0x12: temperature lsb, 8 bits
};

// Global Variables
// ----------------

SdFat sd; // MicroSD card

DS18B20 sensors(ONEWIRE_PIN); // Temperature Sensors

U8GLIB_SH1106_128X64  oled(U8G_I2C_OPT_NONE); // 2c oled using A4=SDA and A5=SCL

uint16_t openTime, closeTime; // minutes after midnight
struct rtc_time time; // current time
struct rtc_settings settings; // saved settings
byte status = 0;
bool inDST; // whether daylight savings time is active
// all temperatures are in F, tenths of a degree
int temperature; // current tempeature, from DS18B20 sensor (primary) or RTC module (secondary)
int historical_weekly_min[21];   // historical minimum temperature for this week, by year, 1950-1970
int historical_weekly_max[21];   // historical maximum temperature for this week, by year, 1950-1970
int historical_weekly_min_1950s; // average historical minimum temperature for this week, 1950-1959
int historical_weekly_min_1960s; // average historical minimum temperature for this week, 1960-1969
int historical_weekly_min_1970;  // average historical minimum temperature for this week, 1970
int historical_weekly_min_avg;   // average historical minimum temperature for this week, 1950-1970
int historical_weekly_max_1950s; // average historical minimum temperature for this week, 1950-1959
int historical_weekly_max_1960s; // average historical minimum temperature for this week, 1960-1969
int historical_weekly_max_1970;  // average historical minimum temperature for this week, 1970
int historical_weekly_max_avg;   // average historical maximum temperature for this week, 1950-1970
File csv;
char _buf[64];

#if DEBUG
char _printf_buf[64];
void serial_printf(const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  vsnprintf(_printf_buf, sizeof(_printf_buf), fmt, va);
  va_end(va);
  _printf_buf[sizeof(_printf_buf)-1] = '\0';
  Serial.print(_printf_buf);
}
#endif

// Boot Screen

struct boot_msgs {
    int ds_model;
    byte ds_addr[8];
    int sd_card;
    int32_t sd_size_kb;
    int32_t csv_size;
    uint16_t csv_date, csv_time;
};

//   +----------------------+
//   | v1.1, [111222333444] |
//   | Sensor: DS18B20      |
//   |     2803005704e13c2b |
//   | MicroSD: SDHC 7.3GB  |
//   | daily.csv: 35122 B   |
//   |  2024-03-16 18:57:34 |
//   +----------------------+

void boot_screen(struct boot_msgs *boot) {
  LOGF("Status: %x\n", status);
  char *msg = _buf;
  int cap = sizeof(_buf);
  oled.firstPage(); do {
    oled.drawStr(0, 0, F("v" VERSION ", ["));
    oled.drawStr(6*20, 0, "]");
    if (status == 0) {
      oled.drawStr(6*8, 0, F("booting"));
    } else if (status & STATUS_ERROR) {
      oled.drawStr(6*8, 0, F("error"));
    } else if (status == STATUS_READY) {
      oled.drawStr(6*8, 0, F("ready"));
    } else {
      msg[0] = msg[1] = msg[2] = (status & STATUS_RTC_WORKING) ? '#' : '?';
      msg[3] = msg[4] = msg[5] = (status & STATUS_SENSOR_FOUND) ? '#' : '?';
      msg[6] = msg[7] = msg[8] = (status & STATUS_SDCARD_FOUND) ? '#' : '?';
      msg[9] = msg[10] = msg[11] = (status & STATUS_DAILY_FOUND) ? '#' : '?';
      msg[12] = 0;
      oled.drawStr(6*7, 0, msg);
    }
    oled.drawStr(0, 10, F("Sensor:"));
    oled.drawStr(6*8, 10,
        boot->ds_model == -1 ? F("-") :
        boot->ds_model == MODEL_DS18S20 ? F("DS18S20") :
        boot->ds_model == MODEL_DS1822 ? F("DS1822") :
        boot->ds_model == MODEL_DS18B20 ? F("DS18B20") : F("unknown"));
    if (boot->ds_model != -1) {
      for (int i = 0; i < 7; i++) {
        snprintf(msg, cap, "%02x", boot->ds_addr[i]);
        oled.drawStr(6*(4+2*i), 20, msg);
      }
    }
    oled.drawStr(0, 30, "MicroSD:");
    if (boot->sd_card == -4) {
      oled.drawStr(6*9, 30, F("? ERROR"));
    } else if (boot->sd_card == -3) {
      oled.drawStr(6*9, 30, F("? NO FAT"));
    } else if (boot->sd_card == -2) {
      snprintf(msg, cap, "? 08x", boot->sd_size_kb);
      oled.drawStr(6*9, 30, msg);
    } else {
      oled.drawStr(6*9, 30, boot->sd_card == -1 ? F("-") :
          boot->sd_card == SD_CARD_TYPE_SD1 ? F("SD1") :
          boot->sd_card == SD_CARD_TYPE_SD2 ? F("SD2") :
          boot->sd_card == SD_CARD_TYPE_SDHC ? F("SDHC") : F("ERR"));
      if (boot->sd_card != -1) {
        float gb = (float)boot->sd_size_kb / 1000000.0f;
        int gb10 = (int)(10.0*gb + 0.5);
        snprintf(msg, cap, "%d.%d GB", gb10/10, gb10%10);
        oled.drawStr(6*14, 30, msg);
      }
    }
    oled.drawStr(0, 40, F("daily.csv:"));
    if (boot->csv_size >= 0) {
      snprintf(msg, cap, "%ld B", boot->csv_size);
      oled.drawStr(6*11, 40, msg);
      msg[cap-1] = '\0';
      oled.drawStr(6*1, 50, fsFmtDate(msg+cap-1, boot->csv_date));
      oled.drawStr(6*12, 50, fsFmtTime(msg+cap-1, boot->csv_time));
    } else {
      oled.drawStr(6*6, 50, F("file not found"));
    }
  } while (oled.nextPage());
}

//   +----------------------+
//   | Fri mm/dd/20yy       |
//   | 12:30 PM      85.2*F |
//   | This week in history |
//   | 1950s: 49.1 - 73.1*F |
//   | 1960s: 51.5 - 72.0*F |
//   | 1970:: 52.1 - 76.4*F |
//   +----------------------+

char *fmt_date(char *str);
char *fmt_time(char *str);
char *fmt_temp(char *str, int temp);
char *fmt_temp_range(char *str, int lotemp, int hitemp);
void info_screen() {
  char *msg = _buf;
  int cap = sizeof(_buf);
  oled.firstPage(); do {
    oled.drawStr(0,  0, fmt_date(msg));
    oled.drawStr(0, 10, fmt_time(msg));
    oled.drawStr(6*13, 10, fmt_temp(msg, temperature));
    oled.drawStr(0, 20, F("This week in history"));
    oled.drawStr(0, 30, F("1950s:"));
    oled.drawStr(0, 40, F("1960s:"));
    oled.drawStr(0, 50, F("1970:"));
    oled.drawStr(6*6, 30, fmt_temp_range(msg, historical_weekly_min_1950s, historical_weekly_max_1950s));
    oled.drawStr(6*6, 40, fmt_temp_range(msg, historical_weekly_min_1960s, historical_weekly_max_1960s));
    oled.drawStr(6*6, 50, fmt_temp_range(msg, historical_weekly_min_1970, historical_weekly_max_1970));
  } while (oled.nextPage());
}

void show_msg(int delay_ms, const __FlashStringHelper *text) {
  LOG(text);
  LOG("\n");
  oled.firstPage(); do {
    oled.drawStr(0, 30, text);
  } while (oled.nextPage());
  if (delay_ms > 0)
    delay(delay_ms);
}

// DS18B20 Temperature Sensors

void sensors_identify(int i, int *ds_model, byte *ds_addr) {
  LOG("Device ");
  Serial.println(i);
  *ds_model = sensors.getFamilyCode();
  switch (*ds_model) {
    case MODEL_DS18S20:
      FLOG("Model: DS18S20/DS1820\n");
      break;
    case MODEL_DS1822:
      FLOG("Model: DS1822\n");
      break;
    case MODEL_DS18B20:
      FLOG("Model: DS18B20\n");
      break;
    default:
      FLOG("Unrecognized Device\n");
      break;
  }

  sensors.getAddress(ds_addr);

  LOG("Address:");
  for (uint8_t i = 0; i < 7; i++) {
    LOGF(" %02x", ds_addr[i]);
  }
  LOG("\n");

  FLOG("Resolution: ");
  LOG(sensors.getResolution());
  LOG("\n");

  if (sensors.getPowerMode()) {
    FLOG("Power Mode: External\n");
  } else {
    FLOG("Power Mode: Parasite\n");
  }
}

// CSV File Parsing

int chop(char *s, int n) {
  for (int i = 0; i < n; i++) {
    if (s[i] == '\r' || s[i] == '\n' || s[i] == '\0') {
      s[i] = '\0';
      return i;
    }
  }
  return -1;
}

int _csv_line_len = 0; // length of last line, not including terminator
int _csv_buf_len = 0; // leftover bytes at end of _buf
char *csv_readline() {
  if (_csv_buf_len > 0 && _csv_line_len+1 < _csv_buf_len) { 
    memmove(_buf, _buf + _csv_line_len+1, _csv_buf_len - (_csv_line_len+1));
    _csv_buf_len -= (_csv_line_len+1);
    int eol_pos = chop(_buf, _csv_buf_len);
    if (eol_pos >= 0) {
      _csv_line_len = eol_pos;
      return _buf;
    }
  } else {
    _csv_buf_len = 0;
  }
  int n = csv.read(_buf + _csv_buf_len, sizeof(_buf) - _csv_buf_len);
  if (n < 0) { // read error
    FLOG("error reading csv\n");
    if (_csv_buf_len == 0) {
      // error at end of file
      FLOG("error at end of file\n");
      _csv_line_len = 0;
      return NULL;
    }
    _buf[_csv_buf_len] = '\n';
    n = 1;
  } else if (n < sizeof(_buf) - _csv_buf_len) { // end of file
    if (_csv_buf_len == 0) {
      // cleanly reached end of file
      FLOG("end of file\n");
      _csv_line_len = 0;
      return NULL;
    }
    _buf[_csv_buf_len+n] = '\n'; // in case of missing EOL
    n++;
  }
  // find newline
  int eol_pos = chop(_buf+_csv_buf_len, n);
  if (eol_pos < 0) {
    FLOG("error, csv line too long\n");
    eol_pos = _csv_buf_len + n - 1;
    _buf[eol_pos] = '\0';
  } else {
    eol_pos = _csv_buf_len + n;
  }
  _csv_line_len = eol_pos;
  _csv_buf_len += n;
  return _buf;
}

bool csv_parse() {
  if (!csv)
    return false;

  FLOG("reading daily.csv\n");
  csv.seek(0);
  char *line = csv_readline();
  if (line == NULL) {
    FLOG("missing header line\n");
    return false;
  } else {
    FLOG("csv header: \"");
    LOG(line);
    FLOG("\"\n");
    return true;
  }
}


// void oled_demo() {
//   char str[2], ch = 0x20;
//   str[0] = ch;
//   str[1] = 0;
//   for (int k = 0; k < 10; k++) {
//     char cc = ch;
//     oled.firstPage(); do {
//       ch = cc;
//       int y = 0;
//       for (int j = 0; j < 6; j++, y+=10) {
//         int x=0;
//         for (int i = 0; i < 21; i++, x+= 6) {
//           if ((i == 0 && j == 0) || (i == 20 && j == 5))
//             str[0] = '*';
//           else
//             str[0] = ch++;
//           oled.drawStr(x,  y, str);
//         }
//       }
//     } while (oled.nextPage());
//     delay(20000);
//   }
// }

void draw_text(byte x_pos, byte y_pos, byte text_size, const char *text) {
  // oled.firstPage();
  // do {
    oled.drawStr(x_pos, y_pos, text);
  /// } while (oled.nextPage());
}

void draw_text(byte x_pos, byte y_pos, byte text_size, const __FlashStringHelper *text) {
  // oled.firstPage();
  // do {
    //oled.drawStr(x_pos, y_pos, text);
  // } while (oled.nextPage());
}

void _draw_text(byte x_pos, byte y_pos, byte text_size, const char *text) {
    oled.drawStr(x_pos, y_pos, text);
}

void _draw_text(byte x_pos, byte y_pos, byte text_size, const __FlashStringHelper *text) {
    // oled.drawStr(x_pos, y_pos, text);
}


int16_t printDaytime(byte x, byte y, byte dawn, int8_t offset);

void rtc_read(byte addr, void *ptr, byte n) {
  // LOGF("rtc_read addr=0x%x ptr=0x%x n=%u\n", addr, ptr, n);
  byte *buf = ptr;
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
  byte *buf = ptr;
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

void init_settings() {
  show_msg(1000, F("initializing"));
  settings.opening_time = (DEFAULT_OPENING_TIME)/15;
  settings.closing_time = (DEFAULT_CLOSING_TIME)/15;
  settings.mode = MODE_NORMAL;
  settings.dstadjust = 1;
  settings.magic = MAGIC;
  rtc_write(0x07, &settings, sizeof(settings));

  settings.magic = 0;
  rtc_read(0x07, &settings, sizeof(settings));
  if (settings.magic != MAGIC)
    show_msg(1000, F("setup failed"));
  else
    show_msg(1000, F("saved settings"));
}

// byte _date, _month, _year;

void fmt_day(byte day, char *str) {
  switch (day) {
    case 1:  memcpy(str, "Sun", 3); break;
    case 2:  memcpy(str, "Mon", 3); break;
    case 3:  memcpy(str, "Tue", 3); break;
    case 4:  memcpy(str, "Wed", 3); break;
    case 5:  memcpy(str, "Thu", 3); break;
    case 6:  memcpy(str, "Fri", 3); break;
    default: memcpy(str, "Sat", 3); break;
  }
}

void update_daylight() {
  // byte date   = (time.d.date >> 4)  * 10 + (time.d.date & 0x0F);
  // byte month0 = ((time.d.month & 0x10) >> 4) * 10 + (time.d.month & 0x0F) - 1;
  // byte month1 = (month0 + 1) % 12;
  // uint16_t dawn0 = DAYLIGHT[3*month0];
  // uint16_t dawn1 = DAYLIGHT[3*month1];
  // uint16_t dusk0 = DAYLIGHT[3*month0+1];
  // uint16_t dusk1 = DAYLIGHT[3*month1+1];
  // byte days = DAYLIGHT[3*month0+2];
  // if (date > days)
  //   date = days;
  // date--;
  // days--;
  // dawn = (byte)((dawn0 * (days - date) + dawn1 * date) / days);
  // dusk = (byte)((dusk0 * (days - date) + dusk1 * date) / days);
  // LOG("for date "); LOG(date); LOG(" out of "); LOG(days); LOG(" in month "); LOG(month0); LOG("..."); LOG(month1);
  // LOG("\n");
  // LOG(dawn); LOG(" "); LOG(dawn0); LOG(" "); LOG(dawn1); LOG("\n");
  // LOG(dusk); LOG(" "); LOG(dusk0); LOG(" "); LOG(dusk1); LOG("\n");
}

char *fmt_date(char *str) {
  byte date   = (time.d.date >> 4)  * 10 + (time.d.date & 0x0F);
  byte month  = ((time.d.month & 0x10) >> 4) * 10 + (time.d.month & 0x0F);
  byte year   = (time.d.year >> 4)  * 10 + (time.d.year & 0x0F);
  // if (date == _date && month == _month && year == _year)
  //   return;
  // _date = date;
  // _month = month;
  // _year = year;
  //  01234567890123
  // "Fri mm/dd/20yy";
  str[14] = '\0';
  str[13] = year  % 10 + 48;
  str[12] = year  / 10 + 48;
  str[11] = '0';
  str[10] = '2';
  str[9] = '/';
  str[8] = date  % 10 + 48;
  str[7] = date  / 10 + 48;
  str[6] = '/';
  str[5] = month % 10 + 48;
  str[4] = month / 10 + 48;
  str[3] = ' ';
  fmt_day(time.d.day, str);
  // _draw_text(0, 0, 1, str);

  // if (settings.mode == MODE_NORMAL) {
  //   check_for_dst();
  //   update_daylight();
  //   openTime = printDaytime(0*6, 40, -1, -1);
  //   closeTime = printDaytime(12*6, 40, -1, -1) + (uint16_t)12*60;
  // }
  return str;
}

// void display_date() {
//   _month = 0xff; // invalidate cached date
//   refresh_date();
// }

// int _temp; // in tenths of a degree
// void display_temp() {
//   struct rtc_temp backup_temp_sensor; // temperature, as determined by RTC module
//   char msb = backup_temp_sensor.msb;
//   _temp = msb;
//   msb = ((int)msb)*9/5 + 32;
//   char str[] = "-xx" "\x80" "F"; // in System5x7 font, ascii 0x80 is degree symbol
//   if (msb < 0)
//     msb = abs(msb);
//   else
//     str[0] = ' ';
//   str[1] = msb / 10 + 48;
//   str[2] = msb % 10 + 48;
//   _draw_text(127-6*5, 0, 1, str);
// }
// void refresh_temp() {
//   char msb = backup_temp_sensor.msb;
//   if (msb == _temp)
//     return;
//   display_temp();
// }

void display99(byte x, byte y, byte val, bool zerofill) {
  char str[] = "xx";
  str[1] = val % 10 + 48;
  str[0] = val / 10 + 48;
  if (str[0] == '0' && !zerofill)
    str[0] = ',' /* space */; // custom font where ,=space
  _draw_text(x, y, 2, str);
}

void check_for_dst() {
  if (!settings.dstadjust) {
    inDST = false;
    return;
  }
  byte date   = (time.d.date >> 4) * 10 + (time.d.date & 0x0F);
  byte month  = ((time.d.month & 0x10) >> 4) * 10 + (time.d.month & 0x0F);
  byte year   = (time.d.year >> 4) * 10 + (time.d.year & 0x0F);
  byte hour   = (time.t.hour >> 4) * 10 + (time.t.hour & 0x0F);
  byte offset = (year + year/4 + 2) % 7;
  bool after2ndSundayInMarch =
    (month == 3 && date == (14-offset) && hour >= 2)
    || (month == 3 && date > (14-offset))
    || (month > 3);
  bool after1stSundayInNovember =
    (month == 11 && date == (7-offset) && hour >= 2)
    || (month == 11 && date > (7-offset))
    || (month > 11);
  inDST = after2ndSundayInMarch && !after1stSundayInNovember;
}

// byte _hour = 0xff, _minute = 0xff, _second = 0xff;
char *fmt_time(char *str) {
  byte hour   = (time.t.hour >> 4)   * 10 + (time.t.hour & 0x0F);
  byte minute = (time.t.minute >> 4) * 10 + (time.t.minute & 0x0F);
  byte second = (time.t.second >> 4) * 10 + (time.t.second & 0x0F);
  check_for_dst();
  if (inDST)
    hour = (hour + 1) % 24;
  bool pm = (hour >= 12);
  if (hour == 0) // midnight 0h becomes 12 am
    hour = 12;
  else if (hour > 12) // 13h becomes 1pm, etc.
    hour -= 12;
  str[0] = hour >= 10 ? hour / 10 + 48 : ' ';
  str[1] = hour % 10 + 48;
  str[2] = ':';
  str[3] = minute / 10 + 48;
  str[4] = minute % 10 + 48;
  str[5] = ' ';
  str[6] = pm ? 'P' : 'A';
  str[7] = 'M';
  str[8] = '\0';
  return str;
}
// void display_time() {
//   _hour = _minute = _second = 0xff; // invalidate cached time
//   _draw_text(30, 8, 2, ":");
//   _draw_text(60, 8, 2, ":");
//   refresh_time();
// }

// -xx.x*F
// xxx.x*F
//  cold!
//  hot!!
char *fmt_temp(char *str, int temp) {
  if (temp < -999) {
    strcpy(str, " cold! ");
    return str;
  }
  if (temp > 9999) {
    strcpy(str, " hot!! ");
    return str;
  }
  char *original = str;
  if (temp <= -100) {
    str[0] = '-';
    temp *= -1;
    str++;
  } else if (temp < 0) {
    str[0] = ' ';
    str[1] = '-';
    temp *= -1;
    str+=2;
  } else if (temp < 100) {
    str[0] = str[1] = ' ';
    str+=2;
  } else if (temp < 1000) {
    str[0] = ' ';
    str++;
  }
  sprintf(str, "%d.%d" DEGREES "F", temp/10, temp%10);
  return original;
}
// xxx.x -xxx.x*F
char *fmt_temp_range(char *str, int lotemp, int hitemp) {
  fmt_temp(str, lotemp);
  str[5] = ' ';
  str[6] = '-';
  fmt_temp(str+7, hitemp);
  return str;
}

int16_t printDaytime(byte x, byte y, byte dawn, int8_t offset) {
  FLOG("calculate dawn/dusk: ");
  LOG((int)(unsigned int)dawn);
  LOG(" offset: "); LOG((int)offset);
  int16_t mm = 240; // 4:00
  mm += dawn;
  mm += offset;
  byte hours = (byte)(mm/60);
  byte minutes = (byte)(mm%60);
  if (inDST)
    hours++;
  char str[] = "hh:mm";
  str[4] = minutes % 10 + 48;
  str[3] = minutes / 10 + 48;
  str[1] = hours % 10 + 48;
  str[0] = hours / 10 + 48;
  if (str[0] == '0')
    str[0] = ' ';
  draw_text(x, y, 1, str);
  LOG(" result: ");
  LOG(mm);
  LOG(inDST?" plus 1 hour for dst" : " plus nothing");
  LOG("\n");
  return mm;
}

boot_msgs boot = { -1, {0,}, -1, -1, -1, 0, 0 };

void setup() {

  DBG(Serial.begin(115200));
  FLOG("Booting, Version " VERSION "\n");

  // for (int i = 0; i < 3; i++) {
  //   pinMode(STRIP1_RGB_PIN+i , OUTPUT);
  //   digitalWrite(STRIP1_RGB_PIN+i, HIGH);
  // }
  for (int i = 0; i < 3; i++) {
    pinMode(STRIP2_RGB_PIN+i , OUTPUT);
    digitalWrite(STRIP2_RGB_PIN+i, HIGH);
  }
  for (int i = 0; i < 3; i++) {
    pinMode(STRIP3_RGB_PIN+i , OUTPUT);
    digitalWrite(STRIP3_RGB_PIN+i, HIGH);
  }

  pinMode(BUTTON_OK_PIN, INPUT_PULLUP);
  pinMode(BUTTON_DN_PIN, INPUT_PULLUP);
  pinMode(BUTTON_UP_PIN, INPUT_PULLUP);

  oled.begin();
  oled.setFont(u8g_font_6x10);
  oled.setFontRefHeightExtendedText();
  oled.setDefaultForegroundColor();
  oled.setFontPosTop();

  status = 0;
  boot_screen(&boot);
  delay(500);

  rtc_read(0x07, &settings, sizeof(settings));
  bool need_reset = false;
  if (settings.magic != MAGIC) {
    show_msg(1000, F("bad magic, resetting"));
    need_reset = true;
  } else if (!digitalRead(BUTTON_UP_PIN) && !digitalRead(BUTTON_DN_PIN)) {
    FLOG("buttons UP+DOWN held, resetting?\n");
    show_msg(0, F("Press OK to reset..."));
    unsigned long then = millis();
    while (!need_reset && (millis() - then) < RESET_CONFIRM_TIMEOUT)
      need_reset = !digitalRead(BUTTON_OK_PIN);
    if (need_reset)
      show_msg(1000, F("reset requested... "));
    else
      show_msg(1000, F("reset not requested"));
  }
  if (need_reset)
    init_settings();
  status |= (settings.magic == MAGIC) ? STATUS_RTC_WORKING : 0;
  boot_screen(&boot);

  FLOG("Scanning for sensors...\n");
  int sensors_found = 0;
  sensors.resetSearch();
  for (int i = 0; sensors.selectNext(); i++) {
    sensors_found++;
    sensors_identify(i, &boot.ds_model, boot.ds_addr);
    FLOG("Temperature: "); LOG(sensors.getTempF()); LOG("F\n");
  }
  FLOG("Found "); LOG(sensors_found); FLOG(" sensors.\n");
  status |= (sensors_found > 0) ? STATUS_SENSOR_FOUND : 0;
  boot_screen(&boot);

  FLOG("Checking for SD card...\n");
  if (!sd.begin(MICROSD_CS_PIN, SPI_SPEED)) {
    int errcode = sd.card()->errorCode();
    if (errcode) {
      FLOG("SD initialization failed. errcode = ");
      LOG(errcode);
      LOG("\n");
      boot.sd_card = -2;
      boot.sd_size_kb = errcode;
    } else if (sd.vol()->fatType() == 0) {
      FLOG("Can't find a valid FAT16/FAT32 partition.");
      boot.sd_card = -3;
    } else {
      FLOG("Can't determine error type");
      boot.sd_card = -4;
    }
  } else {
    status |= STATUS_SDCARD_FOUND;
    boot.sd_card = sd.card()->type();
    float sz = sd.vol()->clusterCount();
    sz *= sd.vol()->sectorsPerCluster();
    sz /= 1000.0f;
    sz *= 512.0f;
    boot.sd_size_kb = (int32_t)(sz + 0.5);
    // FLOG("SD card volume sectors per cluster: "); LOG(sd.vol()->sectorsPerCluster()); FLOG("\n");
    // FLOG("SD card volume count of cluster: "); LOG(sd.vol()->clusterCount()); FLOG("\n");
    // FLOG("SD card total sectors: "); LOG(sd.card()->sectorCount()); FLOG("\n");
    FLOG("SD card size: "); LOG(boot.sd_size_kb); FLOG(" KB\n");
    // Serial.println("Files on card:");
    // Serial.println("   Size    Name");
    // sd.ls(LS_R | LS_SIZE);
  }
  boot_screen(&boot);

  if (status & STATUS_SDCARD_FOUND)
    csv = sd.open("daily.csv", FILE_READ);
  if (csv) {
    status |= STATUS_DAILY_FOUND;
    boot.csv_size = csv.fileSize();
    FLOG("file size: "); LOG(boot.csv_size); FLOG(" bytes\n");
    csv.getCreateDateTime(&boot.csv_date, &boot.csv_time);
  } else {
    FLOG("Could not open daily.csv\n");
    boot.csv_size = -1;
  }
  boot_screen(&boot);

  // TODO: trial read of daily.csv
  if (csv_parse()) {
    status |= STATUS_DAILY_READY;
  }
  boot_screen(&boot);

  delay(1000);

  if (status != STATUS_READY) {
    status |= STATUS_ERROR;
    boot_screen(&boot);
  }
  randomSeed(1234);
}


int blink = 0;
void loop() {

  blink++;
  // if (blink % 2 == 0) {
  //   boot_screen(&boot);
  // } else {
    temperature = blink;
    historical_weekly_min_1950s = blink * 30 + 5 - 500;
    historical_weekly_min_1960s = blink * 100 * 5 - 500;
    historical_weekly_min_1970 = - blink * 10 + 5 - 500;
    historical_weekly_max_1950s = blink * 30 + 5;
    historical_weekly_max_1960s = blink * 100 * 5;
    historical_weekly_max_1970 = - blink * 10 + 5;
    info_screen();
  // }
  //for (int i = 0; sensors.selectNext(); i++) {
  //sensors_identify(i);
  // sensors.resetSearch();
  // if (sensors.selectNext()) {
  //  Serial.print(sensors.getTempF()); Serial.print("F ");
  // } else {
  //   Serial.print(". ");
  // }
 
  // blink++;
  // if ((blink % 10) == 0) {
  // oled.firstPage();
  // do {
  //   display_date();
  //   display_temp();
  //   display_time();
  // } while (oled.nextPage());
  // }

  // for (int i = 0; i < 9; i++) {
  //   int p = (i < 3 ? STRIP1_RGB_PIN+i :
  //       (i < 6 ? STRIP2_RGB_PIN+i-3 : STRIP3_RGB_PIN+i-6));
  //   digitalWrite(p, (blink % 9) == i ? LOW : HIGH);
  // }

  delay(300);
}
// const char *hex_nibbles = "0123456789abcdef";
// char _hex_str[3];
// char byte2hex(byte b) {
//   _hex_str[0] = hex_nibbles[(b>>4)&0xf];
//   _hex_str[1] = hex_nibbles[(b>>0)&0xf];
//   _hex_str[2] = 0;
//   return _hex_str;
// }
