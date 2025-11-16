#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "LGFX_Hacktor.hpp"

#ifdef min
#undef min
#endif

#define TFT_BL 7
#define TFT_PWR 18

LGFX_Hacktor gfx;
LGFX_Sprite canvas(&gfx);
LGFX_Sprite clockbase(&canvas);
LGFX_Sprite needle1(&canvas);
LGFX_Sprite needle2(&canvas);
LGFX_Sprite needle3(&canvas);
LGFX_Sprite stepsIcon(&canvas);


static constexpr int32_t clock_size = 240;
static constexpr int32_t clock_center = clock_size / 2;
static constexpr auto transpalette = 0;
static float zoom = 1.0f;
static uint64_t count_ms = 0;
static float sin_table[60];
static float cos_table[60];

struct DateInfo {
  int year;
  int month;  // 1-12
  int day;    // 1-31
  int wday;   // 0=Sunday
};

static DateInfo start_date;
static int32_t last_rendered_day = -1;

static constexpr const char* WEEKDAY_NAMES[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static constexpr const char* MONTH_NAMES[12] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

struct PaletteColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

static constexpr PaletteColor palette_entries[16] = {
  {0, 0, 0},       // 0 transparent
  {0, 0, 0},       // 1 black shadow
  {32, 32, 32},    // 2 dark gray shadow
  {48, 48, 48},    // 3 medium gray
  {64, 64, 64},    // 4 tick mark halo
  {0, 0, 0},       // 5 used for clearing digits
  {0, 0, 0},       // 6 dial background
  {0, 0, 0},       // 7 unused
  {255, 0, 0},     // 8 strong red (seconds)
  {200, 0, 0},     // 9 darker red fill
  {0, 140, 255},   // 10 accent blue (steps icon)
  {255, 255, 255}, // 11 unused white
  {255, 255, 255}, // 12 white (dial text/marks)
  {255, 255, 255}, // 13 unused white
  {255, 0, 0},     // 14 red indicator dots
  {255, 255, 255}, // 15 pure white (hands, text)
};

static constexpr uint8_t steps_bitmap[] PROGMEM = {
  0xFF,0xFF,0xFF,0xFF,0xFF, // ########################################
  0xFF,0xFF,0xFF,0xFF,0xFF, // ########################################
  0xFF,0xF0,0x7F,0xFF,0xFF, // ############.....#######################
  0xFF,0xE0,0x3F,0xFF,0xFF, // ###########.......######################
  0xFF,0xC0,0x3F,0xFF,0xFF, // ##########........######################
  0xFF,0x80,0x1F,0xFF,0xFF, // #########..........#####################
  0xFF,0x80,0x1F,0xFF,0xFF, // #########..........#####################
  0xFF,0x00,0x1F,0xFF,0xFF, // ########...........#####################
  0xFF,0x00,0x1F,0xFF,0xFF, // ########...........#####################
  0xFF,0x00,0x1F,0xFF,0xFF, // ########...........#####################
  0xFF,0x00,0x1E,0x07,0xFF, // ########...........####......###########
  0xFF,0x00,0x1C,0x03,0xFF, // ########...........###........##########
  0xFF,0x00,0x1C,0x01,0xFF, // ########...........###.........#########
  0xFF,0x00,0x18,0x01,0xFF, // ########...........##..........#########
  0xFF,0x00,0x38,0x00,0xFF, // ########..........###...........########
  0xFF,0x00,0x38,0x00,0xFF, // ########..........###...........########
  0xFF,0x80,0x38,0x00,0xFF, // #########.........###...........########
  0xFF,0x80,0x78,0x00,0xFF, // #########........####...........########
  0xFF,0x80,0x78,0x00,0xFF, // #########........####...........########
  0xFF,0xC0,0x78,0x00,0xFF, // ##########.......####...........########
  0xFF,0xC0,0x78,0x00,0xFF, // ##########.......####...........########
  0xFF,0xFF,0xFC,0x00,0xFF, // ######################..........########
  0xFF,0xC0,0x7C,0x00,0xFF, // ##########.......#####..........########
  0xFF,0xC0,0x7C,0x00,0xFF, // ##########.......#####..........########
  0xFF,0xC0,0x7E,0x00,0xFF, // ##########.......######.........########
  0xFF,0xC0,0x7E,0x01,0xFF, // ##########.......######........#########
  0xFF,0xC0,0x7E,0x01,0xFF, // ##########.......######........#########
  0xFF,0xC0,0x7F,0x01,0xFF, // ##########.......#######.......#########
  0xFF,0xE0,0xFF,0x03,0xFF, // ###########.....########......##########
  0xFF,0xF1,0xFF,0xFF,0xFF, // ############...#########################
  0xFF,0xFF,0xFF,0x03,0xFF, // ########################......##########
  0xFF,0xFF,0xFE,0x03,0xFF, // #######################.......##########
  0xFF,0xFF,0xFE,0x01,0xFF, // #######################........#########
  0xFF,0xFF,0xFE,0x01,0xFF, // #######################........#########
  0xFF,0xFF,0xFE,0x03,0xFF, // #######################.......##########
  0xFF,0xFF,0xFE,0x03,0xFF, // #######################.......##########
  0xFF,0xFF,0xFF,0x07,0xFF, // ########################.....###########
  0xFF,0xFF,0xFF,0xFF,0xFF, // ########################################
  0xFF,0xFF,0xFF,0xFF,0xFF, // ########################################
  0xFF,0xFF,0xFF,0xFF,0xFF  // ########################################
};

static void applyPalette(LGFX_Sprite& sprite) {
  for (int i = 0; i < 16; ++i) {
    auto color = palette_entries[i];
    sprite.setPaletteColor(i, color.r, color.g, color.b);
  }
}

static void initTrigTable() {
  constexpr float deg_to_rad = 0.0174532925f;
  for (int i = 0; i < 60; ++i) {
    float rad = i * 6.0f * deg_to_rad;
    sin_table[i] = std::sin(rad);
    cos_table[i] = -std::cos(rad);
  }
}

static void backlight(uint16_t duty) {
  static bool ledc_initialized = false;
  if (!ledc_initialized) {
    ledcAttach(TFT_BL, 1000, 10);  // 1 kHz PWM, 10-bit resolution
    ledc_initialized = true;
  }
  ledcWrite(TFT_BL, duty);
}

static void power(uint8_t mode) {
  pinMode(TFT_PWR, OUTPUT);
  digitalWrite(TFT_PWR, mode);
}

static uint32_t compileTimeSeconds() {
  const char* t = __TIME__;  // "hh:mm:ss"
  uint32_t hour = (t[0] - '0') * 10 + (t[1] - '0');
  uint32_t minute = (t[3] - '0') * 10 + (t[4] - '0');
  uint32_t second = (t[6] - '0') * 10 + (t[7] - '0');
  return hour * 3600UL + minute * 60UL + second;
}

static bool isLeapYear(int year) {
  return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
}

static int daysInMonth(int year, int month) {
  static constexpr uint8_t DAYS[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  return DAYS[month - 1];
}

static int monthFromAbbrev(const char* date_str) {
  switch (date_str[0]) {
    case 'J': return (date_str[1] == 'a') ? 1 : (date_str[2] == 'n' ? 6 : 7); // Jan, Jun, Jul
    case 'F': return 2;
    case 'M': return (date_str[2] == 'r') ? 3 : 5; // Mar, May
    case 'A': return (date_str[1] == 'p') ? 4 : 8; // Apr, Aug
    case 'S': return 9;
    case 'O': return 10;
    case 'N': return 11;
    case 'D': return 12;
    default:  return 1;
  }
}

static int calculateWeekday(int year, int month, int day) {
  if (month < 3) {
    month += 12;
    --year;
  }
  int K = year % 100;
  int J = year / 100;
  int h = (day + 13 * (month + 1) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;
  return (h + 6) % 7; // convert to 0=Sunday
}

static DateInfo advanceDays(DateInfo date, int32_t days) {
  while (days-- > 0) {
    date.day++;
    date.wday = (date.wday + 1) % 7;
    if (date.day > daysInMonth(date.year, date.month)) {
      date.day = 1;
      ++date.month;
      if (date.month > 12) {
        date.month = 1;
        ++date.year;
      }
    }
  }
  return date;
}

static DateInfo compileDateInfo() {
  DateInfo info{};
  const char* d = __DATE__; // "Mmm dd yyyy"
  info.month = monthFromAbbrev(d);
  info.day = (d[4] == ' ' ? 0 : (d[4] - '0') * 10) + (d[5] - '0');
  info.year = (d[7] - '0') * 1000 + (d[8] - '0') * 100 + (d[9] - '0') * 10 + (d[10] - '0');
  info.wday = calculateWeekday(info.year, info.month, info.day);
  return info;
}

static void updateDateDisplay(const DateInfo& date) {
  const auto* prev_font = clockbase.getFont();
  auto prev_datum = clockbase.getTextDatum();

  const int center_x = clock_center;
  const int center_y = clock_center - 50;
  const int box_width = 150;
  const int box_height = 28;
  const int box_x = center_x - box_width / 2;
  const int box_y = center_y - box_height / 2;

  clockbase.fillRect(box_x, box_y, box_width, box_height, 6);

  clockbase.setFont(&lgfx::v1::fonts::DejaVu12);
  clockbase.setTextDatum(lgfx::middle_center);
  clockbase.setTextColor(15, 6);
  char buffer[12];
  snprintf(buffer, sizeof(buffer), "%s %02d %s", WEEKDAY_NAMES[date.wday], date.day, MONTH_NAMES[date.month - 1]);
  clockbase.drawString(buffer, center_x, center_y + 4);

  clockbase.setFont(prev_font);
  clockbase.setTextDatum(prev_datum);
 
}

static void update7Seg(int32_t hour, int32_t minute) {
  int x = clockbase.getPivotX() - 69;
  int y = clockbase.getPivotY();
  clockbase.setCursor(x, y);
  clockbase.setTextColor(5);   // erase with shadow color
  clockbase.print("88:88");
  clockbase.setCursor(x, y);
  clockbase.setTextColor(12);  // draw digits
  clockbase.printf("%02d:%02d", hour, minute);
}

static void drawDot(int pos, int palette) {
  bool major_tick = 0 == (pos % 5);
  float cosy = cos_table[pos] * (clock_center * 10 / 10.4f);
  float sinx = sin_table[pos] * (clock_center * 10 / 10.4f);
  canvas.fillCircle(clock_center + sinx, clock_center + cosy, major_tick * 3 + 1, palette);
}

static void drawClock(uint64_t time_ms) {
  static int32_t previous_minute = -1;
  int32_t sec = time_ms / 1000;
  int32_t minute = sec / 60;
  int32_t day_index = sec / 86400;

  if (last_rendered_day != day_index) {
    last_rendered_day = day_index;
    DateInfo current_date = advanceDays(start_date, day_index);
    updateDateDisplay(current_date);
  }

  if (previous_minute != minute) {
    previous_minute = minute;
   //update7Seg((minute / 60) % 24, minute % 60);
  }

  clockbase.pushSprite(0, 0);
  stepsIcon.pushSprite(clock_center - (stepsIcon.width() >> 1), clock_center + 30, transpalette);

  drawDot(minute % 60, 14);
  drawDot(((minute / 60) * 5) % 60, 14);
  drawDot(sec % 60, 9);

  float fhour = static_cast<float>(time_ms) / 120000.0f;
  float fmin  = static_cast<float>(time_ms) / 10000.0f;
  float fsec  = static_cast<float>(time_ms) * 6.0f / 1000.0f;
  int px = canvas.getPivotX();
  int py = canvas.getPivotY();
  
  needle1.pushRotateZoom(fhour, 1.0f, 1.0f, transpalette);
  needle3.pushRotateZoom(fmin , 1.0f, 1.0f, transpalette);
  needle2.pushRotateZoom(fsec , 0.7f, 1.0f, transpalette);

  // highlight the second hand's pivot with a solid red cap
  canvas.fillCircle(clock_center, clock_center, 6, 9);
  canvas.drawCircle(clock_center, clock_center, 2, 1);

  canvas.pushRotateZoom(0.0f, zoom, zoom, transpalette);
  gfx.display();
}

static void initSprites() {
  zoom = static_cast<float>(std::min(gfx.width(), gfx.height())) / clock_size;
  gfx.setPivot(gfx.width() >> 1, gfx.height() >> 1);

  canvas.setColorDepth(lgfx::palette_4bit);
  clockbase.setColorDepth(lgfx::palette_4bit);
  needle1.setColorDepth(lgfx::palette_4bit);
  needle2.setColorDepth(lgfx::palette_4bit);
  needle3.setColorDepth(lgfx::palette_4bit);
  stepsIcon.setColorDepth(lgfx::palette_4bit);

  canvas.createSprite(clock_size, clock_size);
  clockbase.createSprite(clock_size, clock_size);
  needle1.createSprite(9, 119);
  needle3.createSprite(9, 119);
  needle2.createSprite(3, 119);
  stepsIcon.createSprite(40, 40);
 
  applyPalette(canvas);
  applyPalette(clockbase);
  applyPalette(needle1);
  applyPalette(needle2);
  applyPalette(needle3);
  applyPalette(stepsIcon);


  canvas.setPivot(clock_center, clock_center);
  clockbase.setPivot(clock_center, clock_center);
  stepsIcon.setPivot(stepsIcon.width() >> 1, stepsIcon.height() >> 1);
  needle1.setPivot(3, 100);
  needle2.setPivot(1, 100);
  needle3.setPivot(3, 100);
  
  
  canvas.fillScreen(transpalette);
  clockbase.fillScreen(transpalette);
  needle1.fillScreen(transpalette);
  needle3.fillScreen(transpalette);
  stepsIcon.fillScreen(transpalette);
  stepsIcon.drawBitmap(0, 0, steps_bitmap, 40, 40, transpalette, 15);
 

  clockbase.setTextFont(4);
  clockbase.setTextDatum(lgfx::middle_center);
  clockbase.fillCircle(clock_center, clock_center, clock_center    ,  6);
  

  for (int i = 1; i <= 60; ++i) {
    float cosy = cos_table[i % 60] * (clock_center * 10 / 10.4f);
    float sinx = sin_table[i % 60] * (clock_center * 10 / 10.4f);
    bool major_tick = 0 == (i % 5);
    
    clockbase.fillCircle(clock_center + sinx, clock_center + cosy, major_tick * 3 + 1, 12);
    if (major_tick && !(i % 15)) {
      cosy = cos_table[i % 60] * (clock_center * 10 / 13.0f);
      sinx = sin_table[i % 60] * (clock_center * 10 / 13.0f);
      
      clockbase.setTextColor(15);
      clockbase.drawNumber(i / 5, clock_center + sinx, clock_center + cosy + 3);
    }
  }
 
  
  needle1.fillRect(1, 35, 5, 60, 15); //hour hand sprite
  needle1.fillCircle(3, 35, 2, 15);

  needle3.fillRect(1, 5, 5, 90, 8); //minutes hand sprite
  needle3.fillCircle(3, 4, 2, 8);

  needle2.fillScreen(9); //seconds hand sprite
  needle2.fillRect(0, 99, 1, 2, 9);
  
  gfx.startWrite();
}

void setup() {
  Serial.begin(115200);
  backlight(1023);
  power(HIGH);

  gfx.init();
  gfx.setColorDepth(16);
  gfx.setRotation(2);  // flip display for watch orientation
  gfx.fillScreen(TFT_BLACK);

  start_date = compileDateInfo();
  last_rendered_day = -1;
  initTrigTable();
  initSprites();
  count_ms = static_cast<uint64_t>(compileTimeSeconds()) * 1000ULL;
  drawClock(count_ms);
}

void loop() {
  static uint32_t previous_tick = 0;
  uint32_t milli = lgfx::millis() % 1000;
  if (previous_tick > milli) {
    count_ms += 1000 + (milli - previous_tick);
  } else {
    count_ms += (milli - previous_tick);
  }
  previous_tick = milli;

  drawClock(count_ms);
}
