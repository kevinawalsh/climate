#include <U8g2lib.h>
#include <SPI.h>
#include <WiFiNINA.h>
#include <DS18B20.h>
#include <RTCZero.h>
#include <stdarg.h>

#include "climate.h"
#include "arduino_secrets.h" 

#define VERSION "0.1"

// URL for fetching climate data, current date and time, etc.
// #define USE_HTTPS
// #define HTTP_PORT 443
// #define HTTP_PORT 80
#define HTTP_PORT 8888
#define URL_SERVER "assembler.kwalsh.org"
#define URL_PATH_STATUS "/status"
#define URL_PATH_MONITOR "/monitor"

// pins 0 to 8 are for RGB Strip Lights, using PWM mode
byte RGB_PWM_PINS[] = { 1, 0, 2, 4, 3, 5, 7, 8, 6 };

// OneWire bus for Dallas Semiconductor DS18B20 Temperature Sensor
#define ONEWIRE_PIN 10 // pin 10 is OneWire data, with external pullup

// #define SHIFTREG_LATCH_PIN 5  // pin 5 is 74HC595 latch
// #define SHIFTREG_CLOCK_PIN 6  // pin 6 is 74HC595 clock
// #define SHIFTREG_DATA_PIN 4   // pin 4 is 74HC595 data

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
byte mode = 0; // 0 = auto, 1 = manual, 2 = gradient
byte rgb[9];

char wifi_ssid[] = SECRET_SSID;
char wifi_pass[] = SECRET_PASS;
#ifdef USE_HTTPS
WiFiSSLClient wifi_client;
#else
WiFiClient wifi_client;
#endif

DS18B20 sensors(ONEWIRE_PIN); // Temperature Sensors
U8G2_SH1106_128X64_NONAME_1_HW_I2C oled(U8G2_R0); // 2c oled using A4=SDA and A5=SCL
RTCZero rtc; // internal Real Time Clock for Arduino MKR 1010 board

char _buf[128]; // used by parsing, serial_printf, oled drawing loops

uint16_t openTime, closeTime; // minutes after midnight
struct rtc_time time; // current time
bool inDST; // whether daylight savings time is active
int temperature = 0; // current tempeature, from DS18B20 sensor (primary) or RTC module (secondary)
int sensors_found = 0;
int historical_weekly_stats[8]; // min, max, min50, max50, min60, max60, min70, max70

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

byte cold[] = { 0, 0, 255 };
byte mid[] = { 0, 255, 0 };
byte hot[] = { 255, 0, 0 };

void interpolate(float v, byte *lo, byte *hi, byte *rgb) {
  rgb[0] = (int)(lo[0]*(1.0f-v) + hi[0]*v);
  rgb[1] = (int)(lo[1]*(1.0f-v) + hi[1]*v);
  rgb[2] = (int)(lo[2]*(1.0f-v) + hi[2]*v);
}

void gradient(float txx, byte *rgb) {
  if (txx < -10.0f)
    txx = -10.0f;
  else if (txx > 20.0f)
    txx = 20.0f;
  if (txx < 0)
    interpolate((txx + 10.0f) / 10.0f, cold, mid, rgb);
  else
    interpolate(txx / 20.0f, mid, hot, rgb);
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
  } else if (!strncasecmp(line, "mode: ", 6)) { // auto | manual r g b r g b r g b | gradient t
    line += 6;
    if (!strcasecmp(line, "auto")) {
      mode = 0;
      FLOG("Mode is now: auto\n");
    } else if (!strncasecmp(line, "manual ", 7)) {
      line += 7;
      mode = 1;
      for (int i = 0; i < 9; i++) {
        rgb[i] = atoi(line);
        char *pos = index(line, ' ');
        if (pos)
          line = pos+1;
      }
      FLOG("Mode is now: manual");
      for (int i = 0; i < 9; i++) {
        LOG(" "); LOG(rgb[i]);
      }
      LOG("\n");
    } else if (!strncasecmp(line, "gradient ", 9)) {
      line += 9;
      mode = 2;
      float txx = atof(line);
      gradient(txx, &rgb[0]);
      gradient(txx, &rgb[3]);
      gradient(txx, &rgb[6]);
    }
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
  Serial.print("Attempting to connect to SSID using WPA/WPA2: ");
  Serial.println(wifi_ssid);
  int wifi_status = WiFi.begin(wifi_ssid, wifi_pass);
  return (wifi_status == WL_CONNECTED);
}

void setup() {
  Serial.begin(9600);
  for (int i = 0; i < 3 && !Serial; i++)
    delay(1000);

  FLOG("Booting, Version " VERSION "\n");

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

    for (int i = 0; i < 4; i++) {
      if (ensure_wifi_connected())
        break;
      else
        delay(5000);
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

void do_lights() {
  if (mode == 0) {
    // auto
    gradient(temperature - historical_weekly_stats[1]/10.0f, &rgb[0]);
  }
  // updateShiftRegister(leds);
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

unsigned long sensor_timestamp = 0;

void loop() {

  if (sensors_found && rate_limit_expired(&sensor_timestamp, 10000)) {
    float degreesF = sensors.getTempF();
    temperature = (int)(10.0f * degreesF + 0.5f);
    FLOG("Sensor Temperature: "); LOG(degreesF); LOG("F\n");
  }

  if (rate_limit_expired(&screen_refresh_timestamp, 1000)) {
    info_screen(0);
    do_lights();
  }

  http_update();
}

