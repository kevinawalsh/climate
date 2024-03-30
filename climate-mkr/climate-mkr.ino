#include <U8g2lib.h>
#include <SPI.h>
#include <WiFiNINA.h>
#include <DS18B20.h>
#include <RTCZero.h>
#include <stdarg.h>
#include <ctype.h>

#include "climate.h"
#include "arduino_secrets.h" 

// TEST: closing times, 8am to 9pm, adjustable by website
// TODO: record min/max temps, upload to website
// TEST: natural variation
// TODO: calibrate gradient
// TEST: simulation mode

#define VERSION "0.1"

// See arduino_screts.h for a list of network SSID and password pairs,
// like:
// #define SSID_AND_PASS { "WorkGuest2G", "", "HomeWiFi", "pass123", 0 }
char *ssid_and_pass[] = SSID_AND_PASS;

// URL for fetching climate data, current date and time, etc.
// #define USE_HTTPS
// #define HTTP_PORT 443
// #define HTTP_PORT 80
#define HTTP_PORT 8888
#define URL_SERVER "assembler.kwalsh.org"
#define URL_PATH_STATUS "/status"
#define URL_PATH_MONITOR "/monitor"

// pins 0 to 8 are for RGB Strip Lights, using PWM mode
byte RGB_PWM_PINS[] = { 1, 2, 0, 4, 5, 3, 7, 6, 8 };

// OneWire bus for Dallas Semiconductor DS18B20 Temperature Sensor
#define ONEWIRE_PIN 10 // pin 10 is OneWire data, with external pullup

#define OLED_SDA_PIN 11 // pin 11 is OLED and DS18B20 RTC bus SDA
#define OLED_SCL_PIN 12 // pin 12 is OLED and DS18B20 RTC bus SCL

// #define SHIFTREG_LATCH_PIN 5  // pin 5 is 74HC595 latch
// #define SHIFTREG_CLOCK_PIN 6  // pin 6 is 74HC595 clock
// #define SHIFTREG_DATA_PIN 4   // pin 4 is 74HC595 data

#define FAN_PIN A3 // pin A3 is fan mosfet, with externall pulldown

#define DEBUG 1
#if DEBUG
#define DBG(code) code
#else
#define DBG(code) do {} while (0)
#endif
#define LOG(msg) DBG(Serial.print(msg))
#define FLOG(msg) DBG(Serial.print(F(msg)))
#define LOGF(msg...) DBG(serial_printf(msg))

#define STATUS_RTC_WORKING     (1 << 0) // DS18B20 Temperature Sensor was found
#define STATUS_SENSOR_FOUND    (1 << 1) // DS18B20 Temperature Sensor was found
#define STATUS_WIFI_PRESENT    (1 << 2) // WiFi module found
#define STATUS_WIFI_CONNECTED  (1 << 3) // WiFi connected
#define STATUS_DATA_DOWNLOADED (1 << 4) // data downloaded from website
#define STATUS_READY           (0x1f)
#define STATUS_ERROR           (1 << 7) // something is wrong

unsigned long screen_refresh_timestamp = 0;
byte status = 0;
byte mode[3] = {0,0,0}; // 0 = auto, 1 = manual_rgb, 2 = manual_gradient, 3 = simulated_date_and_time
byte rgb[9];

char wifi_ssid[64] = { 0, };
#ifdef USE_HTTPS
WiFiSSLClient wifi_client;
#else
WiFiClient wifi_client;
#endif

DS18B20 sensors(ONEWIRE_PIN); // Temperature Sensors
U8G2_SH1106_128X64_NONAME_1_HW_I2C oled(U8G2_R0); // 2c oled using pin 11 as SDA and pin 12 as SCL
RTCZero rtc; // internal Real Time Clock for Arduino MKR 1010 board

char _buf[128]; // used by parsing, serial_printf, oled drawing loops

int opening_time = 8*60, closing_time = 21*60; // minutes after midnight
struct rtc_time time; // current time
bool inDST; // whether daylight savings time is active
int temperature = 0; // current outside tempeature, from DS18B20 sensor
int circuit_temperature = 0; // current tempeature from RTC module, interior of electronics enclosure
int sensors_found = 0;
int historical_weekly_stats[8]; // min, max, min50, max50, min60, max60, min70, max70
int sim_day[3]; // day number, 1 - 365
int sim_time[3]; // minute number, 0 - 1439
byte fan_speed = 0; // fan speed setting, from 0 to 255, based on circuit_temperature

