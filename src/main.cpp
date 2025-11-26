#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <Wire.h>
#include <Preferences.h>
#include <esp_attr.h>
#include <NimBLEDevice.h>
#include "LGFX_Hacktor.hpp"

#define PIN_CHG 21
#define PIN_BTN 0

#ifdef min
#undef min
#endif

#define TFT_BL 7
#define TFT_PWR 18
#define IMU_INT1_PIN 17
#define IMU_INT2_PIN 14

static uint8_t imu_addr = 0x6A;            // LSM6DSLTR (default, may be 0x6B if SDO pulled high)
static constexpr uint8_t IMU_WHOAMI = 0x6A;
static constexpr uint16_t BACKLIGHT_MAX_DUTY = 1023;
static constexpr uint32_t BACKLIGHT_ON_TIME_MS = 10000;
static constexpr uint32_t BACKLIGHT_FADE_MS = 1000;

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
static uint32_t backlight_on_deadline = 0;
static uint32_t backlight_ramp_start = 0;
static uint16_t backlight_level = BACKLIGHT_MAX_DUTY + 1;
static bool backlight_ramping = false;
static volatile bool imu_interrupt = false;
static bool imu_ready = false;
static uint32_t last_debug_ms = 0;
static volatile bool imu_interrupt2 = false;
static uint32_t last_wake_trigger_ms = 0;
static constexpr uint8_t IMU_WAKE_THRESHOLD = 0x0A;     // higher = less sensitive (~62.5 mg/LSB at 2g)
static constexpr uint32_t WAKE_COOLDOWN_MS = 2000;      // ignore new wake events inside this window
static uint16_t step_count = 0;
static bool steps_dirty = true;
static uint32_t last_step_poll_ms = 0;
static constexpr uint32_t STEP_REFRESH_MS = 3000;
static bool pedometer_enabled = false;
static constexpr int STEP_BOX_WIDTH = 120;
static constexpr int STEP_BOX_HEIGHT = 40;
static constexpr int STEP_BOX_X = clock_center - (STEP_BOX_WIDTH / 2);
static constexpr int STEP_BOX_Y = clock_center + 50 - (STEP_BOX_HEIGHT / 2);
static bool info_mode = false;
static bool info_dirty = true;
static uint32_t last_info_render_ms = 0;

// BLE CTS (Current Time Service)
static constexpr uint32_t CTS_SYNC_INTERVAL_MS = 3600000; // hourly
static constexpr uint32_t CTS_RETRY_MS = 300000;         // retry after 5 min on failure
static const NimBLEUUID CTS_SERVICE_UUID = NimBLEUUID((uint16_t)0x1805);
static const NimBLEUUID CTS_CHAR_UUID = NimBLEUUID((uint16_t)0x2A2B);
static bool ble_initialized = false;
static bool cts_sync_in_progress = false;
static uint32_t last_cts_sync_ms = 0;
static uint32_t last_cts_attempt_ms = 0;
static bool button_state = true;
static uint32_t last_button_ms = 0;
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 80;
static Preferences prefs;
static uint32_t reset_counter = 0;

static bool imuWriteRegister(uint8_t reg, uint8_t value);
static bool imuReadRegister(uint8_t reg, uint8_t& value);

static bool syncTimeFromCts(const char* reason);
static int calculateWeekday(int year, int month, int day);
//static void drawStepIconStatic();
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
static uint8_t battery_percent = 100;
static uint32_t last_batt_update_ms = 0;
static bool is_charging = false;

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
  if (duty == backlight_level) {
    return;
  }
  backlight_level = duty;
  ledcWrite(TFT_BL, duty);
}

static void power(uint8_t mode) {
  pinMode(TFT_PWR, OUTPUT);
  digitalWrite(TFT_PWR, mode);
}

