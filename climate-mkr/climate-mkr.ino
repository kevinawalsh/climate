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

// -------------- Software Configuration --------------

#define DEBUG 1

// See arduino_screts.h for list of wifi SSID and password pairs, terminated
// with a zero, like:
// #define SSID_AND_PASS { "WorkGuest2G", "", "HomeWiFi", "pass123", 0 }
char *ssid_and_pass[] = SSID_AND_PASS;

// URL for checking status and monitoring info like current date, time,
// opening/closing hours, mode (auto, manual, simulated).
#define STATUS_URL "http://assembler.kwalsh.org:8888/status"
#define MONITOR_URL "http://assembler.kwalsh.org:8888/monitor"

// -------------- Hardware Configuration --------------

// pins 0 to 8 are for RGB Strip Lights, using PWM mode
byte RGB_PWM_PINS[] = { 1, 2, 0, 4, 5, 3, 7, 6, 8 };

// OneWire bus for Dallas Semiconductor DS18B20 Temperature Sensor
#define ONEWIRE_PIN 10 // pin 10 is OneWire data, with external pullup

#define OLED_SDA_PIN 11 // pin 11 is OLED and DS18B20 RTC bus SDA
#define OLED_SCL_PIN 12 // pin 12 is OLED and DS18B20 RTC bus SCL

// #define SHIFTREG_DATA_PIN 4   // pin 4 is 74HC595 data
// #define SHIFTREG_LATCH_PIN 5  // pin 5 is 74HC595 latch
// #define SHIFTREG_CLOCK_PIN 6  // pin 6 is 74HC595 clock

#define FAN_PIN A3 // pin A3 is fan mosfet, with externall pulldown

// -------------- End of Configuration --------------

#if DEBUG
#define DBG(code) code
#else
#define DBG(code) do {} while (0)
#endif
#define LOG(msg) DBG(Serial.print(msg))
#define FLOG(msg) DBG(Serial.print(F(msg)))
#define LOGF(msg...) DBG(serial_printf(msg))

char _buf[128]; // used by parsing, serial_printf, oled drawing loops

byte rgb[9];

char wifi_ssid[64] = { 0, };
WiFiClient wifi_client;

DS18B20 sensors(ONEWIRE_PIN); // Temperature Sensors
U8G2_SH1106_128X64_NONAME_1_HW_I2C oled(U8G2_R0); // 2c oled using pin 11 as SDA and pin 12 as SCL
RTCZero rtc; // internal Real Time Clock for Arduino MKR 1010 board

int opening_time = 8*60, closing_time = 21*60; // minutes after midnight
struct rtc_time time; // current time

int baseline10 = INVALID_TEMP; // historical baseline temperature, in 0.1*F units
int temperature10 = INVALID_TEMP; // current outside temperature, from DS18B20 sensor, in 0.1*F units
int old_temperature10 = INVALID_TEMP;
int circuit_temperature10 = INVALID_TEMP; // current temperature from RTC module, interior of electronics enclosure, in 0.1*F units

byte fan_speed = 0; // from 0 to 255, based on intenrnal circuit temperature

#define MODE_AUTO 0
// Mode 0 is "auto"
//   tube 0 is green, representing historical average baseline weekly max avg
//   tube 1 varies, representing current temperature deviation from baseline
//   tube 2 varies, representing natural variation 1950-1970 max daily from baseline
#define MODE_MANUAL 1
// Mode 1 is "manual"
//   each tube is set manually by giving RGB values or a deviation from baseline
#define MODE_SIMULATE 2
// Mode 2 is "simulation"
//   tube 0 is like "auto"
//   tube 1 is like "auto", but using temperature from a chosen day and time in 2023
//   tube 2 is like "auto", but using the chosen day
byte mode = MODE_AUTO;

// manual mode variables
struct colors manual_settings[3];

unsigned long screen_refresh_timestamp = 0;
unsigned long sensor_timestamp = 0;
unsigned long fan_timestamp = 0;
unsigned long http_connect_timestamp = 0;
unsigned long wifi_connect_timestamp = 0;
unsigned long variation_timestamp = 0;
unsigned long sim_timestamp = 0;
unsigned long print_timestamp = 0;

// auto mode variables
struct hist_data http_historical_data;
int http_historical_day = 0;
struct hist_data today_in_history; // also used by simulation mode
int variation_index = 0; // also used by simulation mode