struct boot_msgs {
    int ds_model;
    byte ds_addr[8];
    int32_t downloaded_bytes;
};
boot_msgs boot = { -1, {0,}, -1 };

#if DEBUG
void serial_printf(const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  vsnprintf(_buf, sizeof(_buf), fmt, va);
  va_end(va);
  _buf[sizeof(_buf)-1] = '\0';
  Serial.print(_buf);
}
#endif

bool rate_limit_expired(unsigned long *last_update, unsigned long interval) {
  unsigned long time_now = millis();
  if ((time_now - *last_update) >= interval) {
    *last_update = time_now;
    return true;
  } else {
    return false;
  }
}

byte clamp8(float f) {
  if (f <= 0.0f) return 0;
  else if (f >= 255.0f) return 255;
  int b = (int)(f+0.5);
  if (b <= 0) return 0;
  if (b >= 255) return 255;
  return (byte)b;
}

byte cold[] = { 0, 0, 255 };
byte mid[] = { 0, 255, 0 };
byte warm[] = { 150, 40, 0 };
byte hot[] = { 255, 0, 0 };

void interpolate(float v, byte *lo, byte *hi, byte *rgb) {
  rgb[0] = clamp8(lo[0]*(1.0f-v) + hi[0]*v);
  rgb[1] = clamp8(lo[1]*(1.0f-v) + hi[1]*v);
  rgb[2] = clamp8(lo[2]*(1.0f-v) + hi[2]*v);
}

// picks color depending on deviation:
// deviation -7 from average: cold
// deviation -2 from average: mid
// deviation 0 from average: warm
// deviation +5 from average: hot
void gradient(float txx, byte *rgb) {
  if (txx < -7.0f)
    txx = -7.0f;
  else if (txx > 7.0f)
    txx = 7.0f;
  if (txx < -2.0f)
    interpolate((txx + 7.0f) / 5.0f, cold, mid, rgb);
  else if (txx < 0.0f)
    interpolate((txx + 2.0f) / 2.0f, mid, warm, rgb);
  else
    interpolate((txx + 0.0f) / 5.0f, warm, hot, rgb);
}

//   +----------------------+
//   | v1.1, [111222333444] |
//   | Sensor: DS18B20      |
//   |     2803005704e13c2b |
//   | WiFi: valley2        |
//   |     connected        |
//   | Data: 31257 bytes    |
//   +----------------------+

