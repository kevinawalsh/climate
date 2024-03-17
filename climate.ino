#include <Wire.h>
#include <U8glib.h>
// #include <U8x8lib.h>
// #include <U8g2lib.h>
#include <DS18B20.h>
#include "SdFat.h"

#define VERSION "0.1"

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
#define BUTTON0_PIN A0 // pin A0 for push-button, with internal pullup
#define BUTTON1_PIN A1 // pin A1 for push-button, with internal pullup
#define BUTTON2_PIN A2 // pin A2 for push-button, with internal pullup

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
#define LOGF(msg...) DBG(serial_printf(msg))

#define MODE_BOOTING -1
#define MODE_NORMAL 0
#define MODE_MANUAL 1
#define MODE_DIAGNOSTICS 2

#define DEGREES "\xb0" // degree symbol in u8g_font_6x10

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
  byte magic;         // Register 0x0d: alarm 2 data, 8 bits, repurposed, magic 0xcd
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
// U8X8_SH1106_128X64_NONAME_HW_I2C oled;
// U8G2_SH1106_128X64_NONAME_1_HW_I2C oled(U8G2_R0);

uint16_t openTime, closeTime; // minutes after midnight
struct rtc_time time; // current time
struct rtc_settings settings; // saved settings
bool inDST; // whether daylight savings time is active
struct rtc_temp backup_temp_sensor; // temperature, as determined by RTC module
float primary_temp_sensor; // current temperature, as determined by DS18B20 sensors
float historical_weekly_min[21]; // historical minimum temperature for this week, by year, 1950-1970
float historical_weekly_max[21]; // historical maximum temperature for this week, by year, 1950-1970
float historical_weekly_min_avg; // average historical minimum temperature for this week, 1950-1979
float historical_weekly_max_avg; // average historical maximum temperature for this week, 1950-1979

#if DEBUG
char _printf_buf[80];
void serial_printf(const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  vsnprintf(_printf_buf, 80, fmt, va);
  va_end(va);
  _printf_buf[79] = '\0';
  Serial.print(_printf_buf);
}
#endif

// Boot Screen: (6x10 font on 128x64 screen, so 21 columns x 6 rows of text
//   +----------------------+
//   | v1.1, normal mode    |
//   | Sensor: DS18B20      |
//   |     2803005704e13c2b |
//   | MicroSD: SDHC 7.3GB  |
//   | daily.csv: 35122 B   |
//   |  2024-03-16 18:57:34 |
//   +----------------------+

struct boot_msgs {
    int mode;
    int ds_model;
    byte ds_addr[8];
    int sd_card;
    int sd_size_kb;
    int csv_size;
    uint16_t csv_date, csv_time;
};

// const char *hex_nibbles = "0123456789abcdef";
// char _hex_str[3];
// char byte2hex(byte b) {
//   _hex_str[0] = hex_nibbles[(b>>4)&0xf];
//   _hex_str[1] = hex_nibbles[(b>>0)&0xf];
//   _hex_str[2] = 0;
//   return _hex_str;
// }

void boot_screen(struct boot_msgs *boot) {
  char msg[12];
  oled.firstPage(); do {
    oled.drawStr(0, 0, F("v" VERSION ","));
    oled.drawStr(6*6, 0, 
        boot->mode == MODE_BOOTING ? F("booting") :
        boot->mode == MODE_NORMAL ? F("normal mode") :
        boot->mode == MODE_MANUAL ? F("manual mode") :
        boot->mode == MODE_DIAGNOSTICS ? F("diagnostics") : F("error"));
    oled.drawStr(0, 10, F("Sensor:"));
    oled.drawStr(6*8, 10,
        boot->ds_model == -1 ? F("-") :
        boot->ds_model == MODEL_DS18S20 ? F("DS18S20") :
        boot->ds_model == MODEL_DS1822 ? F("DS1822") :
        boot->ds_model == MODEL_DS18B20 ? F("DS18B20") : F("unknown"));
    if (boot->ds_model != -1) {
      for (int i = 0; i < 7; i++) {
        snprintf(msg, sizeof(msg), "%02x", boot->ds_addr[i]);
        oled.drawStr(6*(4+2*i), 20, msg);
      }
    }
    oled.drawStr(0, 30, "MicroSD:");
    oled.drawStr(6*9, 30, boot->sd_card == -1 ? F("-") :
        boot->sd_card == SD_CARD_TYPE_SD1 ? F("SD1") :
        boot->sd_card == SD_CARD_TYPE_SD2 ? F("SD2") :
        boot->sd_card == SD_CARD_TYPE_SDHC ? F("SDHC") : F("unkn"));
    if (boot->sd_card != -1) {
      snprintf(msg, sizeof(msg), "%.1fGB", (float)boot->sd_size_kb / 1048576.0f);
      oled.drawStr(6*14, 30, msg);
    }
    if (boot->csv_size >= 0) {
      oled.drawStr(0, 40, F("daily.csv:"));
      snprintf(msg, sizeof(msg), "%d B", boot->csv_size);
      oled.drawStr(6*11, 40, msg);
      oled.drawStr(6*1, 50, fsFmtDate(msg, boot->csv_date));
      oled.drawStr(6*12, 50, fsFmtDate(msg, boot->csv_time));
    }
  } while (oled.nextPage());
}