// simulation mode variables
int sim_day = 60; // chosen simulation day number, 1 - 365
int sim_time = 0; // minute number, 0 - 1439
int sim_baseline10 = INVALID_TEMP;
int sim_temperature10 = INVALID_TEMP;
int sim_old_temperature10 = INVALID_TEMP;

// boot screen variables
struct boot_msgs {
    int ds_model;
    byte ds_addr[8];
    int32_t downloaded_bytes;
};
boot_msgs boot = { -1, {0,}, -1 };
#define STATUS_RTC_WORKING     (1 << 0) // DS18B20 Temperature Sensor was found
#define STATUS_SENSOR_FOUND    (1 << 1) // DS18B20 Temperature Sensor was found
#define STATUS_WIFI_PRESENT    (1 << 2) // WiFi module found
#define STATUS_WIFI_CONNECTED  (1 << 3) // WiFi connected
#define STATUS_DATA_DOWNLOADED (1 << 4) // data downloaded from website
#define STATUS_READY           (0x1f)
#define STATUS_ERROR           (1 << 7) // something is wrong
byte status = 0;


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

int sensors_found = 0;
void scan_sensors() {
  temperature10 = INVALID_TEMP;
  // FLOG("Scanning for sensors...\n");
  sensors.resetSearch();
  for (int i = 0; sensors.selectNext(); i++) {
    sensors_found++;
    sensors_identify(i, &boot.ds_model, boot.ds_addr);
    float degreesF = sensors.getTempF();
    temperature10 = (int)(10.0f * degreesF + 0.5f);
    FLOG("Sensor Temperature: "); LOG(degreesF); LOG(" F\n");
    float curtemp = temperature10 / 10.0f;
    if (rate_limit_expired(&print_timestamp, 5000)) {
      LOG("Outside temp "); LOG(degreesF); LOG(" F");
      if (baseline10 != INVALID_TEMP) {
        LOG(" deviates "); LOG(degreesF-(baseline10/10.0f)); LOG(" F from historical avg "); LOG(baseline10/10.0f); LOG(" F\n");
      } else {
        LOG(" with unknown baseline temperature\n");
      }
    }
    break;
  }
  // FLOG("Found "); LOG(sensors_found); FLOG(" sensors.\n");
}