void boot_screen(struct boot_msgs *boot) {
  LOGF("Status: %x\n", status);
  char *msg = _buf;
  int cap = sizeof(_buf);
  oled.firstPage(); do {
    oled_drawStr(0, 0, F("v" VERSION ", ["));
    oled_drawStr(6*20, 0, "]");
    if (status == 0) {
      oled_drawStr(6*8, 0, F("booting"));
    } else if (status & STATUS_ERROR) {
      oled_drawStr(6*8, 0, F("error"));
    } else if (status == STATUS_READY) {
      oled_drawStr(6*8, 0, F("ready"));
    } else {
      msg[0] = msg[1] = (status & STATUS_RTC_WORKING) ? '#' : '?';
      msg[2] = msg[3] = (status & STATUS_SENSOR_FOUND) ? '#' : '?';
      msg[4] = msg[5] = (status & STATUS_WIFI_PRESENT) ? '#' : '?';
      msg[6] = msg[7] = (status & STATUS_WIFI_CONNECTED) ? '#' : '?';
      msg[8] = msg[9] = (status & STATUS_DATA_DOWNLOADED) ? '#' : '?';
      msg[10] = 0;
      oled_drawStr(6*7, 0, msg);
    }
    oled_drawStr(0, 10, F("Sensor:"));
    oled_drawStr(6*8, 10,
        boot->ds_model == -1 ? F("-") :
        boot->ds_model == MODEL_DS18S20 ? F("DS18S20") :
        boot->ds_model == MODEL_DS1822 ? F("DS1822") :
        boot->ds_model == MODEL_DS18B20 ? F("DS18B20") : F("unknown"));
    if (boot->ds_model != -1) {
      for (int i = 0; i < 7; i++) {
        snprintf(msg, cap, "%02x", boot->ds_addr[i]);
        oled_drawStr(6*(4+2*i), 20, msg);
      }
    }
    oled_drawStr(0, 30, "WiFi:");
    oled_drawStr(6*6, 30, wifi_ssid);
    if (status & STATUS_WIFI_CONNECTED)
      oled_drawStr(6*4, 40, F("connected"));
    else
      oled_drawStr(6*4, 40, F("searching..."));

    oled_drawStr(0, 50, F("Data:"));
    if (boot->downloaded_bytes >= 0) {
      snprintf(msg, cap, "%ld B", boot->downloaded_bytes);
      oled_drawStr(6*6, 50, msg);
    } else {
      oled_drawStr(6*6, 50, F("not ready"));
    }
  } while (oled.nextPage());
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

void scan_sensors() {
  FLOG("Scanning for sensors...\n");
  sensors.resetSearch();
  for (int i = 0; sensors.selectNext(); i++) {
    sensors_found++;
    sensors_identify(i, &boot.ds_model, boot.ds_addr);
    FLOG("Temperature: "); LOG(sensors.getTempF()); LOG("F\n");
  }
  FLOG("Found "); LOG(sensors_found); FLOG(" sensors.\n");
}

int month_offset[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
int get_daynum(int month, int day) {
    return month_offset[month-1] + day;
}

void parse_mode(int tube, const char *line) {
  if (!strcasecmp(line, "auto")) {
    mode[tube] = 0;
    FLOG("Tube "); LOG(tube); FLOG(" mode is now: auto\n");
  } else if (!strncasecmp(line, "manual ", 7)) {
    line += 7;
    mode[tube] = 1;
    for (int i = 0; i < 3; i++) {
      rgb[3*tube + i] = atoi(line);
      char *pos = index(line, ' ');
      if (pos)
        line = pos+1;
    }
    FLOG("Tube "); LOG(tube); FLOG(" mode is now: manual");
    for (int i = 0; i < 3; i++) {
      LOG(" "); LOG(rgb[3*tube + i]);
    }
    LOG("\n");
  } else if (!strncasecmp(line, "gradient ", 9)) {
    line += 9;
    mode[tube] = 2;
    float txx = atof(line);
    gradient(txx, &rgb[3*tube]);
    FLOG("Tube "); LOG(tube); FLOG(" mode is now: gradient");
    for (int i = 0; i < 3; i++) {
      LOG(" "); LOG(rgb[3*tube + i]);
    }
    LOG("\n");
  } else if (!strncasecmp(line, "sim ", 4)) { // sim mm/dd hh:mm
    line += 4;
    int hr = atoi(line);
    int mn = atoi(line+3);
    if (!(0 <= hr && hr < 24 && 0 <= mn && mn <= 59))
      return;
    line += 6;
    char *pos1 = index(line, '/');
    if (!pos1)
      return;
    int mm = atoi(line);
    int dd = atoi(pos1+1);
    if (!(1 <= mm && mm <= 12 && 1 <= dd && dd <= 31))
      return;
    sim_day[tube] = get_daynum(mm, dd);
    sim_time[tube] = hr*60 + mn;
    if (!(60 <= sim_day[tube] && sim_day[tube] <= 181))
      return;
    mode[tube] = 3;
    FLOG("Tube "); LOG(tube); FLOG(" mode is now: sim ");
    LOG(mm); LOG("/"); LOG(dd); LOG(" "); if (hr<10) LOG("0"); LOG(hr); LOG(":"); if (mn<10) LOG("0"); LOG(mn); LOG("\n");
  }
}

void http_parse_line(const char *line, int linelen) {
  screen_refresh_timestamp = 0;
  if (!strncasecmp(line, "time: ", 6)) { // 14:25
    line += 6;
    int hh = atoi(line);
    int mm = atoi(line+3);
    if (!(0 <= hh && hh < 24 && 0 <= mm && mm <= 59))
      return;
    // time.t.hour = hh;
    // time.t.minute = mm;
    // time.t.second = 0;
    // rtc.setTime(time.t.hour, time.t.minute, 0);
    rtc_read_all();
    rtc_set_hour(hh);
    rtc_set_minute(mm);
    rtc_set_second(0);
    rtc_write_all();
    FLOG("Time is now: ");
    LOG(fmt_time(_buf));
    LOG("\n");
  } else if (!strncasecmp(line, "open: ", 6)) { // hh:mm
     
  } else if (!strncasecmp(line, "date: ", 6)) { // mm/dd/yyyy
    line += 6;
    char *pos1 = index(line, '/');
    if (!pos1)
      return;
    char *pos2 = index(pos1+1, '/');
    if (!pos2)
      return;
    int mm = atoi(line);
    int dd = atoi(pos1+1);
    int yy = atoi(pos2+1);
    if (!(1 <= mm && mm <= 12 && 1 <= dd && dd <= 31 && 2024 <= yy && yy <= 3000))
      return;
    // time.d.day = dd;
    // time.d.month = mm;
    // time.d.year = yy;
    // rtc.setDate(time.d.day, time.d.month, time.d.year);
    rtc_read_all();
    rtc_set_date(dd);
    rtc_set_month(mm);
    rtc_set_year2(yy-2000);
    rtc_write_all();
    FLOG("Date is now: ");
    LOG(fmt_date(_buf));
    LOG("\n");
  } else if (!strncasecmp(line, "daytime: ", 8)) { // hh:mm hh:mm
    line += 8;
    char *pos1 = index(line, ':');
    if (!pos1) return;
    char *pos2 = index(pos1+1, ' ');
    if (!pos2) return;
    char *pos3 = index(pos2+1, ':');
    if (!pos3) return;
    int h1 = atoi(line);
    int m1 = atoi(pos1+1);
    int h2 = atoi(pos2+1);
    int m2 = atoi(pos3+1);
    int a = 60*h1 + m1;
    int b = 60*h2 + m2;
    if (!(0 <= a && a <= b && b <= 24*60))
      return;
    opening_time = a;
    closing_time = b;
    FLOG("Daylight is now ");
    LOG(a/60); LOG(":"); if ((a%60)<10) LOG("0"); LOG(a%60);
    LOG(" to ");
    LOG(b/60); LOG(":"); if ((b%60)<10) LOG("0"); LOG(b%60);
    LOG("\n");
  } else if (!strncasecmp(line, "mode0: ", 7)) { // auto | manual r g b | gradient t | sim mm/dd hh:mm
    line += 7;
    parse_mode(0, line);
  } else if (!strncasecmp(line, "mode1: ", 7)) { // auto | manual r g b | gradient t | sim mm/dd hh:mm
    line += 7;
    parse_mode(1, line);
  } else if (!strncasecmp(line, "mode2: ", 7)) { // auto | manual r g b | gradient t | sim mm/dd hh:mm
    line += 7;
    parse_mode(2, line);
  } else if (!strncasecmp(line, "temp: ", 6)) { // lo hi lo50 hi50 lo60 hi60 lo70 hi70
    line += 6;
    for (int i = 0; i < 8 && line; i++) {
      float degF = atof(line);
      historical_weekly_stats[i] = (int)(degF * 10.0f + 0.5f);
      char *pos = index(line, ' ');
      if (pos)
        line = pos+1;
      else
        line = NULL;
    }
    FLOG("This week in history:");
    for (int i = 0; i < 8; i++) {
      LOG(" "); LOG(historical_weekly_stats[i]);
    }
    LOG("\n");
  }
}

void print_url(char *server, int port, char *path) {
#ifdef USE_HTTPS
  Serial.print("https://");
  Serial.print(server);
  if (port != 443) {
    Serial.print(":");
    Serial.print(port);
  }
#else
  Serial.print("http://");
  Serial.print(server);
  if (port != 80) {
    Serial.print(":");
    Serial.print(port);
  }
#endif
  Serial.print(path);
}

void http_send_get(char *server, char *path) {
  wifi_client.print("GET ");
  wifi_client.print(path);
  wifi_client.println(" HTTP/1.1");
  wifi_client.print("Host: ");
  wifi_client.println(server);
  wifi_client.println("Connection: close");
  wifi_client.println();
}

int responselen = 0;
char responsebuf[256];
void http_got(char c) {
  if (c == '\r' || c == '\n' || c == '\0') {
    if (responselen > 0) {
      responsebuf[responselen] = '\0';
      Serial.println(responsebuf);
      http_parse_line(responsebuf, responselen);
      responselen = 0;
    }
  } else if (responselen == sizeof(responsebuf) - 1) {
    FLOG("line too long");
  } else {
    responsebuf[responselen++] = c;
  }
}

bool http_get(char *server, char *path) {
  Serial.print("Fetching ");
  print_url(server, HTTP_PORT, path);
  Serial.print("\n");
  if (!wifi_client.connect(server, HTTP_PORT)) {
    Serial.println("  Connection failed.");
    return false;
  }

  http_send_get(server, path);

  int32_t n = 0; // total bytes
  responselen = 0;
  Serial.println("=== BEGIN RESPONSE ===");
  while (wifi_client.connected()) {
    while (wifi_client.available()) {
      char c = wifi_client.read();
      n++;
      http_got(c);
    }
  }
  http_got('\0');
  wifi_client.stop();
  Serial.println("==== END RESPONSE ====");
  boot.downloaded_bytes = n;
  return true;
}

void printWifiStatus() {
  Serial.println("Connected to WiFi...");
  Serial.print("  SSID: ");
  Serial.println(WiFi.SSID());
  IPAddress ip = WiFi.localIP();
  Serial.print("  Local IP Address: ");
  Serial.println(ip);

  long rssi = WiFi.RSSI();
  Serial.print("  Signal strength (RSSI): ");
  Serial.print(rssi);
  Serial.println(" dBm");
}

unsigned long wifi_connect_timestamp = 0;
bool ensure_wifi_connected() {
  if (WiFi.status() == WL_CONNECTED)
    return true;
  wifi_connect_timestamp = millis();
  strcpy(wifi_ssid, "");
  char *wifi_pass;
  for (int i = 0; ssid_and_pass[i]; i += 2) {
    strcpy(wifi_ssid, ssid_and_pass[i]);
    wifi_pass = ssid_and_pass[i+1];
    Serial.print("Attempting to connect to SSID using WPA/WPA2: ");
    Serial.println(wifi_ssid);
    int wifi_status = WiFi.begin(wifi_ssid, wifi_pass);
    if (wifi_status == WL_CONNECTED)
      return true;
    strcpy(wifi_ssid, "");
  }
  return false;
}

void fan_calibrate() {
  while (true) {
    while (Serial.available() && !isdigit(Serial.peek()))
      Serial.read();
    if (Serial.available() > 0) {
      int pwm = Serial.parseInt();
      LOG("FAN: "); LOG(pwm); LOG("\n");
      analogWrite(FAN_PIN, pwm);
    }
  }
}

void setup() {

  Serial.begin(9600);
  for (int i = 0; i < 3 && !Serial; i++)
    delay(1000);

  FLOG("Booting, Version " VERSION "\n");
    
  pinMode(FAN_PIN, OUTPUT);
  analogWrite(FAN_PIN, 0);

  // fan_calibrate();

  // pinMode(SHIFTREG_LATCH_PIN, OUTPUT);
  // pinMode(SHIFTREG_CLOCK_PIN, OUTPUT);
  // pinMode(SHIFTREG_DATA_PIN, OUTPUT);
  for (int i = 0; i < 9; i++)
    pinMode(RGB_PWM_PINS[i], OUTPUT);

  oled.begin();
  oled.setFont(u8g_font_6x10);
  // oled.setFontRefHeightExtendedText();
  // oled.setDefaultForegroundColor();
  oled.setFontPosTop();

  status = 0;
  boot_screen(&boot);
  delay(500);

  memset(&time, 0, sizeof(time));
  rtc_read_all();
  // rtc.begin();
  // rtc.setTime(time.t.hour, time.t.minute, 0);
  // rtc.setDate(time.d.day, time.d.month, time.d.year);
  FLOG("DS3231 RTC says time is ");
  LOG(fmt_time(_buf));
  FLOG("\nDS3231 RTC says date is ");
  LOG(fmt_date(_buf));
  FLOG("\n");
  status |= STATUS_RTC_WORKING;
  boot_screen(&boot);

  scan_sensors();
  status |= (sensors_found > 0) ? STATUS_SENSOR_FOUND : 0;
  boot_screen(&boot);

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("No WiFi module?");
  } else {
    status |= STATUS_WIFI_PRESENT;
    boot_screen(&boot);

    String fv = WiFi.firmwareVersion();
    if (fv < WIFI_FIRMWARE_LATEST_VERSION)
      Serial.println("WiFi firmware requires update.");

    for (int i = 0; i < 3; i++) {
      if (ensure_wifi_connected())
        break;
      else
        delay(3000);
    }
    if (ensure_wifi_connected()) {
      printWifiStatus();
      status |= STATUS_WIFI_CONNECTED;
      boot_screen(&boot);
    } else {
      Serial.println("Could not connect to wifi.");
    }

    if ((status & STATUS_WIFI_CONNECTED) && http_get(URL_SERVER, URL_PATH_STATUS))
      status |= STATUS_DATA_DOWNLOADED;
  }
  boot_screen(&boot);

  delay(1000);

  if (status != STATUS_READY) {
    status |= STATUS_ERROR;
    boot_screen(&boot);
  }

}

unsigned long variation_timestamp = 0;
struct hist_data today_in_history;
unsigned int variation_index = 0;

//bool strcpy_line_P(char *buf, int bufsize, __FlashStringHelper **p) {
bool strcpy_line_P(char *buf, int bufsize, const char **p) {
  if (!p) {
    buf[0] = '\0';
    return false;
  }
  int i = 0;
  while (true) {
    char c = pgm_read_byte((*p)++);
    if (c == '\r' && pgm_read_byte((*p)+1) == '\n')
      (*p)++;
    if (c == '\0' || c == '\r' || c == '\n') {
      buf[i] = '\0';
      return true;
    } else if (i < bufsize-1) {
      buf[i++] = c;
    }
  }
}

float find_sim_temp(int tube) {
  int sim_daynum = sim_day[tube]; // 60 - 181
  int sim_hour = sim_time[tube]/60; // minutes since midnight
  int d = 60, h = 1, t = 0;
 //  __FlashStringHelper *p = reinterpret(__FlashStringHelper*)bedford_historical;
  const char *p = bedford_historical;
  char *line = _buf;
  bool first = true;
  while (strcpy_line_P(line, sizeof(_buf), &p)) {
    if (line[0] == '\0')
      continue;
    // LOG("LINE: "); LOG(line); LOG("\n");
    // expecting one of:
    //   "mm/dd,hr,temp"
    //   ",hr,temp"
    char *slash = index(line, '/'); // line[2] == '/' ? &line[2] : NULL;
    if (slash) {
      int mm = atoi(line);
      int dd = atoi(slash+1);
      d = get_daynum(mm, dd);
    }
    if (!first && d > sim_daynum) return t*1.0f;
    char *comma1 = index(line, ',');
    if (!comma1 || (slash && slash > comma1)) continue;
    char *comma2 = index(comma1+1, ',');
    if (!comma2) continue;
    h = atoi(comma1+1);
    if (!first && d == sim_daynum && h > sim_hour) return t*1.0f;
    t = atoi(comma2+1);
    // Serial.println(t);
    first = false;
  }
  return t*1.0f;
}

unsigned long sim_timestamp = 0;
unsigned long print_timestamp = 0;

void do_lights() {
  rtc_read_all();
  bool closed = (time.t.hour*60+time.t.minute < opening_time || time.t.hour*60+time.t.minute >= closing_time);
  int daynum = get_daynum(rtc_cur_month(), rtc_cur_date());
  bool have_hist_data = false;
  float maxavg;
  if (60 <= daynum && daynum <= 181) {
    memcpy_P(&today_in_history, &historical[daynum-60], sizeof(struct hist_data));
    maxavg = today_in_history.maxavg;
    have_hist_data = true;
  } else {
    maxavg = historical_weekly_stats[1]/10.0f;
  }
  bool update_sim = rate_limit_expired(&sim_timestamp, 20000);
  for (int tube = 0; tube < 3; tube++) {
    if (mode[tube] == 0) { // auto
      if (closed) { 
        rgb[0] = 0;
        rgb[1] = 0;
        rgb[2] = 0;
      } else if (tube == 0) { // green for historical baseline
        rgb[0] = 0;
        rgb[1] = 255/3;
        rgb[2] = 0;
      } else if (tube == 1) { // current temperature
        float curtemp = temperature / 10.0f;
        if (rate_limit_expired(&print_timestamp, 5000)) {
          LOG("Outside temp "); LOG(curtemp); LOG(" F, deviates ");
          LOG(curtemp-maxavg);; LOG(" F from historical avg "); LOG(maxavg); LOG(" F\n");
        }
        gradient(curtemp - maxavg, &rgb[1*3]);
      } else if (tube == 2) { // natural variation
        if (have_hist_data && rate_limit_expired(&variation_timestamp, 10000)) {
          if (variation_index >= 21 || !today_in_history.maxdaily[variation_index])
            variation_index = 0;
          float oldtemp = (float)today_in_history.maxdaily[variation_index];
          LOG("Temp from "); LOG(1950+variation_index); LOG(" is "); LOG(oldtemp); LOG(" F, deviates ");
          LOG(oldtemp-maxavg); LOG(" F from historical avg "); LOG(maxavg); LOG(" F\n");
          gradient(oldtemp - maxavg, &rgb[2*3]);
          variation_index++;
        }
      }
    } else if (mode[tube] == 3) { // sim
      if (!update_sim)
        continue;
      int sim_daynum = sim_day[tube];
      bool sim_have_hist_data = false;
      struct hist_data sim_today_in_history;
      memcpy_P(&sim_today_in_history, &historical[sim_daynum-60], sizeof(struct hist_data));
      float sim_maxavg = sim_today_in_history.maxavg;
      float sim_temp = find_sim_temp(tube);
      if (tube == 0) { // green for historical baseline
        rgb[0] = 0;
        rgb[1] = 255/3;
        rgb[2] = 0;
        LOG("Simulated outside temp is "); LOG(sim_temp); LOG(" F, deviates ");
        LOG(sim_temp-sim_maxavg); LOG(" F from historical avg "); LOG(sim_maxavg); LOG(" F\n");
      } else if (tube == 1) { // current temperature
        gradient(sim_temp - sim_maxavg, &rgb[1*3]);
      } else if (tube == 2) { // natural variation
        if (rate_limit_expired(&variation_timestamp, 2000)) {
          if (variation_index >= 21 || !sim_today_in_history.maxdaily[variation_index])
            variation_index = 0;
          float oldtemp = (float)sim_today_in_history.maxdaily[variation_index];
          LOG("Simulated temp from "); LOG(1950+variation_index); LOG(" is "); LOG(oldtemp); LOG(" F, deviates ");
          LOG(oldtemp-sim_maxavg); LOG(" F from historical avg "); LOG(sim_maxavg); LOG(" F\n");
          gradient(oldtemp - sim_maxavg, &rgb[2*3]);
          variation_index++;
        }
      }
    }
  }
  for (int i = 0; i < 9; i++) {
    analogWrite(RGB_PWM_PINS[i], rgb[i]);
  }
}

unsigned long http_connect_timestamp = 0;

bool http_update() {
  if (WiFi.status() != WL_CONNECTED) {
    if (!rate_limit_expired(&wifi_connect_timestamp, 30000) || !ensure_wifi_connected()) {
      http_got('\0');
      return false;
    }
  }
  if (!wifi_client.connected()) {
    http_got('\0');
    if (!rate_limit_expired(&http_connect_timestamp, 30000))
      return false;
    Serial.print("Connecting to ");
    print_url(URL_SERVER, HTTP_PORT, URL_PATH_MONITOR);
    Serial.print("\n");
    if (!wifi_client.connect(URL_SERVER, HTTP_PORT)) {
      Serial.println("  Connection failed.");
      return false;
    }
    http_send_get(URL_SERVER, URL_PATH_MONITOR);
    http_update();
  }
  bool gotsome = false;
  while (wifi_client.available()) {
    char c = wifi_client.read();
    http_got(c);
    gotsome = true;
  }
  return gotsome;
}

void adjust_fan() {
  rtc_temp temp = { 0xff, };
  rtc_read(0x11, &temp, sizeof(temp));
  circuit_temperature = temp.msb * 90/5 + 320;
  float interior_temp = temp.msb * 9.0f/5.0f + 32.0f;
  byte new_fan_speed;
  if (interior_temp < 70) {
    new_fan_speed = 0;
  } else if (interior_temp < 75) {
    new_fan_speed = 2;
  } else if (interior_temp < 80) {
    new_fan_speed = 4;
  } else if (interior_temp < 85) {
    new_fan_speed = 8;
  } else if (interior_temp < 90) {
    new_fan_speed = 16;
  } else {
    new_fan_speed = 255;
  }
  if (new_fan_speed == fan_speed)
    return;
  LOG("Circuit temp "); LOG(circuit_temperature/10); LOG("."); LOG(circuit_temperature%10);
  LOG(" F, fan speed = "); LOG(new_fan_speed); LOG("\n");
  analogWrite(FAN_PIN, new_fan_speed);
  fan_speed = new_fan_speed;
}

unsigned long sensor_timestamp = 0;
unsigned long fan_timestamp = 0;
float sim_txx = 0;

void parse_serial(char *line, int linelen) {
  if (linelen == 0) {
    if (mode[0] == 3) {
      sim_time[0] += 60;
      if (sim_time[0] >= 60*24) {
        sim_time[0] = 0;
        sim_day[0]++;
        if (sim_day[0] > 181)
          sim_day[0] = 60;
      }
      sim_day[1] = sim_day[2] = sim_day[0];
      sim_time[1] = sim_time[2] = sim_time[0];
      LOG("Simulating day "); LOG(sim_day[0]); LOG(" hour "); LOG(sim_time[0]/60); LOG("\n");
      sim_timestamp = 0;
      screen_refresh_timestamp = 0;
    } else if (mode[0] == 2) {
      sim_txx += 0.5f;
      if (sim_txx >= 6.0f)
        sim_txx = -6.0f;
      for (int tube = 0; tube < 3; tube++) {
        gradient(sim_txx, &rgb[3*tube]);
      }
      LOG("simulate deviation "); LOG(sim_txx); LOG(" from avg, RGB = ");
      LOG(rgb[0]); LOG(" "); LOG(rgb[1]); LOG(" "); LOG(rgb[2]); LOG("\n");
    }
    return;
  }
  if (!strncmp(line, "fan ", 4)) {
    int f = atoi(line+4);
    analogWrite(FAN_PIN, f);
    LOG("Temporary fan speed = "); LOG(f); LOG("\n");
    fan_timestamp = millis();
  } else if (!strncmp(line, "sim ", 4)) {
    int d = atoi(line+4);
    if (d < 60) d = 60;
    else if (d > 181) d = 181;
    sim_time[0] = sim_time[1] = sim_time[2] = 0;
    sim_day[0] = sim_day[1] = sim_day[2] = d;
    mode[0] = mode[1] = mode[2] = 3;
    LOG("Simulating day "); LOG(sim_day[0]); LOG(" hour "); LOG(sim_time[0]/60); LOG("\n");
    sim_timestamp = 0;
  } else if (!strncmp(line, "grad ", 5)) {
    sim_txx = atof(line+5);
    for (int tube = 0; tube < 3; tube++) {
      gradient(sim_txx, &rgb[3*tube]);
      mode[tube] = 2;
    }
    LOG("simulate deviation "); LOG(sim_txx); LOG(" from avg, RGB = ");
      LOG(rgb[0]); LOG(" "); LOG(rgb[1]); LOG(" "); LOG(rgb[2]); LOG("\n");
  } else if (!strncmp(line, "rgb ", 4)) {
    char *space1 = index(line+4, ' ');
    if (!space1) return;
    char *space2 = index(space1+1, ' ');
    if (!space2) return;
    int r = atoi(line+4);
    int g = atoi(space1+1);
    int b = atoi(space2+1);
    for (int tube = 0; tube < 3; tube++) {
      rgb[3*tube+0] = r;
      rgb[3*tube+1] = g;
      rgb[3*tube+2] = b;
      mode[tube] = 1;
    }
    LOG("RGB = "); LOG(rgb[0]); LOG(" "); LOG(rgb[1]); LOG(" "); LOG(rgb[2]); LOG("\n");
  } else if (!strcmp(line, "auto")) {
    mode[0] = mode[1] = mode[2] = 0;
    LOG("auto mode\n");
  }
}

char serial_buf[80];
int serial_cnt = 0;
void loop() {

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n' || c == '\0') {
      serial_buf[serial_cnt] = '\0';
      parse_serial(serial_buf, serial_cnt);
      serial_cnt = 0;
    } else if (serial_cnt < sizeof(serial_buf)-1) {
      serial_buf[serial_cnt++] = c;
    }
  }

  if (rate_limit_expired(&fan_timestamp, 30000)) {
    adjust_fan();
  }

  if (sensors_found && rate_limit_expired(&sensor_timestamp, 10000)) {
    float degreesF = sensors.getTempF();
    temperature = (int)(10.0f * degreesF + 0.5f);
    // FLOG("Sensor Temperature: "); LOG(degreesF); LOG("F\n");
  }

  if (rate_limit_expired(&screen_refresh_timestamp, 1000)) {
    info_screen(0);
    do_lights();
  }

  http_update();
}