static bool imuWriteRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(imu_addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool imuReadRegister(uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(imu_addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(imu_addr, static_cast<uint8_t>(1)) != 1) {
    return false;
  }
  value = Wire.read();
  return true;
}

static void IRAM_ATTR imuInterruptHandler() {
  imu_interrupt = true;
}

static void IRAM_ATTR imuInterruptHandler2() {
  imu_interrupt2 = true;
}

static uint16_t readRegister16(uint8_t reg) {
  Wire.beginTransmission(0x36);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(0x36, (uint8_t)2);
  uint16_t value = 0;
  if (Wire.available() >= 2) {
    value = (Wire.read() << 8) | Wire.read();
  }
  return value;
}

static uint8_t readBatteryPercent() {
  uint16_t raw = readRegister16(0x04); // SOC register
  return static_cast<uint8_t>(std::min<uint16_t>(100, raw >> 8));
}

static bool readStepCounter(uint16_t& steps) {
  uint8_t lo = 0;
  uint8_t hi = 0;
  if (!imuReadRegister(0x4B, lo)) {
    return false;
  }
  if (!imuReadRegister(0x4C, hi)) {
    return false;
  }
  steps = static_cast<uint16_t>((hi << 8) | lo);
  return true;
}

static void dumpPedometerConfig() {
  uint8_t val = 0;
  imuReadRegister(0x19, val); // CTRL10_C
  Serial.printf("PEDO CFG: CTRL10_C=0x%02X\n", val);

  imuWriteRegister(0x01, 0x80); // enter embedded func reg bank
  uint8_t emb_a = 0, emb_b = 0;
  imuReadRegister(0x04, emb_a); // EMB_FUNC_EN_A
  imuReadRegister(0x05, emb_b); // EMB_FUNC_EN_B
  imuWriteRegister(0x01, 0x00); // return to user bank
  Serial.printf("PEDO CFG: EMB_FUNC_EN_A=0x%02X EMB_FUNC_EN_B=0x%02X\n", emb_a, emb_b);

  uint16_t steps = 0;
  if (readStepCounter(steps)) {
    Serial.printf("PEDO CFG: initial step count=%u\n", static_cast<unsigned>(steps));
  } else {
    Serial.println("PEDO CFG: failed to read step counter");
  }
}

static void clearImuInterruptSource() {
  uint8_t _unused = 0;
  imuReadRegister(0x1B, _unused); // WAKE_UP_SRC (also clears latched wake events)
}

static void applyCtsTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
  start_date.year = year;
  start_date.month = month;
  start_date.day = day;
  start_date.wday = calculateWeekday(year, month, day);
  last_rendered_day = -1;

  uint32_t sec_of_day = static_cast<uint32_t>(hour) * 3600UL + static_cast<uint32_t>(minute) * 60UL + static_cast<uint32_t>(second);
  count_ms = static_cast<uint64_t>(sec_of_day) * 1000ULL;
  Serial.printf("CTS time applied: %04u-%02u-%02u %02u:%02u:%02u\n",
                static_cast<unsigned>(year), month, day, hour, minute, second);
}

static const NimBLEAddress CTS_SERVER_ADDR_RANDOM = NimBLEAddress("5e:d7:d0:ce:90:96", BLE_ADDR_RANDOM);

static bool syncTimeFromCts(const char* reason) {
  if (cts_sync_in_progress) {
    return false;
  }
  cts_sync_in_progress = true;
  last_cts_attempt_ms = lgfx::millis();

  if (!ble_initialized) {
    NimBLEDevice::init("HacktorWatch");
    ble_initialized = true;
  }

  bool updated = false;
  auto tryConnect = [&](const NimBLEAddress& addr, const char* label) -> NimBLEClient* {
    Serial.printf("CTS sync (%s): connecting to %s (%s)...\n", reason, addr.toString().c_str(), label);
    NimBLEClient* c = NimBLEDevice::createClient();
    if (!c->connect(addr)) {
      Serial.printf("CTS direct connect failed (%s)\n", label);
      NimBLEDevice::deleteClient(c);
      return nullptr;
    }
    return c;
  };

  NimBLEClient* client = tryConnect(CTS_SERVER_ADDR_RANDOM, "random");
  if (!client) {
    cts_sync_in_progress = false;
    return false;
  }

  NimBLERemoteService* svc = client->getService(CTS_SERVICE_UUID);
  if (!svc) {
    Serial.println("CTS service not found");
    client->disconnect();
    NimBLEDevice::deleteClient(client);
    cts_sync_in_progress = false;
    return false;
  }
  NimBLERemoteCharacteristic* chr = svc->getCharacteristic(CTS_CHAR_UUID);
  if (!chr || !chr->canRead()) {
    Serial.println("CTS characteristic not readable");
    client->disconnect();
    NimBLEDevice::deleteClient(client);
    cts_sync_in_progress = false;
    return false;
  }

  std::string val = chr->readValue();
  if (val.size() < 7) {
    Serial.printf("CTS payload too short: %u bytes\n", static_cast<unsigned>(val.size()));
    client->disconnect();
    NimBLEDevice::deleteClient(client);
    cts_sync_in_progress = false;
    return false;
  }

  const uint8_t* d = reinterpret_cast<const uint8_t*>(val.data());
  uint16_t year = static_cast<uint16_t>(d[0] | (d[1] << 8));
  uint8_t month = d[2];
  uint8_t day = d[3];
  uint8_t hour = d[4];
  uint8_t minute = d[5];
  uint8_t second = d[6];

  applyCtsTime(year, month, day, hour, minute, second);
  last_cts_sync_ms = lgfx::millis();
  updated = true;
  client->disconnect();
  NimBLEDevice::deleteClient(client);

  if (!updated) {
    Serial.println("CTS sync failed");
  } else {
    Serial.println("CTS sync complete");
  }
  cts_sync_in_progress = false;
  return updated;
}

