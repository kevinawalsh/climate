#include <arduino.h>

#include "climate.h"

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

// "mm/dd";
char *fmt_sim_date(char *str) {
  int daynum = sim_day;
  if (daynum < 1) daynum = 1;
  else if (daynum > 365) daynum = 365;
  int month = 1;
  int date = daynum;
  for (int m = 1; m < 12; m++) {
    if (daynum <= month_offset[m])
      break;
    month = m+1;
    date = daynum - month_offset[m];
  }
  int i = 0;
  str[i++] = month / 10 + 48;
  str[i++] = month % 10 + 48;
  str[i++] = '/';
  str[i++] = date  / 10 + 48;
  str[i++] = date  % 10 + 48;
  str[i++] = '\0';
  return str;
}

// "12:34 AM"
char *fmt_time(char *str) {
  byte hour   = rtc_cur_hour();
  byte minute = rtc_cur_minute();
  // check_for_dst();
  // if (inDST)
  //   hour = (hour + 1) % 24;
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

// "12:34 AM"
char *fmt_sim_time(char *str) {
  byte hour   = sim_time / 60;
  byte minute = sim_time % 60;
  // check_for_dst();
  // if (inDST)
  //   hour = (hour + 1) % 24;
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
// " ??? *F"
char *fmt_temp10(char *str, int temp) {
  if (temp == INVALID_TEMP) {
    strcpy(str, "??? " DEGREES "F");
    return str;
  }
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

// "-xx*"
// " -x*"
// "  x*"
// " xx*"
// "xxx*"
// "cold"
// "hot!"
// "???*"
char *fmt_circuit_temp10(char *str, int temp) {
  if (temp == INVALID_TEMP) {
    strcpy(str, "???" DEGREES);
    return str;
  }
  if (temp < -999) {
    strcpy(str, "cold");
    return str;
  }
  if (temp > 9999) {
    strcpy(str, "hot!");
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
  sprintf(str, "%d" DEGREES "F", temp/10);
  return original;
}

// "-xx.x - xx.x*F"
char *fmt_temp10_range(char *str, int lotemp, int hitemp) {
  fmt_temp10(str, lotemp);
  str[5] = ' ';
  str[6] = '-';
  fmt_temp10(str+7, hitemp);
  return str;
}

//   +----------------------+
//   | mm/dd/yyyy  12:30 PM  |
//   | Today's temp: 75.2*F |
//   | Baseline max: 73.1*F |
//   | This day '52: 72.0*F |
//   | Open: hh:hh - hh:hh  |
//   | Circuit: 90* fan:255 |
//   +----------------------+
void info_screen(int edit_sel) {
  char *msg = _buf;
  int cap = sizeof(_buf);
  oled.firstPage(); do {
    oled_drawStr(0,  0, fmt_date(msg));
    oled_drawStr(6*12, 0, fmt_time(msg));
    oled_drawStr(0, 10, "Today's temp:");
    oled_drawStr(6*13, 10, fmt_temp10(msg, temperature10));
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
    oled_drawStr(0, 20, "Baseline max:");
    oled_drawStr(6*13, 20, fmt_temp10(msg, baseline10));
    sprintf(msg, "This day '%d:", 50+variation_index); // '50 - '70
    oled_drawStr(0, 30, msg);
    oled_drawStr(6*13, 30, fmt_temp10(msg, old_temperature10));
    if ((millis() / 3000) % 2 == 0) {
      sprintf(msg, "Open %2d:%02d - %d:%02d", opening_time/60, opening_time%60,
          closing_time/60, closing_time % 60);
    } else {
      sprintf(msg, "WiFi: %s", wifi_ssid);
    }
    oled_drawStr(0, 40, msg);
    oled_drawStr(0, 50, "Circuit:");
    oled_drawStr(6*7, 50, fmt_circuit_temp10(msg, circuit_temperature10));
    oled_drawStr(6*13, 50, "fan:");
    sprintf(msg, "%3d", fan_speed);
    oled_drawStr(6*17, 50, msg);
  } while (oled.nextPage());
}

//   +----------------------+
//   | [sim] mm/dd 12:30 PM |
//   | Today's temp: 75.2*F |
//   | Baseline max: 73.1*F |
//   | This day '52: 72.0*F |
//   | Open: hh:hh - hh:hh  |
//   | Circuit: 90* fan:255 |
//   +----------------------+
void sim_screen() {
  char *msg = _buf;
  int cap = sizeof(_buf);
  oled.firstPage(); do {
    oled_drawStr(0, 0, "[sim]");
    oled_drawStr(6*6,  0, fmt_sim_date(msg));
    oled_drawStr(6*12, 0, fmt_sim_time(msg));
    oled_drawStr(0, 10, "Today's temp:");
    oled_drawStr(6*13, 10, fmt_temp10(msg, sim_temperature10));
    oled_drawStr(0, 20, "Baseline max:");
    oled_drawStr(6*13, 20, fmt_temp10(msg, sim_baseline10));
    sprintf(msg, "This day '%d:", 50+variation_index); // '50 - '70
    oled_drawStr(0, 30, msg);
    oled_drawStr(6*13, 30, fmt_temp10(msg, sim_old_temperature10));
    sprintf(msg, "Open %2d:%02d - %d:%02d", opening_time/60, opening_time%60,
        closing_time/60, closing_time % 60);
    oled_drawStr(0, 40, msg);
    oled_drawStr(0, 50, "Circuit:");
    oled_drawStr(6*7, 50, fmt_circuit_temp10(msg, circuit_temperature10));
    oled_drawStr(6*13, 50, "fan:");
    sprintf(msg, "%3d", fan_speed);
    oled_drawStr(6*17, 50, msg);
  } while (oled.nextPage());
}
//   +----------------------+
//   | mm/dd/yyyy  12:30 PM |
//   | [manual override]    |
//   | Tube 0: rrr ggg bbb  |
//   | Tube 1: offset  12.5 |
//   | Tube 2: rrr ggg bbb  |
//   | Circuit: 90* fan:255 |
//   +----------------------+
void manual_screen() {
  char *msg = _buf;
  int cap = sizeof(_buf);
  oled.firstPage(); do {
    oled_drawStr(0,  0, fmt_date(msg));
    oled_drawStr(6*12, 0, fmt_time(msg));
    oled_drawStr(0, 10, "[manual override]");
    for (int t = 0; t < 3; t++) {
      struct colors *chosen = &manual_settings[t];
      if (-30.0f <= chosen->txx && chosen->txx <= +30.0f) {
        int txx = (int)(10.0f * chosen->txx + 0.5f);
        sprintf(msg, "Tube %d: offset %3d.%d", t, txx/10, abs(txx)%10);
      } else {
        sprintf(msg, "Tube %d: %3d %3d %3d", t, chosen->rgb[0], chosen->rgb[1], chosen->rgb[2]);
      }
      oled_drawStr(0, 20+10*t, msg);
    }
    oled_drawStr(0, 50, "Circuit:");
    oled_drawStr(6*7, 50, fmt_circuit_temp10(msg, circuit_temperature10));
    oled_drawStr(6*13, 50, "fan:");
    sprintf(msg, "%3d", fan_speed);
    oled_drawStr(6*17, 50, msg);
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