// Temperature Sensors
//
void sensors_identify(int i, int *ds_model, byte *ds_addr) {
  LOG("Device ");
  Serial.println(i);
  *ds_model = sensors.getFamilyCode();
  switch (*ds_model) {
    case MODEL_DS18S20:
      LOG("Model: DS18S20/DS1820\n");
      break;
    case MODEL_DS1822:
      LOG("Model: DS1822\n");
      break;
    case MODEL_DS18B20:
      LOG("Model: DS18B20\n");
      break;
    default:
      LOG("Unrecognized Device\n");
      break;
  }

  sensors.getAddress(ds_addr);

  LOG("Address:");
  for (uint8_t i = 0; i < 7; i++) {
    LOGF(" %02x", ds_addr[i]);
  }
  LOG("\n");

  LOG("Resolution: ");
  LOG(sensors.getResolution());
  LOG("\n");

  if (sensors.getPowerMode()) {
    LOG("Power Mode: External\n");
  } else {
    LOG("Power Mode: Parasite\n");
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
  Wire.beginTransmission(0x68);  // Start I2C protocol with DS3231 address
  Wire.write(addr);              // Send register address
  Wire.endTransmission(false);   // I2C restart
  Wire.requestFrom(0x68, (int)n);     // Request n bytes from DS3231 and release I2C bus at end of reading
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
  // settings.dawn_offset = 20; // default: open 20 minutes after dawn
  // settings.dusk_offset = 20; // default: close 20 minutes after dusk
  settings.opening_time = (7*60+15)/5;  // default: open at 7:15 AM (8:15 AM daylight savings)
  settings.closing_time = (7*60+15)/5; // default: close at 7:15 PM (8:15 AM daylight savings)
  settings.mode = MODE_NORMAL;
  settings.dstadjust = 1;
  settings.magic = 0xcd;
  rtc_write(0x07, &settings, sizeof(settings));

  settings.magic = 0;
  rtc_read(0x07, &settings, sizeof(settings));
  if (settings.magic != 0xcd) {
    draw_text(0, 32, 1, F("setup failed         "));
    LOG("setup failed\n");
  } else {
    draw_text(0, 32, 1, F("saved settings       "));
    LOG("settings saved\n");
  }

  // check_for_dst();
}

byte _date, _month, _year;

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

void refresh_date() {
  byte date   = (time.d.date >> 4)  * 10 + (time.d.date & 0x0F);
  byte month  = ((time.d.month & 0x10) >> 4) * 10 + (time.d.month & 0x0F);
  byte year   = (time.d.year >> 4)  * 10 + (time.d.year & 0x0F);
  if (date == _date && month == _month && year == _year)
    return;
  _date = date;
  _month = month;
  _year = year;
  char str[] = "Fri mm/dd/20yy ";
  str[13] = year  % 10 + 48;
  str[12] = year  / 10 + 48;
  str[8] = date  % 10 + 48;
  str[7] = date  / 10 + 48;
  str[5] = month % 10 + 48;
  str[4] = month / 10 + 48;
  fmt_day(time.d.day, str);
  _draw_text(0, 0, 1, str);

  if (settings.mode == MODE_NORMAL) {
    check_for_dst();
    update_daylight();
    openTime = printDaytime(0*6, 40, -1, -1);
    closeTime = printDaytime(12*6, 40, -1, -1) + (uint16_t)12*60;
  }
}

void display_date() {
  _month = 0xff; // invalidate cached date
  refresh_date();
}

char _temp;
void display_temp() {
  char msb = backup_temp_sensor.msb;
  _temp = msb;
  msb = ((int)msb)*9/5 + 32;
  char str[] = "-xx" "\x80" "F"; // in System5x7 font, ascii 0x80 is degree symbol
  if (msb < 0)
    msb = abs(msb);
  else
    str[0] = ' ';
  str[1] = msb / 10 + 48;
  str[2] = msb % 10 + 48;
  _draw_text(127-6*5, 0, 1, str);
}
void refresh_temp() {
  char msb = backup_temp_sensor.msb;
  if (msb == _temp)
    return;
  display_temp();
}

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

byte _hour = 0xff, _minute = 0xff, _second = 0xff;
void refresh_time() {
  byte hour   = (time.t.hour >> 4)   * 10 + (time.t.hour & 0x0F);
  byte minute = (time.t.minute >> 4) * 10 + (time.t.minute & 0x0F);
  byte second = (time.t.second >> 4) * 10 + (time.t.second & 0x0F);
  if (hour != _hour) {
    _hour = hour;
    check_for_dst();
    if (inDST)
      hour = (hour + 1) % 24;
    bool pm = (hour >= 12);
    if (hour == 0) // midnight 0h becomes 12 am
      hour = 12;
    else if (hour > 12) // 13h becomes 1pm, etc.
      hour -= 12;
    display99(6, 8, hour, false);
    _draw_text(95, 8, 2, pm ? "PM" : "AM");
  }
  if (minute != _minute) {
    _minute = minute;
    display99(36, 8, minute, true);
  }
  if (second != _second) {
    _second = second;
    display99(66, 8, second, true);
  }
}
void display_time() {
  _hour = _minute = _second = 0xff; // invalidate cached time
  _draw_text(30, 8, 2, ":");
  _draw_text(60, 8, 2, ":");
  refresh_time();
}

int16_t printDaytime(byte x, byte y, byte dawn, int8_t offset) {
  LOG("calculate dawn/dusk: ");
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

void setup() {

  DBG(Serial.begin(115200));
  LOG("Booting, Version " VERSION "\n");

  boot_msgs boot = { MODE_BOOTING, -1, {0,}, -1, -1, -1, 0, 0 };

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

  pinMode(BUTTON0_PIN, INPUT_PULLUP);
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  oled.begin();
  oled.setFont(u8g_font_6x10);
  oled.setFontRefHeightExtendedText();
  oled.setDefaultForegroundColor();
  oled.setFontPosTop();

  boot_screen(&boot);
  delay(1000);

  LOG("Scanning for sensors...\n");
  int found = 0;
  sensors.resetSearch();
  for (int i = 0; sensors.selectNext(); i++) {
    found++;
    sensors_identify(i, &boot.ds_model, boot.ds_addr);
    LOG("Temperature: "); LOG(sensors.getTempF()); LOG("F\n");
  }
  LOG("Found "); LOG(found); LOG(" sensors.\n");

  boot_screen(&boot);
  delay(1000);

  rtc_read(0x07, &settings, sizeof(settings));
  bool need_reset = false;
  if (settings.magic != 0xcd) {
    LOG("bad magic, resetting\n");
    _draw_text(0, 32, 1, F("bad magic, resetting"));
    need_reset = true;
  }
  if (need_reset) {
    init_settings();
  }

  if (!sd.begin(MICROSD_CS_PIN, SPI_SPEED)) {
    int errcode = sd.card()->errorCode();
    if (errcode) {
      Serial.print("SD initialization failed. errcode = ");
      Serial.println(errcode);
    } else if (sd.vol()->fatType() == 0) {
      Serial.println("Can't find a valid FAT16/FAT32 partition.");
    } else {
      Serial.println("Can't determine error type");
    }
  } else {
    Serial.println("Files on card:");
    Serial.println("   Size    Name");
    sd.ls(LS_R | LS_SIZE);
  }

  Serial.print("Temperature: ");
}

int blink = 0;

void loop() {
  //for (int i = 0; sensors.selectNext(); i++) {
  //sensors_identify(i);
  // sensors.resetSearch();
  // if (sensors.selectNext()) {
    Serial.print(sensors.getTempF()); Serial.print("F ");
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

  delay(1000);
}