int month_offset[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
int get_daynum(int month, int day) {
    return month_offset[month-1] + day;
}

byte hex2nibble(char c) {
  if ('0' <= c && c <= '9')
    return c-'0';
  else if ('a' <= c && c <= 'f')
    return 0xA + (c-'a');
  else if ('A' <= c && c <= 'F')
    return 0xA + (c-'A');
  else
    return 0;
}

// #RGB     (short hex format)
// #RRGGBB  (full hex format)
// TXX      (in range -30.0 to +30.0)
void parse_manual(const char *line, struct colors *chosen) {
  if (line[0] == '#' && 
      line[1] && line[2] && line[3] && (!line[4] == ' ' || !line[4])) {
    chosen->txx = -300.0f; // invalid
    chosen->rgb[0] = hex2nibble(line[1]) << 4;
    chosen->rgb[2] = hex2nibble(line[2]) << 4;
    chosen->rgb[2] = hex2nibble(line[3]) << 4;
  } else if (line[0] == '#') {
    chosen->txx = -300.0f; // invalid
    chosen->rgb[0] = (hex2nibble(line[1]) << 4) | hex2nibble(line[2]);
    chosen->rgb[2] = (hex2nibble(line[3]) << 4) | hex2nibble(line[4]);
    chosen->rgb[2] = (hex2nibble(line[5]) << 4) | hex2nibble(line[6]);
  } else {
    chosen->txx = atof(line);
  }
}

void print_manual(struct colors *chosen) {
  if (-30.0f <= chosen->txx && chosen->txx <= +30.0f) {
    Serial.print("deviation ");
    Serial.print(chosen->txx);
    Serial.print(" F from baseline, rgb = ");
    gradient(chosen->txx, chosen->rgb);
  }
  char msg[9];
  sprintf(msg, "#%02x%02x%02x\n", chosen->rgb[0], chosen->rgb[1], chosen->rgb[2]);
}

void parse_simulate(const char *line) { // mm/dd hh:mm
  char *slash = index(line, '/');
  if (slash) {
    int mm = atoi(slash-2);
    int dd = atoi(slash+1);
    sim_day = get_daynum(mm, dd);
  } else {
    // sim_day = 60;
  }
  char *colon = index(line, ':');
  if (colon) {
    int hh = atoi(colon-2);
    int mm = atoi(colon+1);
    sim_time = 60*hh + mm;
  } else {
    // sim_time = 12*60;
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
  } else if (!strncasecmp(line, "mode: ", 6)) { // auto | manual rgbx rgbx rgbx | simulate day hh mm
    line += 6;
    if (!strcasecmp(line, "auto")) {
      mode = MODE_AUTO;
      variation_index = 0;
      variation_timestamp = millis();
      LOG("Mode: auto\n");
    } else if (!strncasecmp(line, "manual ", 7)) {
      line += 7;
      char *sp1 = index(line, ' ');
      if (!sp1) return;
      char *sp2 = index(sp1+1, ' ');
      if (!sp2) return;
      parse_manual(line, &manual_settings[0]);
      parse_manual(sp1+1, &manual_settings[1]);
      parse_manual(sp1+2, &manual_settings[2]);
      mode = MODE_MANUAL;
      LOG("Mode: manual\n");
    } else if (!strncasecmp(line, "simulate ", 9)) {
      line += 9;
      parse_simulate(line);
      mode = MODE_SIMULATE;
      start_simulation();
      variation_index = 0;
      sim_timestamp = variation_timestamp = millis();
      LOG("Mode: simulate\n");
    }
  } else if (!strncasecmp(line, "historical: ", 12)) { // mm/dd baseline max1950 max1951 max1952 ...
    line += 12;
    char *slash = index(line, '/');
    if (!slash) return;
    char *space = index(slash+1, ' ');
    if (!space) return;
    int mm = atoi(slash-2);
    int dd = atoi(slash+1);
    http_historical_day = get_daynum(mm, dd);
    memset(&http_historical_data, 0, sizeof(http_historical_data));
    line = space+1;
    http_historical_data.maxavg = atof(line);
    for (int i = 0; i < 21; i++) {
      space = index(line, ' ');
      if (!space) break;
      line = space+1;
      http_historical_data.maxdaily[i] = atoi(line);
    }
  }
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

bool http_open_get(char *url) {
  if (strlen(url) >= sizeof(responsebuf))
    return false;
  char *buf = responsebuf;
  strcpy(buf, url);
  char *server, *path;
  int port;
  bool useSSL;
  if (!strncasecmp(buf, "http://", 7)) {
    useSSL = false;
    port = 80;
    server = buf + 7;
    buf += 7;
  } else if (!strncasecmp(buf, "https://", 8)) {
    useSSL = true;
    port = 443;
    server = buf + 8;
    buf += 8;
  } else {
    Serial.print("Unrecognized URL: ");
    Serial.println(url);
    return false;
  }
  char *colon = index(buf, ':');
  char *slash = index(buf, '/');
  if (colon && (!slash || colon < slash)) {
    // "http://www.whatever.org:NNN/some/file"
    *colon = '\0';
    port = atoi(colon+1);
    path = slash;
  } else if (slash) {
    memmove(&server[-1], server, slash-server);
    slash[-1] = '\0';
    path = slash;
  } else {
    path = "/";
  }

  Serial.print("Fetching ");
  Serial.println(url);
  if (useSSL) {
    if (!wifi_client.connectSSL(server, port)) {
      Serial.println("  Secure connection failed.");
      return false;
    }
  } else {
    if (!wifi_client.connect(server, port)) {
      Serial.println("  Connection failed.");
      return false;
    }
  }

  http_send_get(server, path);
  return true;
}

bool http_get(char *url) {
  if (!http_open_get(url))
    return false;
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

// void fan_calibrate() {
//   while (true) {
//     while (Serial.available() && !isdigit(Serial.peek()))
//       Serial.read();
//     if (Serial.available() > 0) {
//       int pwm = Serial.parseInt();
//       LOG("FAN: "); LOG(pwm); LOG("\n");
//       analogWrite(FAN_PIN, pwm);
//     }
//   }
// }

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

    if ((status & STATUS_WIFI_CONNECTED) && http_get(STATUS_URL))
      status |= STATUS_DATA_DOWNLOADED;
  }
  boot_screen(&boot);

  delay(1000);

  if (status != STATUS_READY) {
    status |= STATUS_ERROR;
    boot_screen(&boot);
  }

}

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

float find_sim_temp() {
  int sim_hour = sim_time / 60;
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
    if (!first && d > sim_day) return t*1.0f;
    char *comma1 = index(line, ',');
    if (!comma1 || (slash && slash > comma1)) continue;
    char *comma2 = index(comma1+1, ',');
    if (!comma2) continue;
    h = atoi(comma1+1);
    if (!first && d == sim_day && h > sim_hour) return t*1.0f;
    t = atoi(comma2+1);
    // Serial.println(t);
    first = false;
  }
  return t*1.0f;
}

