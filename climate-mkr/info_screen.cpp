#include <arduino.h>

#include "climate.h"

//   +----------------------+
//   | mm/dd/yyyy 12:30 PM  |
//   | Today's temp: 85.2*F |
//   | This week in history |
//   | 1950s: 49.1 - 73.1*F |
//   | 1960s: 51.5 - 72.0*F |
//   | 1970:: 52.1 - 76.4*F |
//   +----------------------+

#define DEGREES "\xb0" // degree symbol in u8g_font_6x10

// "Mon"
void fmt_day(byte day, char *str) {
  switch (day) {
    case 1:  strcpy(str, "Sun"); break;
    case 2:  strcpy(str, "Mon"); break;
    case 3:  strcpy(str, "Tue"); break;
    case 4:  strcpy(str, "Wed"); break;
    case 5:  strcpy(str, "Thu"); break;
    case 6:  strcpy(str, "Fri"); break;
    case 7:  strcpy(str, "Sat"); break;
    default: strcpy(str, "   "); break;
  }
}

// "Mon mm/dd/20yy";
char *fmt_date(char *str) {
  byte day   = rtc_cur_day();
  byte date  = rtc_cur_date();
  byte month = rtc_cur_month();
  byte year  = rtc_cur_year2();
  int i = 0;
  // fmt_day(day, str); i+= 3; str[i++] = ' ';
  str[i++] = month / 10 + 48;
  str[i++] = month % 10 + 48;
  str[i++] = '/';
  str[i++] = date  / 10 + 48;
  str[i++] = date  % 10 + 48;
  str[i++] = '/';
  str[i++] = '2';
  str[i++] = '0';
  str[i++] = year  / 10 + 48;
  str[i++] = year  % 10 + 48;
  str[i++] = '\0';
  return str;
}

// "12:34 AM"
char *fmt_time(char *str) {
  byte hour   = rtc_cur_hour();
  byte minute = rtc_cur_minute();
  byte second = rtc_cur_second();
  // check_for_dst();
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

// "-xx.x*F"
// " -x.x*F"
// "  x.x*F"
// " xx.x*F"
// "xxx.x*F"
// " cold! "
// " hot!! "
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

// "-xx.x - xx.x*F"
char *fmt_temp_range(char *str, int lotemp, int hitemp) {
  fmt_temp(str, lotemp);
  str[5] = ' ';
  str[6] = '-';
  fmt_temp(str+7, hitemp);
  return str;
}

void info_screen(int edit_sel) {
  char *msg = _buf;
  int cap = sizeof(_buf);
  oled.firstPage(); do {
    oled_drawStr(0,  0, fmt_date(msg));
    oled_drawStr(6*11, 0, fmt_time(msg));
    oled_drawStr(0, 10, "Today's temp:");
    oled_drawStr(6*13, 10, fmt_temp(msg, temperature));
    if (edit_sel == 1) // Fri
      oled.drawLine(0, 9, 6*3, 9);
    else if (edit_sel == 2) // month
      oled.drawLine(6*4, 9, 6*6, 9);
    else if (edit_sel == 3) // date
      oled.drawLine(6*7, 9, 6*9, 9);
    else if (edit_sel == 4) // year
      oled.drawLine(6*10, 9, 6*14, 9);
    else if (edit_sel == 5) // hour
      oled.drawLine(6*0, 19, 6*2, 19);
    else if (edit_sel == 6) // minute
      oled.drawLine(6*3, 19, 6*5, 19);
    else if (edit_sel == 7) // am, pm
      oled.drawLine(6*6, 19, 6*8, 19);
    oled_drawStr(0, 20, F("This week in history"));
    oled_drawStr(0, 30, F("1950s:"));
    oled_drawStr(0, 40, F("1960s:"));
    oled_drawStr(0, 50, F("1970:"));
    oled_drawStr(6*6, 30, fmt_temp_range(msg, historical_weekly_stats[2], historical_weekly_stats[3]));
    oled_drawStr(6*6, 40, fmt_temp_range(msg, historical_weekly_stats[4], historical_weekly_stats[5]));
    oled_drawStr(6*6, 50, fmt_temp_range(msg, historical_weekly_stats[6], historical_weekly_stats[7]));
  } while (oled.nextPage());
}

void oled_drawStr(int x, int y, const char *s) {
  oled.drawStr(x, y, s);
}

void oled_drawStr(int x, int y, const __FlashStringHelper *f) {
  strncpy_P(_buf, (PGM_P)f, sizeof(_buf) - 1); 
  _buf[sizeof(_buf)-1] = '\0';
  oled.drawStr(x, y, _buf);
}
