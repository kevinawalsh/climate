#include <U8g2lib.h>
#include <SPI.h>
#include <WiFiNINA.h>
#include <DS18B20.h>
#include <stdarg.h>

#include "arduino_secrets.h" 

#define VERSION "0.1"

// URL for fetching climate data
#define USE_HTTPS
#define URL_SERVER "assembler.kwalsh.org"
#define URL_PATH "/climate/"

// OneWire bus for Dallas Semiconductor DS18B20 Temperature Sensor
#define ONEWIRE_PIN 7 // pin 7 is OneWire data, with external pullup
#define SHIFTREG_LATCH_PIN 5  // pin 5 is 74HC595 latch
#define SHIFTREG_CLOCK_PIN 6  // pin 6 is 74HC595 clock
#define SHIFTREG_DATA_PIN 4   // pin 4 is 74HC595 data

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

byte status = 0;

int wifi_status = WL_IDLE_STATUS;

char wifi_ssid[] = SECRET_SSID;
char wifi_pass[] = SECRET_PASS;
#ifdef USE_HTTPS
#define HTTP_PORT 443
WiFiSSLClient wifi_client;
#else
#define HTTP_PORT 80
WiFiClient wifi_client;
#endif

DS18B20 sensors(ONEWIRE_PIN); // Temperature Sensors
// U8GLIB_SH1106_128X64 oled(U8G_I2C_OPT_NONE); // 2c oled using A4=SDA and A5=SCL
U8G2_SH1106_128X64_NONAME_1_HW_I2C oled(U8G2_R0); // 2c oled using A4=SDA and A5=SCL

char _buf[128]; // used by parsing, serial_printf, oled drawing loops

int temperature;
int sensors_found = 0;

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

void oled_drawStr(int x, int y, const char *s) {
  oled.drawStr(x, y, s);
}

void oled_drawStr(int x, int y, const __FlashStringHelper *f) {
  strncpy_P(_buf, (PGM_P)f, sizeof(_buf) - 1); 
  _buf[sizeof(_buf)-1] = '\0';
  oled.drawStr(x, y, _buf);
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

bool http_get(char *server, char *path) {
  Serial.print("Fetching http://");
  Serial.print(server);
  Serial.println(path);
  if (!wifi_client.connect(server, HTTP_PORT)) {
    Serial.println("  Connection failed.");
    return false;
  }

  wifi_client.print("GET ");
  wifi_client.print(path);
  wifi_client.println(" HTTP/1.1");
  wifi_client.print("Host: ");
  wifi_client.println(server);
  wifi_client.println("Connection: close");
  wifi_client.println();

  int32_t n = 0;
  Serial.println("=== BEGIN RESPONSE ===");
  while (wifi_client.connected()) {
    while (wifi_client.available()) {
      char c = wifi_client.read();
      Serial.write(c);
      n++;
    }
  }
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

void setup() {
  Serial.begin(9600);
  while (!Serial);
  FLOG("Booting, Version " VERSION "\n");

  pinMode(SHIFTREG_LATCH_PIN, OUTPUT);
  pinMode(SHIFTREG_CLOCK_PIN, OUTPUT);
  pinMode(SHIFTREG_DATA_PIN, OUTPUT);

  oled.begin();
  oled.setFont(u8g_font_6x10);
  // oled.setFontRefHeightExtendedText();
  // oled.setDefaultForegroundColor();
  oled.setFontPosTop();

  status = 0;
  boot_screen(&boot);
  delay(500);

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

    while (wifi_status != WL_CONNECTED) {
      Serial.print("Attempting to connect to SSID using WPA/WPA2: ");
      Serial.println(wifi_ssid);
      wifi_status = WiFi.begin(wifi_ssid, wifi_pass);
      if (wifi_status != WL_CONNECTED) {
        delay(5000);
      }
    }
    printWifiStatus();
    status |= STATUS_WIFI_CONNECTED;
    boot_screen(&boot);

    if (http_get(URL_SERVER, URL_PATH))
      status |= STATUS_DATA_DOWNLOADED;
  }
  boot_screen(&boot);

  delay(1000);

  if (status != STATUS_READY) {
    status |= STATUS_ERROR;
    boot_screen(&boot);
  }

}

void updateShiftRegister(byte leds)
{
   digitalWrite(SHIFTREG_LATCH_PIN, LOW);
   shiftOut(SHIFTREG_DATA_PIN, SHIFTREG_CLOCK_PIN, LSBFIRST, leds);
   digitalWrite(SHIFTREG_LATCH_PIN, HIGH);
}

void loop() {

  byte leds = 0;        // Initially turns all the LEDs off, by giving the variable 'leds' the value 0
  updateShiftRegister(leds);
  delay(500);
  for (int i = 0; i < 8; i++)        // Turn all the LEDs ON one by one.
  {
    leds = (leds << 1) | 1;
    bitSet(leds, i);                // Set the bit that controls that LED in the variable 'leds'
    updateShiftRegister(leds);
    delay(500);
  }

  // do_lights();

  // refresh_display();

  // if (sensors_found) {
  //   float degreesF = sensors.getTempF();
  //   temperature = (int)(10.0f * degreesF + 0.5);
  //   FLOG("Sensor Temperature: "); LOG(degreesF); LOG("F\n");
  // }

  // http_get(URL_SERVER, URL_PATH);

  // delay(5000);

}