static bool enablePedometer() {
  bool ok = true;
  // Enable embedded function register access (set FUNC_CFG_ACCESS.FUNC_CFG_EN=1)
  ok &= imuWriteRegister(0x01, 0x80);
  // Enable pedometer in embedded function enable register B (PEDO_EN should be bit4)
  ok &= imuWriteRegister(0x04, 0x00); // EMB_FUNC_EN_A: keep other funcs off
  ok &= imuWriteRegister(0x05, 0x10); // EMB_FUNC_EN_B: pedometer enable
  // Return to user bank
  ok &= imuWriteRegister(0x01, 0x00);

  // Reset step counter then enable pedometer + embedded functions in CTRL10_C
  ok &= imuWriteRegister(0x19, 0x6C); // CTRL10_C: FUNC_EN | PEDO_EN | PEDO_RST_STEP | (keep bit5 set)
  delay(5);
  ok &= imuWriteRegister(0x19, 0x3C); // CTRL10_C: FUNC_EN | PEDO_EN | keep bit5 set, no reset

  if (!ok) {
    Serial.println("Pedometer enable failed");
    pedometer_enabled = false;
  } else {
    pedometer_enabled = true;
    Serial.println("Pedometer enabled");
    step_count = 0;
    steps_dirty = true;
  }
  return ok;
}

static void i2cScan() {
  Serial.println("I2C scan start");
  for (uint8_t addr = 1; addr < 0x7F; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" - found device at 0x%02X\n", addr);
    }
    delay(2);
  }
  Serial.println("I2C scan done");
}

static bool initImuWakeGesture() {
  uint8_t whoami = 0;
  Serial.println("IMU init start");

  const uint8_t candidates[] = {0x6A, 0x6B};
  bool found_addr = false;
  for (uint8_t addr : candidates) {
    imu_addr = addr;
    if (imuReadRegister(0x0F, whoami)) {
      Serial.printf("IMU candidate 0x%02X WHOAMI=0x%02X\n", addr, whoami);
      if (whoami == IMU_WHOAMI) {
        found_addr = true;
        break;
      }
    } else {
      Serial.printf("IMU candidate 0x%02X WHOAMI read failed\n", addr);
    }
  }
  if (!found_addr) {
    Serial.println("IMU not detected on 0x6A/0x6B");
    return false;
  }

  bool ok = true;
  ok &= imuWriteRegister(0x12, 0x44); // CTRL3_C: BDU = 1, IF_INC = 1
  ok &= imuWriteRegister(0x10, 0x40); // CTRL1_XL: 104 Hz, 2g, BW = 400 Hz
  ok &= imuWriteRegister(0x18, 0x38); // CTRL9_XL: enable X, Y, Z accelerometer axes

  ok &= imuWriteRegister(0x58, 0x80); // TAP_CFG: LIR (latch interrupts)
  ok &= imuWriteRegister(0x5B, IMU_WAKE_THRESHOLD); // WAKE_UP_THS: threshold (higher = less sensitive)
  ok &= imuWriteRegister(0x5C, 0x00); // WAKE_UP_DUR: no additional duration requirement
  ok &= imuWriteRegister(0x5E, 0x20); // MD1_CFG: route wake-up interrupt to INT1
  ok &= imuWriteRegister(0x5F, 0x20); // MD2_CFG: route wake-up interrupt to INT2

  if (!ok) {
    Serial.println("IMU register write failed");
    return false;
  }

  enablePedometer(); // best-effort; keep wake interrupt even if pedometer setup fails
  if (pedometer_enabled) {
    dumpPedometerConfig();
  }
  clearImuInterruptSource();    // clear any stale wake flags
  if (!pedometer_enabled) {
    Serial.println("Pedometer did not enable; step counter may stay at 0");
  }
  Serial.println("IMU init done; waiting for wake interrupts on INT1");
  return true;
}