float pick_next_historical_datapoint() {
  if (variation_index >= 21 || !today_in_history.maxdaily[variation_index])
    variation_index = 0;
  float baseline = today_in_history.maxavg;
  float oldtemp = (float)today_in_history.maxdaily[variation_index];
  LOG("Max temperature from this day in "); LOG(1950+variation_index); LOG(" was "); LOG(oldtemp); LOG(" F, deviates ");
  LOG(oldtemp-baseline); LOG(" F from historical avg "); LOG(baseline); LOG(" F\n");
  variation_index++;
  return oldtemp;
}

void set_lights() {
  for (int i = 0; i < 9; i++) {
    analogWrite(RGB_PWM_PINS[i], rgb[i]);
  }
}

void auto_lights() {
  rtc_read_all();
  bool closed = (time.t.hour*60+time.t.minute < opening_time || time.t.hour*60+time.t.minute >= closing_time);
  int daynum = get_daynum(rtc_cur_month(), rtc_cur_date());
  // use data from http when possible
  // otherwise, fall back to built-in data when possible
  // otherwise, set all lights to dim white as failure indicator
  if (http_historical_day == daynum) {
    memcpy(&today_in_history, &http_historical_data, sizeof(struct hist_data));
  } else if (60 <= daynum && daynum <= 181) {
    memcpy_P(&today_in_history, &historical[daynum-60], sizeof(struct hist_data));
  } else {
    baseline10 = INVALID_TEMP;
    memset(rgb, closed ? 0 : 10, 9);
    set_lights();
    return;
  }
  float temperature = temperature10 / 10.0f;
  float baseline = today_in_history.maxavg;
  baseline10 = (int)(10.0f * baseline + 0.5f);

  // outside of daylight hours, turn off
  if (closed) {
    memset(rgb, 0, 9);
    set_lights();
    return;
  }

  // tube 0 is green, representing baseline 
  rgb[0] = 0;
  rgb[1] = 255/3;
  rgb[2] = 0;
 
  // tube 1 varies, representing current temperature deviation from baseline
  gradient(temperature - baseline, &rgb[3]);

  // tube 2 varies, representing historical variation from baseline
  if (rate_limit_expired(&variation_timestamp, 10000)) {
    float old = pick_next_historical_datapoint();
    old_temperature10 = (int)(10.0f * old + 0.5f);
  }
  gradient(old_temperature10/10.0f - baseline, &rgb[6]);

  set_lights();
}

void manual_lights() {

  for (int t = 0; t < 3; t++) {
    struct colors *chosen = &manual_settings[t];
    if (-30.0f <= chosen->txx && chosen->txx <= +30.0f) {
      gradient(chosen->txx, &rgb[3*t]);
    } else {
      memcpy(&rgb[3*t], chosen->rgb, 3);
    }
  }
  
  set_lights();
}

void start_simulation() {
  if (sim_time < 0 || sim_time >= 24*60)
    sim_time = 12*60;
  if (sim_day < 60 || sim_day > 181)
    sim_day = 60;

  // always use built-in historical data
  memcpy_P(&today_in_history, &historical[sim_day-60], sizeof(struct hist_data));
  float sim_baseline = today_in_history.maxavg;
  sim_baseline10 = (int)(10.0f * sim_baseline + 0.5f);

  // take temperature from built-in record of 2023
  float sim_temperature = find_sim_temp();
  sim_temperature10 = (int)(10.0f * sim_temperature + 0.5f);

  LOG("Simulation for day "); LOG(sim_day);
  LOG(" "); LOG(sim_time/60); LOG(":"); if (sim_time%60<10) LOG("0"); LOG(sim_time%60);
  LOG(" outside temperature was "); LOG(sim_temperature); LOG(" F, deviates ");
  LOG(sim_temperature-sim_baseline); LOG(" F from historical avg "); LOG(sim_baseline); LOG(" F\n");
}

void sim_advance() {
  sim_time += 60;
  if (sim_time >= 24*60) {
    sim_time = 0;
    sim_day++;
  }
  start_simulation();
}

