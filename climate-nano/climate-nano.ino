#include <Wire.h>
#include <DS18B20.h>

#define VERSION "0.1"

// Arduino Hardware Configuration
// ------------------------------

// OneWire bus for Dallas Semiconductor DS18B20 Temperature Sensor
#define ONEWIRE_PIN 10 // pin 10 is OneWire data, with external pullup

// RGB LED Drivers
const byte RGB_PWM_PINS[] = {5, 3, 6}; // pins 3, 5, 6 for R, G, B

// Debugging LED
#define DEBUG_LED_PIN 13 // pin 13 is built-in LED

// Calibration setup for temperature sensor
# define CALIBRATION_PERIOD 1000 // wait 1 second between calibration readings
# define CALIBRATION_COUNT 5 // take 5 calibration readings

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

// Global Variables
// ----------------

DS18B20 sensors(ONEWIRE_PIN); // Temperature Sensors
int sensors_found = 0;
// unsigned long scan_timestamp = 0;
unsigned long calibration_timestamp = 0;
unsigned int calibration = 0;
float calibration_sum = 0.0f;
float baseline = 0.0f;
byte cur_rgb[3] = {0, 0, 0};
byte debug_rgb[3] = {0, 0, 0};
bool debug_lights = false;

#if DEBUG
char _buf[128]; // used by serial_printf
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
  if (!*last_update || (time_now - *last_update) >= interval) {
    *last_update = time_now;
    return true;
  } else {
    return false;
  }
}

void sensors_identify(int i) {
  FLOG("Device "); LOG(i); FLOG("\n");
  int ds_model = sensors.getFamilyCode();
  switch (ds_model) {
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

  byte ds_addr[8];
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
    sensors_identify(i);
    FLOG("Temperature: "); LOG(sensors.getTempF()); LOG("F\n");
    calibration = 0;
    calibration_sum = 0.0f;
    baseline = 0.0f;
    calibration_timestamp = 0;
  }
  FLOG("Found "); LOG(sensors_found); FLOG(" sensors.\n");
}

byte clamp8(float f) {
  if (f <= 0.0f) return 0;
  else if (f >= 255.0f) return 255;
  int b = (int)round(f);
  if (b <= 0) return 0;
  if (b >= 255) return 255;
  return (byte)b;
}

byte cold[] = { 0, 0, 255 };
byte mid[] =  { 0, 175, 40 };
byte warm[] = { 0, 100, 0 };
byte hot[] =  { 255, 0, 0 };

void interpolate(float v, byte *lo, byte *hi, byte *rgb) {
  rgb[0] = clamp8(lo[0]*(1.0f-v) + hi[0]*v);
  rgb[1] = clamp8(lo[1]*(1.0f-v) + hi[1]*v);
  rgb[2] = clamp8(lo[2]*(1.0f-v) + hi[2]*v);
}

// picks color depending on deviation:
// deviation -7/2 from average: cold
// deviation -2/2 from average: mid
// deviation 0/2 from average: warm
// deviation +5/2 from average: hot
void gradient(float txx, byte *rgb) {
  txx *= 2.0f;
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

void display(byte *new_rgb) {
  for (int i = 0; i < 3; i++) {
    if (new_rgb[i] == cur_rgb[i])
      continue;
    cur_rgb[i] = new_rgb[i];
    analogWrite(RGB_PWM_PINS[i], cur_rgb[i]);
  }
}

void do_lights() {
  float t = sensors.getTempF();

  if (rate_limit_expired(&calibration_timestamp, CALIBRATION_PERIOD)) {
    if (calibration < CALIBRATION_COUNT) {
      calibration++;
      calibration_sum += t;
      baseline = calibration_sum / calibration;
      FLOG("Calibration reading "); LOG(calibration); FLOG(": "); LOG(t);
      FLOG(" F, baseline is now "); LOG(baseline); FLOG(" F\n");
    } else  {
      FLOG("Current temperature: "); LOG(t); FLOG(" F, ");
      LOG(t-baseline); FLOG(" from baseline of "); LOG(baseline); FLOG(" F\n");
    }
  }

  byte new_rgb[3];
  gradient(t - baseline, new_rgb);

  display(debug_lights ? debug_rgb : new_rgb);
}

void blink(int n) {
  for (int i = 0; i < 2*n; i++) {
    digitalWrite(DEBUG_LED_PIN, i%2 ? LOW : HIGH);
    delay(250);
  }
}

void setup() {
  DBG(Serial.begin(115200));
  FLOG("Booting, Version " VERSION "\n");
  pinMode(DEBUG_LED_PIN, OUTPUT);
  for (int i = 0; i < 3; i++) {
    pinMode(RGB_PWM_PINS[i], OUTPUT);
    analogWrite(RGB_PWM_PINS, 0);
  }
  display(cold);
  blink(2);
  display(mid);
  blink(2);
  display(hot);
  blink(2);
  display(warm);
  FLOG("Ready\n");
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

void parse_serial(char *line, int linelen) {
  if (linelen == 0) {
    debug_lights = false;
    return;
  }
  debug_lights = true;
  if (line[0] == '#' && 
      line[1] && line[2] && line[3] && (line[4] == ' ' || !line[4])) {
    debug_rgb[0] = hex2nibble(line[1]) * 0x11;
    debug_rgb[1] = hex2nibble(line[2]) * 0x11;
    debug_rgb[2] = hex2nibble(line[3]) * 0x11;
  } else if (line[0] == '#') {
    debug_rgb[0] = (hex2nibble(line[1]) << 4) | hex2nibble(line[2]);
    debug_rgb[1] = (hex2nibble(line[3]) << 4) | hex2nibble(line[4]);
    debug_rgb[2] = (hex2nibble(line[5]) << 4) | hex2nibble(line[6]);
  } else {
    float txx = atof(line);
    gradient(txx, debug_rgb);
  }
  LOGF("#%02x%02x%02x\n", debug_rgb[0], debug_rgb[1], debug_rgb[2]);
  display(debug_rgb);
}

char serial_buf[80]; // serial console for debugging
int serial_cnt = 0;

void loop() {
  if (!sensors_found /* && rate_limit_expired(&scan_timestamp, 5000)*/) {
    scan_sensors();
  }

  while (DEBUG && Serial && Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n' || c == '\0') {
      serial_buf[serial_cnt] = '\0';
      parse_serial(serial_buf, serial_cnt);
      serial_cnt = 0;
    } else if (serial_cnt < sizeof(serial_buf)-1) {
      serial_buf[serial_cnt++] = c;
    }
  }

  if (sensors_found) {
    do_lights();
  } else {
    delay(5000);
  }
}