static uint32_t compileTimeSeconds() {
  const char* t = __TIME__;  // "hh:mm:ss"
  uint32_t hour = (t[0] - '0') * 10 + (t[1] - '0');
  uint32_t minute = (t[3] - '0') * 10 + (t[4] - '0');
  uint32_t second = (t[6] - '0') * 10 + (t[7] - '0');
  return hour * 3600UL + minute * 60UL + second;
}

static void triggerBacklight(uint32_t now_ms) {
  backlight_on_deadline = now_ms + BACKLIGHT_ON_TIME_MS;
  backlight_ramp_start = 0;
  backlight_ramping = false;
  backlight(BACKLIGHT_MAX_DUTY);
}

static void updateBacklight(uint32_t now_ms) {
  if (backlight_on_deadline == 0) {
    return;
  }

  if (backlight_ramping) {
    uint32_t elapsed = now_ms - backlight_ramp_start;
    if (elapsed >= BACKLIGHT_FADE_MS) {
      backlight(0);
      backlight_on_deadline = 0;
      backlight_ramping = false;
      backlight_ramp_start = 0;
    } else {
      uint32_t duty = (BACKLIGHT_MAX_DUTY * (BACKLIGHT_FADE_MS - elapsed)) / BACKLIGHT_FADE_MS;
      backlight(static_cast<uint16_t>(duty));
    }
    return;
  }

  if (static_cast<int32_t>(backlight_on_deadline - now_ms) <= 0) {
    backlight_ramping = true;
    backlight_ramp_start = now_ms;
  }
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

static void drawBatteryIcon(int x, int y, uint8_t percent, bool charging) {
  const int icon_width = 20;
  const int icon_height = 12;
  clockbase.fillRect(x, y, icon_width, icon_height, 6);
  clockbase.drawRect(x, y, icon_width - 4, icon_height, 12);
  clockbase.fillRect(x + icon_width - 4, y + 3, 3, icon_height - 6, 12);

  int level_width = (icon_width - 6) * percent / 100;
  clockbase.fillRect(x + 2, y + 2, level_width, icon_height - 4, percent > 20 ? 10 : 14);

  if (charging) {
    clockbase.fillTriangle(x + 7, y + 2, x + 11, y + 2, x + 8, y + 7, 15);
    clockbase.fillTriangle(x + 8, y + 5, x + 12, y + 5, x + 9, y + 10, 15);
  }
}

static void drawBatteryGauge(uint8_t percent, bool charging) {
  const int start_x = clock_center + 25;
  const int start_y = clock_center - 5;
  drawBatteryIcon(start_x, start_y, percent, charging);

  auto prev_font = clockbase.getFont();
  auto prev_datum = clockbase.getTextDatum();
  clockbase.setFont(&lgfx::v1::fonts::Font2);
  clockbase.setTextDatum(lgfx::top_left);
  clockbase.setTextColor(12, 6);

  char buf[8];
  snprintf(buf, sizeof(buf), "%3u%%", percent);
  clockbase.drawString(buf, start_x + 20, start_y - 2);

  clockbase.setFont(prev_font);
  clockbase.setTextDatum(prev_datum);
}

static void drawStepCount(uint16_t steps) {
  //auto prev_font = canvas.getFont();
  //auto prev_datum = canvas.getTextDatum();

  // clear numeric area only; icon is drawn once on clockbase
  //const int text_area_x = STEP_BOX_X + 48;
  //const int text_area_w = STEP_BOX_WIDTH - 52;
  //canvas.fillRect(text_area_x, STEP_BOX_Y, text_area_w, STEP_BOX_HEIGHT, 6);

  char buf[12];
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(steps));
  canvas.setFont(&lgfx::v1::fonts::Font2);
  canvas.setTextDatum(lgfx::middle_center);
  canvas.setTextColor(12, 6);
  canvas.drawString(buf, 120, 170);

  //canvas.setFont(prev_font);
  //canvas.setTextDatum(prev_datum);
}