void sim_lights() {

  if (rate_limit_expired(&sim_timestamp, 20000)) {
    sim_advance();
  }
  
  bool closed = (sim_time < opening_time || sim_time >= closing_time);
  float sim_old_temperature = pick_next_historical_datapoint();
  sim_old_temperature10 = (int)(10.0f * sim_old_temperature + 0.5f);
  float temperature = sim_temperature10 / 10.0f;
  float baseline = sim_baseline10 / 10.0f;

  // tube 0 is green, representing baseline 
  rgb[0] = 0;
  rgb[1] = 255/3;
  rgb[2] = 0;
 
  // tube 1 varies, representing simulated temperature deviation from baseline
  gradient(temperature - baseline, &rgb[3]);

  // tube 2 varies, representing historical variation from baseline
  gradient(sim_old_temperature - baseline, &rgb[6]);

  // dim everything by a factor of 3 when simulating closing time
  if (closed) {
    for (int i = 0; i < 9; i++)
      rgb[i] /= 3;
  }

  set_lights();
}

bool http_update() {
  if (WiFi.status() != WL_CONNECTED) {
    http_got('\0');
    if (!rate_limit_expired(&wifi_connect_timestamp, 30000))
      return false;
    if (!ensure_wifi_connected())
      return false;
  }
  if (!wifi_client.connected()) {
    http_got('\0');
    if (!rate_limit_expired(&http_connect_timestamp, 30000))
      return false;
    if (!http_open_get(MONITOR_URL))
      return false;
  }
  while (wifi_client.available()) {
    char c = wifi_client.read();
    http_got(c);
  }
  return true;
}

void adjust_fan() {
  rtc_temp temp = { 0xff, };
  rtc_read(0x11, &temp, sizeof(temp));
  circuit_temperature10 = temp.msb * 90/5 + 320;
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
  LOG("Circuit temp "); LOG(circuit_temperature10/10); LOG("."); LOG(abs(circuit_temperature10)%10);
  LOG(" F, fan speed = "); LOG(new_fan_speed); LOG("\n");
  analogWrite(FAN_PIN, new_fan_speed);
  fan_speed = new_fan_speed;
}

void parse_serial(char *line, int linelen) {
  screen_refresh_timestamp = 0;
  if (linelen == 0) {
    if (mode == MODE_SIMULATE) {
      sim_advance();
      sim_timestamp = millis();
    } else if (mode == MODE_MANUAL) {
      for (int t = 0; t < 3; t++) {
        struct colors *chosen = &manual_settings[t];
        if (-30.0f <= chosen->txx && chosen->txx <= +30.0f) {
          chosen->txx += 0.5;
          if (chosen->txx >= 6.0f)
            chosen->txx = -8.0f;
          gradient(chosen->txx, &rgb[3*t]);
          LOG("Tube "); LOG(t); LOG(" deviation "); LOG(chosen->txx); LOG(" -> color is #");
          char msg[8];
          sprintf(msg, "%02x%02x%02x\n", chosen->rgb[0], chosen->rgb[1], chosen->rgb[2]);
          LOG(msg);
        }
      }
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
    sim_time = 0;
    sim_day = d;
    mode = MODE_SIMULATE;
    start_simulation();
    variation_index = 0;
    sim_timestamp = variation_timestamp = millis();
  } else if (!strncmp(line, "tube 0 ", 7)) {
    mode = MODE_MANUAL;
    parse_manual(line+7, &manual_settings[0]);
    print_manual(&manual_settings[0]);
  } else if (!strncmp(line, "tube 1 ", 7)) {
    mode = MODE_MANUAL;
    parse_manual(line+7, &manual_settings[1]);
    print_manual(&manual_settings[1]);
  } else if (!strncmp(line, "tube 2 ", 7)) {
    mode = MODE_MANUAL;
    parse_manual(line+7, &manual_settings[2]);
    print_manual(&manual_settings[2]);
  } else if (!strcmp(line, "auto")) {
    mode = MODE_AUTO;
    variation_index = 0;
    variation_timestamp = millis();
    LOG("auto mode\n");
  }
}

char serial_buf[80]; // serial console for debugging
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

  if (rate_limit_expired(&sensor_timestamp, 10000)) {
    scan_sensors();
  }

  if (rate_limit_expired(&screen_refresh_timestamp, 1000)) {
    if (mode == 0) {
      auto_lights();
      info_screen(0);
    } else if (mode == 1) {
      manual_lights();
      manual_screen();
    } else if (mode == 2) {
      sim_lights();
      sim_screen();
    }
  }

  http_update();
}