/*static void drawStepIconStatic() {
  const int icon_x = STEP_BOX_X + 4;
  const int icon_y = STEP_BOX_Y + (STEP_BOX_HEIGHT - 40) / 2;
  clockbase.drawBitmap(icon_x, icon_y, steps_bitmap, 40, 40, transpalette, 15);
}*/

static void drawInfoScreen(uint32_t now_ms) {
  gfx.fillScreen(TFT_BLACK);
  gfx.setTextColor(TFT_WHITE, TFT_BLACK);
  gfx.setFont(&lgfx::v1::fonts::Font2);
  gfx.setTextDatum(lgfx::middle_center);

  char line[80];
  const int cx = gfx.width() / 2;
  int y = 50;
  const int dy = 20;

  uint32_t uptime_s = now_ms / 1000;
  uint32_t last_cts_age_s = last_cts_sync_ms ? (now_ms - last_cts_sync_ms) / 1000 : 0;

  snprintf(line, sizeof(line), "Uptime: %lus", static_cast<unsigned long>(uptime_s));
  gfx.drawString(line, cx, y);
  y += dy;

  if (last_cts_sync_ms) {
    snprintf(line, sizeof(line), "Last CTS: %lus ago", static_cast<unsigned long>(last_cts_age_s));
  } else {
    snprintf(line, sizeof(line), "Last CTS: never");
  }
  gfx.drawString(line, cx, y);
  y += dy;

  snprintf(line, sizeof(line), "Steps: %u", static_cast<unsigned>(step_count));
  gfx.drawString(line, cx, y);
  y += dy;

  snprintf(line, sizeof(line), "Resets: %lu", static_cast<unsigned long>(reset_counter));
  gfx.drawString(line, cx, y);
  y += dy;

  snprintf(line, sizeof(line), "Battery: %u%% %s", battery_percent, digitalRead(PIN_CHG) == LOW ? "(chg)" : "(disch)");
  gfx.drawString(line, cx, y);
  y += dy;

  gfx.drawString(backlight_level ? "Backlight: on" : "Backlight: off", cx, y);
  y += dy;
  gfx.drawString(info_mode ? "Screen: info" : "Screen: clock", cx, y);

  last_info_render_ms = now_ms;
  info_dirty = false;
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

  bool charging = digitalRead(PIN_CHG) == LOW;
  bool backlight_on = backlight_level > 0;

  clockbase.pushSprite(0, 0);
  drawBatteryGauge(battery_percent, charging);
  //stepsIcon.pushSprite(clock_center - (stepsIcon.width() >> 1), clock_center + 30, transpalette);
  

  drawStepCount(step_count); // draw below hands so hands stay on top
  steps_dirty = false;

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
  //drawStepIconStatic();
  gfx.startWrite();
}

void setup() {
  power(HIGH);
  Serial.begin(115200);
  unsigned long wait_start = millis();
  while (!Serial && (millis() - wait_start) < 2000) {
    delay(10);
  }
  Serial.println("\nBooting...");
  Wire.begin();
  Wire.setClock(400000);
  i2cScan();
  pinMode(PIN_CHG, INPUT_PULLUP);
  pinMode(IMU_INT1_PIN, INPUT_PULLUP);
  pinMode(IMU_INT2_PIN, INPUT_PULLUP);
  pinMode(PIN_BTN, INPUT_PULLUP);
  prefs.begin("watch", false);
  reset_counter = prefs.getUInt("reset_count", 0);
  ++reset_counter;
  prefs.putUInt("reset_count", reset_counter);
  
  backlight(0);

  imu_ready = initImuWakeGesture();
  if (imu_ready) {
    attachInterrupt(digitalPinToInterrupt(IMU_INT1_PIN), imuInterruptHandler, RISING);
    attachInterrupt(digitalPinToInterrupt(IMU_INT2_PIN), imuInterruptHandler2, RISING);
    Serial.println("INT1/INT2 interrupts attached");
  } else {
    Serial.println("IMU init failed; backlight will only respond to manual triggers");
  }

  gfx.init();
  gfx.setColorDepth(16);
  gfx.setRotation(2);  // flip display for watch orientation
  gfx.fillScreen(TFT_BLACK);

  start_date = compileDateInfo();
  last_rendered_day = -1;
  initTrigTable();
  initSprites();
  battery_percent = readBatteryPercent();
  count_ms = static_cast<uint64_t>(compileTimeSeconds()) * 1000ULL;
  triggerBacklight(lgfx::millis());  // initial wake so the face is visible on boot
  syncTimeFromCts("boot");
  drawClock(count_ms);
}

void loop() {
  static uint32_t previous_tick = 0;
  uint32_t now_ms = lgfx::millis();
  uint32_t milli = now_ms % 1000;
  if (previous_tick > milli) {
    count_ms += 1000 + (milli - previous_tick);
  } else {
    count_ms += (milli - previous_tick);
  }
  previous_tick = milli;

  if (imu_interrupt || imu_interrupt2) {
    imu_interrupt = false;
    imu_interrupt2 = false;
    uint8_t wake_src = 0;
    uint8_t status_reg = 0;
    if (imu_ready) {
      if (!imuReadRegister(0x1B, wake_src)) {
        Serial.println("IMU: failed to read WAKE_UP_SRC");
      }
      if (!imuReadRegister(0x1E, status_reg)) {
        Serial.println("IMU: failed to read STATUS_REG");
      }
      clearImuInterruptSource(); // ensure interrupt is de-latched even if reads failed
    }
    bool within_cooldown = (now_ms - last_wake_trigger_ms) < WAKE_COOLDOWN_MS;
    Serial.printf("IMU interrupt @ %lu ms: WAKE_UP_SRC=0x%02X STATUS=0x%02X cooldown=%d\n",
                  static_cast<unsigned long>(now_ms), wake_src, status_reg, within_cooldown);
    if (!within_cooldown) {
      last_wake_trigger_ms = now_ms;
      triggerBacklight(now_ms);
    }
  }

  bool backlight_on = backlight_level > 0;
  if (imu_ready && backlight_on && (now_ms - last_step_poll_ms >= STEP_REFRESH_MS)) {
    uint16_t steps = 0;
    if (readStepCounter(steps)) {
      step_count = steps;
      steps_dirty = true;
    } else {
      Serial.println("Step counter read failed");
    }
    last_step_poll_ms = now_ms;
  }

  if (now_ms - last_batt_update_ms > 60000 || now_ms < last_batt_update_ms) {
    last_batt_update_ms = now_ms;
    battery_percent = readBatteryPercent();
  }

  // Button toggle (active low on PIN_BTN)
  bool btn = digitalRead(PIN_BTN) == LOW;
  if (btn != button_state && (now_ms - last_button_ms) > BUTTON_DEBOUNCE_MS) {
    button_state = btn;
    last_button_ms = now_ms;
    if (btn) { // pressed
      info_mode = !info_mode;
      info_dirty = true;
      triggerBacklight(now_ms);
    }
  }

  if (now_ms - last_debug_ms > 2000) {
    last_debug_ms = now_ms;
    Serial.printf("DBG alive t=%lu bl=%u ramp=%d deadline=%lu imu_ready=%d pedo=%d irq1=%d irq2=%d steps=%u\n",
                  static_cast<unsigned long>(now_ms),
                  backlight_level,
                  backlight_ramping,
                  static_cast<unsigned long>(backlight_on_deadline),
                  imu_ready,
                  pedometer_enabled,
                  digitalRead(IMU_INT1_PIN),
                  digitalRead(IMU_INT2_PIN),
                  static_cast<unsigned>(step_count));
  }

  if (!cts_sync_in_progress) {
    bool never_synced = (last_cts_sync_ms == 0);
    bool due_hourly = (!never_synced) && (now_ms - last_cts_sync_ms >= CTS_SYNC_INTERVAL_MS);
    bool due_retry = (now_ms - last_cts_attempt_ms >= CTS_RETRY_MS);
    if ((never_synced && due_retry) || (due_hourly && due_retry)) {
      syncTimeFromCts(never_synced ? "retry" : "periodic");
    }
  }

  updateBacklight(now_ms);
  if (info_mode) {
    if (info_dirty || (now_ms - last_info_render_ms) > 1000) {
      drawInfoScreen(now_ms);
    }
  } else {
    drawClock(count_ms);
  }
}
