#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <Wire.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <NimBLEDevice.h>
#include "LGFX_Hacktor.hpp"
#include "DigitalWatchface.hpp"
#include "TimerWatchface.hpp"
#include "WatchfaceTouch.hpp"
#include "WatchfaceSlide.hpp"
#include "WatchButton.hpp"
#include "WatchTimer.hpp"
#include "WatchTiming.hpp"
#include "WatchTouch.hpp"
#include "WatchHaptics.hpp"
#include "HapticAlert.hpp"
#include "HapticRequests.hpp"
#include "WatchLog.hpp"
#include "StepTracking.hpp"
#include "StepCheckpoint.hpp"
#include "StepCounterRead.hpp"
#include "Lsm6dslPedometer.hpp"
#include "Lsm6dslStartup.hpp"
#include "ImuReset.hpp"
#include "WatchSettings.hpp"
#include "WatchSettingsStore.hpp"
#include "SettingsMenu.hpp"
#include "SettingsWatchface.hpp"
#include "SettingsSlide.hpp"
#include "PowerStats.hpp"
#include "WatchActivityBle.hpp"
#include "CtsBleCleanup.hpp"
#include "BleAdvertisingSchedule.hpp"

#define PIN_CHG 21
#define PIN_BTN 0

#ifdef min
#undef min
#endif

#define TFT_BL 7
#define PERIPHERAL_PWR 18
#define TP_RST 2
#define I2C_SDA 8
#define I2C_SCL 9
#define IMU_INT1_PIN 17
#define IMU_INT2_PIN 14

#ifndef HACKTOR_BACKLIGHT_RAIL_WORKAROUND
#define HACKTOR_BACKLIGHT_RAIL_WORKAROUND 1
#endif
#ifndef HACKTOR_HAND_ANTIALIASING
#define HACKTOR_HAND_ANTIALIASING 1
#endif

static uint8_t imu_addr = 0x6A;            // LSM6DSLTR (default, may be 0x6B if SDO pulled high)
static constexpr uint8_t IMU_WHOAMI = 0x6A;
static constexpr uint16_t BACKLIGHT_MAX_DUTY = 1023;
static constexpr uint32_t BACKLIGHT_FADE_MS = 1000;
static constexpr uint32_t BATTERY_REFRESH_MS = 60000;
static constexpr uint32_t PANEL_SLEEP_DELAY_MS = 120;
static constexpr uint32_t PANEL_WAKE_DELAY_MS = 120;
static constexpr uint32_t IDLE_RETRY_MS = 20;
static constexpr uint32_t PERIPHERAL_POWER_SETTLE_MS = 5;
static constexpr uint32_t PERIPHERAL_POWER_OFF_MS = 10;
static constexpr uint32_t MAX_STANDBY_SLEEP_MS = 60000;
static constexpr uint32_t TOUCH_POLL_MS = 20;

LGFX_Hacktor gfx;
LGFX_Sprite canvas(&gfx);
LGFX_Sprite clockbase(&canvas);
LGFX_Sprite needle1(&canvas);
LGFX_Sprite needle2(&canvas);
LGFX_Sprite needle3(&canvas);


static constexpr int32_t clock_size = 240;
static constexpr int32_t clock_center = clock_size / 2;
static constexpr auto transpalette = 0;
static float zoom = 1.0f;
static watch::Clock watch_clock;
static watch::WatchSettings watch_settings;
static watch::WatchSettings saved_settings;
static watch::SettingsMenu settings_menu;
static watch::SettingsSlide settings_slide;
static watch::PowerStats power_stats;
static watch::WakeReason pending_wake_reason = watch::WakeReason::Boot;
static bool settings_dirty = true;
static bool manual_sync_requested = false;
static bool manual_sync_presented = false;
static char settings_status[48] = "";
static uint32_t backlight_on_deadline = 0;
static uint32_t backlight_ramp_start = 0;
static uint16_t backlight_level = BACKLIGHT_MAX_DUTY + 1;
static bool backlight_ramping = false;
static bool backlight_triggered = false;
static bool display_sleeping = false;
static bool display_needs_init = false;
static bool peripherals_powered = true;
static bool i2c_ready = false;
static bool refresh_after_wake = false;
static bool graphics_ready = false;
static uint32_t display_sleep_start_ms = 0;
static bool light_sleep_available = true;
static volatile bool imu_interrupt = false;
static bool imu_ready = false;
#if HACKTOR_LOG_LEVEL >= WATCH_LOG_LEVEL_VERBOSE
static uint32_t last_debug_ms = 0;
#endif
static volatile bool imu_interrupt2 = false;
static uint32_t last_wake_trigger_ms = 0;
static constexpr uint8_t IMU_WAKE_THRESHOLD = 0x0A;     // higher = less sensitive (~62.5 mg/LSB at 2g)
static constexpr uint32_t WAKE_COOLDOWN_MS = 2000;      // ignore new wake events inside this window
static uint32_t step_count = 0;
static uint16_t raw_step_count = 0;
static bool step_read_valid = false;
static watch::StepTracking daily_steps;
static watch::StepCheckpoint step_checkpoint;
static bool step_clock_ready = false;
static bool step_baseline_attempted = false;
static uint32_t last_step_baseline_attempt_ms = 0;
static constexpr uint32_t STEP_BASELINE_RETRY_MS = 60000;
static bool step_anchor_trusted = false;
static bool step_anchor_attempted = false;
static uint32_t last_step_anchor_attempt_ms = 0;
static bool step_persistence_ready = false;
// BLE's synchronous connection retries can take over a minute. Leave a wide
// window before midnight; scheduled sync resumes after the boundary snapshot.
static constexpr uint32_t STEP_CTS_GUARD_MS = 300000;
static bool steps_dirty = true;
static uint32_t last_step_poll_ms = 0;
static constexpr uint32_t STEP_REFRESH_MS = 3000;
static bool pedometer_enabled = false;
static constexpr int STEP_ROW_Y = clock_center + 50;
static constexpr int STEP_ICON_WIDTH = 17;
static constexpr int STEP_ICON_HEIGHT = 10; // Matches the visible Font2 digits.
static constexpr int STEP_ICON_GAP = 4;
static constexpr uint8_t SECONDARY_COLOR = 13;
enum class Watchface { Analog, Digital, Timer };
static Watchface active_watchface = Watchface::Analog;
static Watchface outgoing_watchface = Watchface::Analog;
static Watchface incoming_watchface = Watchface::Analog;
static watch::WatchfaceSlide face_slide;
static bool digital_dirty = true;
static uint64_t last_digital_minute = UINT64_MAX;
static watch::WatchTimer countdown;
static watch::WatchTouch touch;
static watch::WatchfaceTouch face_touch;
static bool touch_ready = false;
static uint32_t last_touch_poll_ms = 0;
static bool timer_dirty = true;
static bool timer_edit_seconds = false;
static uint32_t last_timer_seconds = UINT32_MAX;
static watch::WatchHaptics haptics;
static watch::HapticAlert haptic_alert;
static watch::HapticRequests haptic_requests;
static watch::HapticPlayback active_haptic = watch::HapticPlayback::None;
static bool imu_haptic_clear_pending = false;
// Personal walking-distance estimate: male, 1.80 m tall. One step is one footfall.
static constexpr float ESTIMATED_STEP_METERS = watch::kEstimatedStepMillimetres / 1000.0f;
static bool info_mode = false;
static bool info_dirty = true;
static uint32_t last_info_render_ms = 0;

// BLE CTS (Current Time Service)
static constexpr uint32_t CTS_SYNC_INTERVAL_MS = 3600000; // hourly
static constexpr uint32_t CTS_RETRY_MS = 300000;         // retry after 5 min on failure
static constexpr uint32_t CTS_SCAN_DURATION_MS = 5000;
static const NimBLEUUID CTS_SERVICE_UUID = NimBLEUUID((uint16_t)0x1805);
static const NimBLEUUID CTS_CHAR_UUID = NimBLEUUID((uint16_t)0x2A2B);
static bool ble_initialized = false;
static watch::WatchActivityBle activity_ble;
static watch::BleAdvertisingSchedule advertising_schedule;
static bool cts_sync_in_progress = false;
static bool cts_has_synced = false;
static uint32_t last_cts_sync_ms = 0;
static uint32_t last_cts_attempt_ms = 0;
static watch::WatchButton button;
static Preferences prefs;
static uint32_t reset_counter = 0;

static bool imuWriteRegister(uint8_t reg, uint8_t value);
static bool imuReadRegister(uint8_t reg, uint8_t& value);

static bool syncTimeFromCts(const char* reason, bool quick_attempt = false);
static bool parkPeripheralRail();
static int calculateWeekday(int year, int month, int day);
static int daysInMonth(int year, int month);
static watch::SettingsData currentSettingsData();
static float sin_table[60];
static float cos_table[60];

struct DateInfo {
  int year;
  int month;  // 1-12
  int day;    // 1-31
  int wday;   // 0=Sunday
};

static DateInfo start_date;
static int32_t calendar_base_day = 0;
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
  {160, 235, 170}, // 7 mint digital-watchface accent
  {255, 0, 0},     // 8 strong red (seconds)
  {200, 0, 0},     // 9 darker red fill
  {0, 140, 255},   // 10 accent blue
  {255, 194, 128}, // 11 light-orange timer accent
  {255, 255, 255}, // 12 white (dial text/marks)
  {192, 192, 192}, // 13 secondary date, battery and step information
  {255, 0, 0},     // 14 red indicator dots
  {255, 255, 255}, // 15 pure white (hands, text)
};

// Native-size sneaker outline: one-pixel strokes, collar, laces and sole.
static constexpr uint8_t step_shoe_bitmap[] PROGMEM = {
  0x20,0x00,0x00, // ..#..............
  0x50,0x00,0x00, // .#.#.............
  0x90,0x80,0x00, // #..#....#........
  0x8F,0x40,0x00, // #...####.#.......
  0x80,0xA0,0x00, // #.......#.#......
  0x80,0x5E,0x00, // #........#.####..
  0x80,0x01,0x00, // #..............#.
  0xBF,0xFF,0x80, // #.###############
  0x80,0x00,0x80, // #...............#
  0x7F,0xFF,0x00, // .###############.
};

// The cached dial and hand sprites use palette indices. The compositing canvas
// uses real RGB colors for antialiasing, or indices in the low-memory fallback.
static uint32_t canvasColor(uint8_t index) {
  if (canvas.hasPalette()) {
    return index;
  }
  const auto& color = palette_entries[index];
  return lgfx::color888(color.r, color.g, color.b);
}

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

static uint16_t backlightTarget() {
  return watch::brightnessDuty(watch_settings, BACKLIGHT_MAX_DUTY);
}

static uint32_t backlightOnTimeMs() {
  return watch::timeoutMillis(watch_settings);
}

static void loadWatchSettings() {
  watch_settings = watch::loadSettings(prefs);
  saved_settings = watch_settings;
}

static bool saveWatchSettings() {
  const bool ok = watch::saveSettings(prefs, watch_settings, saved_settings);
  if (!ok) {
    snprintf(settings_status, sizeof(settings_status), "Setting could not be saved");
    settings_dirty = true;
    WATCH_LOG_W("Settings save failed");
  }
  return ok;
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
  power_stats.screen(duty > 0 && !display_sleeping, esp_timer_get_time(), pending_wake_reason);
  if (duty > 0) pending_wake_reason = watch::WakeReason::Other;
  backlight_level = duty;
  ledcWrite(TFT_BL, duty);
}

static void power(uint8_t mode) {
  pinMode(PERIPHERAL_PWR, OUTPUT);
  digitalWrite(PERIPHERAL_PWR, mode);
  gpio_sleep_sel_dis(static_cast<gpio_num_t>(PERIPHERAL_PWR));
  peripherals_powered = mode != LOW;
}

static bool startI2c() {
  // Releasing these pins is safe only after 3V3_2 has been restored: the bus
  // has external pullups to main 3V3 and also reaches switched peripherals.
  pinMode(I2C_SDA, INPUT);
  pinMode(I2C_SCL, INPUT);
  i2c_ready = Wire.begin(I2C_SDA, I2C_SCL, 400000);
  if (!i2c_ready) {
    WATCH_LOG_E("I2C initialization failed");
  }
  return i2c_ready;
}

static void driveLow(uint8_t pin) {
  // Arduino's digitalWrite ignores a pin released by Wire/LovyanGFX until
  // pinMode claims it. Preload the latch through the IDF API to avoid a HIGH
  // glitch when enabling its output driver.
  gpio_set_level(static_cast<gpio_num_t>(pin), 0);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  gpio_sleep_sel_dis(static_cast<gpio_num_t>(pin));
}

static bool imuWriteRegister(uint8_t reg, uint8_t value) {
  if (!i2c_ready) return false;
  Wire.beginTransmission(imu_addr);
  const bool written = Wire.write(reg) == 1 && Wire.write(value) == 1;
  const bool sent = Wire.endTransmission() == 0;
  return written && sent;
}

static bool imuReadRegister(uint8_t reg, uint8_t& value) {
  if (!i2c_ready) return false;
  Wire.beginTransmission(imu_addr);
  const bool written = Wire.write(reg) == 1;
  const bool sent = Wire.endTransmission(!written) == 0;
  bool ok = written && sent && Wire.requestFrom(imu_addr, size_t(1), true) == 1;
  if (ok) {
    const int byte = Wire.read();
    ok = byte >= 0;
    if (ok) value = static_cast<uint8_t>(byte);
  }
  while (Wire.available()) Wire.read();
  return ok;
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
  const bool ok = i2c_ready && watch::readStepCounter(Wire, imu_addr, steps);
  if (ok != step_read_valid || (ok && raw_step_count != steps)) info_dirty = true;
  step_read_valid = ok;
  if (ok) raw_step_count = steps;
  return ok;
}

// BDU affects unused accelerometer/gyro output latching, not step counting.
// Keep it clear until the current daily/raw anchor is durable. A reset during
// an uncommitted cold setup must never pair a new sensor epoch with old flash.
// This marker relies on the current no-FIFO configuration (FIFO requires BDU).
static bool setStepAnchorTrusted(bool trusted) {
  const uint8_t control = trusted ? 0x44 : 0x04;
  uint8_t actual = 0;
  const bool ok = i2c_ready && imuWriteRegister(0x12, control) &&
      imuReadRegister(0x12, actual) && actual == control;
  step_anchor_trusted = ok && trusted;
  return ok;
}

#if HACKTOR_LOG_LEVEL >= WATCH_LOG_LEVEL_DEBUG
static void dumpPedometerConfig() {
  // Stay in the current bank: a failed bank restore must be visible in the
  // trace rather than hidden by the diagnostics themselves.
  const uint8_t registers[] = {0x01, 0x0f, 0x10, 0x12, 0x18, 0x19, 0x58, 0x5e, 0x5f};
  for (uint8_t reg : registers) {
    uint8_t value = 0;
    const bool ok = imuReadRegister(reg, value);
    WATCH_LOG_D("IMU CFG: reg=%02X value=%02X ok=%d", reg, value, ok);
  }

  uint16_t steps = 0;
  if (readStepCounter(steps)) {
    WATCH_LOG_D("PEDO CFG: initial step count=%u", static_cast<unsigned>(steps));
  } else {
    WATCH_LOG_D("PEDO CFG: failed to read step counter");
  }
}

#endif

static bool clearImuInterruptSource() {
  uint8_t _unused = 0;
  return imuReadRegister(0x1B, _unused); // Also clears latched wake events.
}

static void publishDailySteps() {
  const uint32_t count = daily_steps.count();
  if (count == step_count) return;
  step_count = count;
  activity_ble.updateValues(count);
  steps_dirty = true;
  info_dirty = true;
  digital_dirty = true;
}

static int32_t currentStepDay(uint64_t now_us) {
  const int32_t day = calendar_base_day +
      static_cast<int32_t>(watch_clock.at(now_us) / 86400000ULL);
  // The date editor supports through year 9999. Keep the last record if that
  // horizon is exceeded, rather than repeatedly scheduling a rejected date.
  return day > watch::StepHistory::kLastCivilDay ? watch::StepHistory::kLastCivilDay : day;
}

static void prepareStepDate(int32_t day) {
  if (daily_steps.hasDay() && daily_steps.dayKey() == day) return;
  step_anchor_attempted = false;
  if (step_anchor_trusted && !setStepAnchorTrusted(false))
    WATCH_LOG_W("Step anchor could not be marked pending");
}

static uint32_t nextDailyStepWaitMs(uint32_t now_ms, uint64_t now_us) {
  if (!step_clock_ready) return MAX_STANDBY_SLEEP_MS;
  // Panel shutdown can itself cross midnight after the last service call.
  // Let the service close the old day with a raw snapshot before changing it.
  if (!daily_steps.hasDay() || daily_steps.dayKey() != currentStepDay(now_us)) return 0;
  uint32_t wait = std::min(step_checkpoint.waitMs(now_ms),
      watch::millisUntilMidnight(watch_clock.at(now_us)));
  if (step_persistence_ready && daily_steps.needsAnchorSave()) {
    const uint32_t anchor_wait = step_anchor_attempted
        ? watch::remainingMs(now_ms, last_step_anchor_attempt_ms, STEP_BASELINE_RETRY_MS)
        : 0;
    wait = std::min(wait, anchor_wait);
  }
  if (!pedometer_enabled || !daily_steps.needsBaseline()) return wait;
  const uint32_t retry_wait = step_baseline_attempted
      ? watch::remainingMs(now_ms, last_step_baseline_attempt_ms, STEP_BASELINE_RETRY_MS)
      : 0;
  return std::min(wait, retry_wait);
}

static bool deferCtsForStepReset() {
  return step_clock_ready && pedometer_enabled &&
      watch::millisUntilMidnight(watch_clock.at(esp_timer_get_time())) <= STEP_CTS_GUARD_MS;
}

static void applyWatchTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
  start_date.year = year;
  start_date.month = month;
  start_date.day = day;
  start_date.wday = calculateWeekday(year, month, day);
  calendar_base_day = watch::civilDayNumber(year, month, day);
  last_rendered_day = -1;
  digital_dirty = true;

  uint32_t sec_of_day = static_cast<uint32_t>(hour) * 3600UL + static_cast<uint32_t>(minute) * 60UL + static_cast<uint32_t>(second);
  watch_clock.set(static_cast<uint64_t>(sec_of_day) * 1000ULL, esp_timer_get_time());
  // The next step service archives the outgoing date and restores this date's
  // history using one fresh sensor snapshot, including after a CTS correction.
  WATCH_LOG_I("Clock set: %04u-%02u-%02u %02u:%02u:%02u",
                static_cast<unsigned>(year), month, day, hour, minute, second);
}

static bool findCtsServer(NimBLEAddress& out_addr, const char* reason) {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(100);

  WATCH_LOG_D("CTS scan (%s): searching for service %s for %lu ms...",
              reason, CTS_SERVICE_UUID.toString().c_str(),
              static_cast<unsigned long>(CTS_SCAN_DURATION_MS));

  NimBLEScanResults results = scan->getResults(CTS_SCAN_DURATION_MS);
  const int count = results.getCount();
  if (count == 0) {
    WATCH_LOG_D("CTS scan: no BLE devices found");
    scan->clearResults();
    return false;
  }

  for (int i = 0; i < count; ++i) {
    const NimBLEAdvertisedDevice* dev = results.getDevice(static_cast<uint32_t>(i));
    if (!dev || !dev->isConnectable()) {
      continue;
    }
    if (!dev->isAdvertisingService(CTS_SERVICE_UUID)) {
      continue;
    }
    out_addr = dev->getAddress();
#if HACKTOR_LOG_LEVEL >= WATCH_LOG_LEVEL_DEBUG
    std::string name = dev->haveName() ? dev->getName() : std::string();
    if (!name.empty()) {
      WATCH_LOG_D("CTS scan: found %s (%s)", out_addr.toString().c_str(), name.c_str());
    } else {
      WATCH_LOG_D("CTS scan: found %s", out_addr.toString().c_str());
    }
#endif
    scan->clearResults();
    return true;
  }

  WATCH_LOG_D("CTS scan: no CTS advertiser found");
  scan->clearResults();
  return false;
}

static bool syncTimeFromCts(const char* reason, bool quick_attempt) {
  // This SDK has one BLE connection slot. A phone can keep its connection;
  // time sync waits until the slot is free, independently of advertising.
  if (watch_settings.airplane || cts_sync_in_progress ||
      activity_ble.closing() || activity_ble.connected()) {
    return false;
  }
  cts_sync_in_progress = true;
  last_cts_attempt_ms = lgfx::millis();

  // Share the host with the activity peripheral. Release only our private CTS
  // client when sharing, including failed connection/discovery/read attempts.
  struct CtsSyncCleanup {
    NimBLEClient* client = nullptr;
    bool shared_radio;
    explicit CtsSyncCleanup(bool shared) : shared_radio(shared) {}
    ~CtsSyncCleanup() {
      if (!watch::releaseCtsBle<NimBLEDevice>(client, shared_radio)) {
        WATCH_LOG_E("CTS: BLE cleanup failed");
      }
      ble_initialized = NimBLEDevice::isInitialized();
      cts_sync_in_progress = false;
    }
  } cleanup(activity_ble.active());

  if (!NimBLEDevice::isInitialized()) {
    ble_initialized = NimBLEDevice::init("HacktorWatch");
    if (!ble_initialized) {
      WATCH_LOG_E("CTS: BLE initialization failed");
      return false;
    }
  }

  auto tryConnect = [&](const NimBLEAddress& addr, const char* label) -> NimBLEClient* {
    WATCH_LOG_D("CTS sync (%s): connecting to %s (%s)...", reason, addr.toString().c_str(), label);
    NimBLEClient* c = NimBLEDevice::createClient();
    cleanup.client = c;
    if (!c) {
      WATCH_LOG_E("CTS: failed to create BLE client");
      return nullptr;
    }
    if (quick_attempt) {
      c->setConnectTimeout(5000);
      c->setConnectRetries(0);
    }
    if (!c->connect(addr)) {
      WATCH_LOG_W("CTS direct connect failed (%s)", label);
      return nullptr;
    }
    return c;
  };

  NimBLEAddress cts_addr;
  if (!findCtsServer(cts_addr, reason)) {
    return false;
  }
  // A phone may have joined while the synchronous scan was running. Preserve
  // its connection instead of competing for the SDK's single connection slot.
  if (activity_ble.connected()) return false;

  NimBLEClient* client = tryConnect(cts_addr, "scan");
  if (!client) {
    return false;
  }

  NimBLERemoteService* svc = client->getService(CTS_SERVICE_UUID);
  if (!svc) {
    WATCH_LOG_W("CTS service not found");
    return false;
  }
  NimBLERemoteCharacteristic* chr = svc->getCharacteristic(CTS_CHAR_UUID);
  if (!chr || !chr->canRead()) {
    WATCH_LOG_W("CTS characteristic not readable");
    return false;
  }

  std::string val = chr->readValue();
  if (val.size() < 7) {
    WATCH_LOG_W("CTS payload too short: %u bytes", static_cast<unsigned>(val.size()));
    return false;
  }

  const uint8_t* d = reinterpret_cast<const uint8_t*>(val.data());
  uint16_t year = static_cast<uint16_t>(d[0] | (d[1] << 8));
  uint8_t month = d[2];
  uint8_t day = d[3];
  uint8_t hour = d[4];
  uint8_t minute = d[5];
  uint8_t second = d[6];

  // CTS permits unknown calendar fields (zero), which cannot be displayed as
  // a concrete watch date. Reject those and malformed values before applying.
  if (year < 1 || year > 9999 || month < 1 || month > 12 ||
      day < 1 || hour >= 24 || minute >= 60 || second >= 60) {
    WATCH_LOG_W("CTS: invalid date/time fields");
    return false;
  }
  static constexpr uint8_t MONTH_DAYS[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  uint8_t max_day = MONTH_DAYS[month - 1];
  if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) {
    ++max_day;
  }
  if (day > max_day) {
    WATCH_LOG_W("CTS: invalid calendar date");
    return false;
  }

  applyWatchTime(year, month, day, hour, minute, second);
  last_cts_sync_ms = lgfx::millis();
  cts_has_synced = true;
  WATCH_LOG_I("CTS sync complete");
  return true;
}

static bool enablePedometer() {
  // Bank-A configuration must be written with both accelerometer and gyro off.
  // Startup's sensor reset guarantees that here. This is LSM6DSL-specific;
  // other LSM6DS variants put embedded-function enables in different registers.
  pedometer_enabled = watch::configureLsm6dslPedometer(imuReadRegister, imuWriteRegister);
  if (!pedometer_enabled) {
    WATCH_LOG_E("Pedometer enable failed");
  } else {
    WATCH_LOG_I("LSM6DSL pedometer configured and verified");
  }
  return pedometer_enabled;
}

#if HACKTOR_LOG_LEVEL >= WATCH_LOG_LEVEL_DEBUG
static void i2cScan() {
  WATCH_LOG_D("I2C scan start");
  for (uint8_t addr = 1; addr < 0x7F; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      WATCH_LOG_D(" - found device at 0x%02X", addr);
    }
    delay(2);
  }
  WATCH_LOG_D("I2C scan done");
}
#endif

static bool initImuWakeGesture(bool& counter_retained, uint16_t& retained_raw) {
  counter_retained = false;
  uint8_t whoami = 0;
  WATCH_LOG_D("IMU init start");

  const uint8_t candidates[] = {0x6A, 0x6B};
  bool found_addr = false;
  for (uint8_t addr : candidates) {
    imu_addr = addr;
    // A USB/CPU reset does not power-cycle the IMU. Restore the user register
    // bank before probing identity, even if an earlier boot left bank access on.
    imuWriteRegister(0x01, 0x00); // FUNC_CFG_ACCESS: user register bank.
    if (imuReadRegister(0x0F, whoami)) {
      WATCH_LOG_D("IMU candidate 0x%02X WHOAMI=0x%02X", addr, whoami);
      if (whoami == IMU_WHOAMI) {
        found_addr = true;
        break;
      }
    } else {
      WATCH_LOG_D("IMU candidate 0x%02X WHOAMI read failed", addr);
    }
  }
  if (!found_addr) {
    WATCH_LOG_E("IMU not detected on 0x6A/0x6B");
    return false;
  }

  // Read before flash restoration or destructive setup. A CPU/USB reset can
  // leave the IMU counting; a verified running configuration and committed
  // anchor marker let us preserve its counter and recover the unsaved delta.
  if (watch::lsm6dslRunning(imuReadRegister, IMU_WAKE_THRESHOLD) &&
      readStepCounter(retained_raw)) {
    counter_retained = true;
    step_anchor_trusted = true;
    pedometer_enabled = true;
    clearImuInterruptSource();
    WATCH_LOG_I("Retaining running IMU: raw=%u", static_cast<unsigned>(retained_raw));
    return true;
  }

  // Cold/unverified sensors retain the known-good reset/configuration path.
  // Keep the BDU anchor marker clear until a new raw baseline is saved.
  // AN5040's reset sequence requires gyro power-down and accel high performance.
  if (!imuWriteRegister(0x11, 0x00) || // CTRL2_G: gyro power-down.
      !imuWriteRegister(0x15, 0x00) || // CTRL6_C: XL_HM_MODE = 0.
      !imuWriteRegister(0x10, 0x40) || // CTRL1_XL: accel high performance, 104 Hz.
      !imuWriteRegister(0x12, 0x01)) { // CTRL3_C: SW_RESET.
    WATCH_LOG_E("IMU reset command failed");
    return false;
  }
  // A temporary NACK while reset is in progress must not disable motion and
  // steps for the entire boot. Require a confirmed ready read within 100 ms.
  if (!watch::waitForImuReset(imuReadRegister,
                            [](uint32_t ms) { delay(ms); },
                            []() -> uint32_t { return lgfx::millis(); })) {
    WATCH_LOG_E("IMU reset timed out");
    return false;
  }

  bool ok = true;
  ok &= imuWriteRegister(0x12, 0x04); // CTRL3_C: IF_INC = 1, anchor uncommitted.
  enablePedometer(); // Best effort, with accel/gyro still in power-down.
  // Ensure later writes cannot accidentally target an embedded register bank.
  if (!imuWriteRegister(0x01, 0x00)) return false;
  ok &= imuWriteRegister(0x0D, 0x00); // INT1_CTRL: no step-detect/data-ready IRQ.
  ok &= imuWriteRegister(0x0E, 0x00); // INT2_CTRL: no step-detect/data-ready IRQ.
  ok &= imuWriteRegister(0x18, 0x00); // CTRL9_XL: DEN/soft-iron off, reserved bits zero.
  ok &= imuWriteRegister(0x10, 0x40); // CTRL1_XL: 104 Hz, 2g.

  ok &= imuWriteRegister(0x58, 0x81); // TAP_CFG: INTERRUPTS_ENABLE | LIR; hold wake until acknowledged
  ok &= imuWriteRegister(0x5B, IMU_WAKE_THRESHOLD); // WAKE_UP_THS: threshold (higher = less sensitive)
  ok &= imuWriteRegister(0x5C, 0x00); // WAKE_UP_DUR: no additional duration requirement
  ok &= imuWriteRegister(0x5E, 0x20); // MD1_CFG: route wake-up interrupt to INT1
  ok &= imuWriteRegister(0x5F, 0x20); // MD2_CFG: route wake-up interrupt to INT2

  uint8_t ctrl1 = 0, ctrl3 = 0;
  ok &= imuReadRegister(0x10, ctrl1) && ctrl1 == 0x40;
  ok &= imuReadRegister(0x12, ctrl3) && ctrl3 == 0x04;
  if (!ok) {
    WATCH_LOG_E("IMU register write failed");
    return false;
  }

#if HACKTOR_LOG_LEVEL >= WATCH_LOG_LEVEL_DEBUG
  dumpPedometerConfig();
#endif
  clearImuInterruptSource();    // clear any stale wake flags
  if (!pedometer_enabled) {
    WATCH_LOG_W("Pedometer did not enable; step counter may stay at 0");
  }
  WATCH_LOG_I("IMU init done; waiting for wake interrupts on INT1");
  return true;
}

static uint32_t compileTimeSeconds() {
  const char* t = __TIME__;  // "hh:mm:ss"
  uint32_t hour = (t[0] - '0') * 10 + (t[1] - '0');
  uint32_t minute = (t[3] - '0') * 10 + (t[4] - '0');
  uint32_t second = (t[6] - '0') * 10 + (t[7] - '0');
  return hour * 3600UL + minute * 60UL + second;
}

static void triggerBacklight(uint32_t now_ms, watch::WakeReason reason = watch::WakeReason::Other) {
  if ((display_sleeping || backlight_level == 0) &&
      (pending_wake_reason == watch::WakeReason::Other || reason == watch::WakeReason::Button)) {
    pending_wake_reason = reason;
  }
  backlight_on_deadline = now_ms + backlightOnTimeMs();
  backlight_ramp_start = 0;
  backlight_ramping = false;
  // Render the current time/steps before illuminating a sleeping panel.
  backlight_triggered = true;
  info_dirty = true;
  digital_dirty = true;
  timer_dirty = true;
  settings_dirty = true;
}

static const char* watchfaceName() {
  switch (active_watchface) {
    case Watchface::Analog: return "analog";
    case Watchface::Digital: return "digital";
    case Watchface::Timer: return "timer";
  }
  return "analog";
}

static Watchface nextWatchface(Watchface face) {
  switch (face) {
    case Watchface::Analog: return Watchface::Digital;
    case Watchface::Digital: return Watchface::Timer;
    case Watchface::Timer: return Watchface::Analog;
  }
  return Watchface::Analog;
}

static Watchface previousWatchface(Watchface face) {
  switch (face) {
    case Watchface::Analog: return Watchface::Timer;
    case Watchface::Digital: return Watchface::Analog;
    case Watchface::Timer: return Watchface::Digital;
  }
  return Watchface::Analog;
}

static void interruptSettingsMotion(bool force_closed = false) {
  if (!settings_slide.active()) return;
  const bool keep_open = !force_closed &&
      (settings_slide.dragging() ? settings_slide.originOpen() : settings_slide.targetOpen());
  settings_slide.cancel();
  if (keep_open) settings_menu.resetTouch();
  else {
    settings_menu.close();
    manual_sync_requested = false;
    manual_sync_presented = false;
    watch_settings.brightness = saved_settings.brightness;
  }
  settings_dirty = true;
  digital_dirty = true;
  timer_dirty = true;
  info_dirty = true;
}

static void resetTouchInput() {
  face_touch.reset();
  if (settings_slide.dragging()) interruptSettingsMotion();
  if (face_slide.dragging()) {
    // An interrupted contact must not leave a static face partly offscreen.
    // The selected face stays the original until a drag is committed.
    face_slide.cancel();
    digital_dirty = true;
    timer_dirty = true;
    info_dirty = true;
  }
}

static watch::TimerUiState timerUiState() {
  switch (countdown.state()) {
    case watch::TimerState::Ready: return watch::TimerUiState::Ready;
    case watch::TimerState::Running: return watch::TimerUiState::Running;
    case watch::TimerState::Paused: return watch::TimerUiState::Paused;
  }
  return watch::TimerUiState::Ready;
}

static bool screenTouchVisible() {
  // A pending wake or a haptic-only rail wake must never accept a gesture.
  return !info_mode && !display_sleeping && backlight_level > 0;
}

static bool watchfaceTouchVisible() {
  return !settings_menu.isOpen() && !settings_slide.active() && screenTouchVisible();
}

static void initTouch() {
  interruptSettingsMotion(true);
  face_slide.cancel();
  resetTouchInput();
  settings_menu.resetTouch();
  touch_ready = i2c_ready && touch.begin(Wire);
  last_touch_poll_ms = lgfx::millis();
  if (!touch_ready) {
    WATCH_LOG_W("Touch controller unavailable; watchface button remains usable");
  }
}

static void updateTimer() {
  if (countdown.update(esp_timer_get_time())) {
    timer_dirty = true;
    settings_dirty = true;
    resetTouchInput(); // A held finger must not start the rearmed countdown.
    if (watch_settings.vibration) haptic_requests.requestTimer();
    WATCH_LOG_I("Timer finished");
  }
}

static bool preparePeripheralServicePower() {
  if (!peripherals_powered) {
    const uint32_t off_wait_ms = watch::remainingMs(lgfx::millis(), display_sleep_start_ms,
                                                    PERIPHERAL_POWER_OFF_MS);
    if (off_wait_ms) delay(off_wait_ms);
    power(HIGH);
    delay(PERIPHERAL_POWER_SETTLE_MS);
    // Keep LCD and touch reset LOW. Do not initialize or wake either device.
    // Q1 allows the brief glow accepted for sleeping alerts and midnight resets.
    pinMode(34, OUTPUT);
    digitalWrite(34, HIGH);
    driveLow(33);
  }
  return i2c_ready || startI2c();
}

static void serviceDailySteps(uint32_t now_ms) {
  if (!step_clock_ready) return;
  uint64_t sample_time_us = esp_timer_get_time();
  const bool day_changed = !daily_steps.hasDay() ||
      daily_steps.dayKey() != currentStepDay(sample_time_us);
  const bool checkpoint_due = step_checkpoint.due(now_ms);
  const bool baseline_due = pedometer_enabled && daily_steps.needsBaseline() &&
      (!step_baseline_attempted || !watch::remainingMs(
          now_ms, last_step_baseline_attempt_ms, STEP_BASELINE_RETRY_MS));
  const bool anchor_due = step_persistence_ready && daily_steps.needsAnchorSave() &&
      (!step_anchor_attempted || !watch::remainingMs(
          now_ms, last_step_anchor_attempt_ms, STEP_BASELINE_RETRY_MS));
  if (!day_changed && !checkpoint_due && !baseline_due && !anchor_due) return;

  bool sampled = false;
  if (pedometer_enabled) {
    step_baseline_attempted = true;
    last_step_baseline_attempt_ms = now_ms;
    uint16_t raw = 0;
    // Sample even when RAM is clean: the IMU counts while the screen is asleep.
    if (preparePeripheralServicePower()) {
      prepareStepDate(currentStepDay(esp_timer_get_time()));
    }
    if (i2c_ready && readStepCounter(raw)) {
      sample_time_us = esp_timer_get_time();
      prepareStepDate(currentStepDay(sample_time_us));
      daily_steps.sample(currentStepDay(sample_time_us), raw);
      sampled = true;
    } else {
      WATCH_LOG_W("Step snapshot unavailable; retaining known totals");
    }
  }
  if (!sampled) {
    sample_time_us = esp_timer_get_time();
    // On a failed boundary read, preserve the known old total and show the new
    // date's restored total immediately. Its baseline retries once per minute.
    prepareStepDate(currentStepDay(sample_time_us));
    daily_steps.observeDay(currentStepDay(sample_time_us));
  }
  publishDailySteps();

  const bool save_anchor = step_persistence_ready && daily_steps.needsAnchorSave() &&
      (!step_anchor_attempted || !watch::remainingMs(
          now_ms, last_step_anchor_attempt_ms, STEP_BASELINE_RETRY_MS));
  if (checkpoint_due || save_anchor) {
    // Regular totals stay on the 30-minute schedule. A cold/new-date baseline
    // needs one durable raw anchor first; failed anchor commits retry slowly.
    const uint32_t attempt_ms = lgfx::millis();
    step_checkpoint.attempted(attempt_ms);
    const uint32_t second = watch_clock.at(sample_time_us) / 1000ULL % 86400ULL;
    if (save_anchor) {
      step_anchor_attempted = true;
      last_step_anchor_attempt_ms = attempt_ms;
    }
    const bool gate_ready = !save_anchor || setStepAnchorTrusted(false);
    if (!gate_ready || !daily_steps.save(prefs, second)) {
      WATCH_LOG_W("Step save failed; anchor retries in one minute, totals at next checkpoint");
    } else if (!daily_steps.needsBaseline() && !setStepAnchorTrusted(true)) {
      WATCH_LOG_W("Step anchor saved but IMU continuity marker unavailable");
    }
  }
  if (display_sleeping && !backlight_triggered &&
      !haptic_alert.active() && !haptic_requests.pending()) parkPeripheralRail();
}

static void stopHapticPlayback() {
  const bool stopped = haptics.standby();
  const uint32_t now_ms = lgfx::millis();
  if (stopped || haptic_alert.timedOut(now_ms)) {
    haptic_alert.settle(now_ms);
  } else {
    // Retain the motion guard if the stop could not be confirmed. Retry only
    // at the next poll, within the existing playback watchdog.
    haptic_alert.markPolled(now_ms);
  }
  if (!stopped) WATCH_LOG_E("Haptic stop command failed");
}

static void serviceHaptics(uint32_t now_ms) {
  const bool screen_visible = !display_sleeping && backlight_level > 0;
  haptic_requests.expireConfirmation(now_ms, screen_visible);
  if (!watch_settings.vibration) {
    haptic_requests.mute();
    if (haptic_alert.playing() &&
        (haptic_alert.pollDue(now_ms) || haptic_alert.timedOut(now_ms))) stopHapticPlayback();
  }
  if (haptic_requests.pending() && !haptic_alert.active()) {
    // Consume each request once. Timer completion takes priority over feedback.
    const auto request = haptic_requests.take(now_ms, screen_visible);
    if (request != watch::HapticPlayback::None) {
      if (preparePeripheralServicePower() && haptics.begin(Wire)) {
        active_haptic = request;
        // Arm the guard before GO: even an ambiguous failed write must retain
        // motion suppression until the motor is stopped or the watchdog ends.
        haptic_alert.start(lgfx::millis(),
            request == watch::HapticPlayback::Confirmation ? 500U : 0U);
        const bool started = request == watch::HapticPlayback::Timer
            ? haptics.startSixPulse() : haptics.startConfirmation();
        if (!started) {
          WATCH_LOG_W("Vibration playback unavailable");
          stopHapticPlayback();
        }
        settings_dirty = true;
      } else {
        WATCH_LOG_W("Vibration driver unavailable");
        haptics.standby();
        haptics.end();
        if (display_sleeping && !backlight_triggered && !haptic_requests.pending())
          parkPeripheralRail();
      }
    }
    now_ms = lgfx::millis();
  }
  if (haptic_alert.playing()) {
    if (haptic_alert.timedOut(now_ms)) {
      WATCH_LOG_E("Haptic playback timeout");
      stopHapticPlayback();
    } else if (haptic_alert.stopDue(now_ms) ||
               (active_haptic == watch::HapticPlayback::Confirmation &&
                haptic_requests.timerPending() && haptic_alert.pollDue(now_ms))) {
      // Half-second feedback has a normal stop deadline; the separate hard
      // watchdog still protects failed stops. A timer may preempt feedback.
      haptic_alert.markPolled(now_ms);
      stopHapticPlayback();
    } else if (haptic_alert.pollDue(now_ms)) {
      bool playing = false;
      haptic_alert.markPolled(now_ms);
      if (!haptics.readPlaying(playing) || !playing) stopHapticPlayback();
    }
  }
  now_ms = lgfx::millis();
  if (haptic_alert.settling() && haptic_alert.waitMs(now_ms) == 0) {
    // Drain motor-generated latched motion before allowing ordinary wrist wake
    // again. Keep I2C powered during the brief mechanical settling interval.
    if (imu_ready) {
      // A failed read may leave a motor-generated level latched HIGH. Keep it
      // out of the wake sources until a valid clear or an observed LOW.
      imu_haptic_clear_pending = !i2c_ready || !clearImuInterruptSource();
    }
    imu_interrupt = false;
    imu_interrupt2 = false;
    haptics.end();
    haptic_alert.update(now_ms);
    active_haptic = watch::HapticPlayback::None;
    settings_dirty = true;
    if (display_sleeping && !backlight_triggered && !haptic_requests.pending())
      parkPeripheralRail();
  }
}

static void handleTimerAction(watch::TimerAction action, uint32_t now_ms) {
  switch (action) {
    case watch::TimerAction::SelectMinutes:
      timer_edit_seconds = false;
      break;
    case watch::TimerAction::SelectSeconds:
      timer_edit_seconds = true;
      break;
    case watch::TimerAction::Decrement:
    case watch::TimerAction::Increment: {
      const int delta = action == watch::TimerAction::Increment ? 1 : -1;
      if (timer_edit_seconds) countdown.adjustSeconds(delta);
      else countdown.adjustMinutes(delta);
      break;
    }
    case watch::TimerAction::StartPause:
      if (countdown.state() == watch::TimerState::Running) {
        countdown.pause(esp_timer_get_time());
      } else {
        countdown.start(esp_timer_get_time());
      }
      break;
    case watch::TimerAction::Reset:
      countdown.reset();
      break;
    case watch::TimerAction::None:
      return;
  }
  triggerBacklight(now_ms);
}

static void closeSettings(bool illuminate = true) {
  settings_slide.cancel();
  settings_menu.close();
  manual_sync_requested = false;
  manual_sync_presented = false;
  // An interrupted brightness drag was a preview, not a committed setting.
  watch_settings.brightness = saved_settings.brightness;
  resetTouchInput();
  if (illuminate) triggerBacklight(lgfx::millis());
}

static void dragSettings(bool was_open, int offset, uint32_t now_ms) {
  const bool starting = !settings_slide.dragging();
  if (starting) {
    face_slide.cancel();
    settings_slide.begin(was_open, now_ms);
    if (!was_open) {
      // Prepare the preview without handing this still-held contact to tiles.
      settings_menu.open();
      settings_status[0] = '\0';
      manual_sync_requested = false;
      manual_sync_presented = false;
    }
    settings_dirty = true;
    triggerBacklight(now_ms);
  }
  settings_slide.drag(offset, now_ms);
}

static void releaseSettings(bool commit, uint32_t now_ms) {
  if (!settings_slide.dragging()) return;
  settings_slide.release(commit, now_ms);
  triggerBacklight(now_ms);
}

static void handleSettingsEvent(const watch::SettingsEvent& event, uint32_t now_ms) {
  if (event.redraw) settings_dirty = true;
  switch (event.action) {
    case watch::SettingsAction::None: return;
    case watch::SettingsAction::Close:
      closeSettings();
      return;
    case watch::SettingsAction::ToggleAirplane: {
      const bool enable = !watch_settings.airplane;
      if (enable) {
        // The activity server owns its callbacks until the host has stopped.
        // Disconnect through its lifecycle, without deleting live objects here.
        activity_ble.requestStop(now_ms);
        activity_ble.service(now_ms);
        if (!activity_ble.ownsRadio() && NimBLEDevice::isInitialized() &&
            (!NimBLEDevice::deinit(false) || !NimBLEDevice::deinit(true))) {
          snprintf(settings_status, sizeof(settings_status), "Radio shutdown failed");
          WATCH_LOG_W("Airplane mode could not stop BLE");
          break;
        }
      }
      ble_initialized = NimBLEDevice::isInitialized();
      watch_settings.airplane = enable;
      manual_sync_requested = false;
      settings_status[0] = '\0';
      saveWatchSettings();
      break;
    }
    case watch::SettingsAction::ToggleVibration:
      watch_settings.vibration = !watch_settings.vibration;
      if (!watch_settings.vibration) {
        haptic_requests.mute();
        if (haptic_alert.playing()) stopHapticPlayback();
      } else haptic_requests.requestConfirmation(now_ms);
      saveWatchSettings();
      break;
    case watch::SettingsAction::ToggleBleAdvertising:
      watch_settings.ble_advertising = !watch_settings.ble_advertising;
      if (!watch_settings.ble_advertising) activity_ble.requestStop(now_ms);
      saveWatchSettings();
      break;
    case watch::SettingsAction::ToggleMotionWake:
      watch_settings.motion_wake = !watch_settings.motion_wake;
      imu_interrupt = false;
      imu_interrupt2 = false;
      if (watch_settings.motion_wake && imu_ready) {
        imu_haptic_clear_pending = !i2c_ready || !clearImuInterruptSource();
      }
      saveWatchSettings();
      break;
    case watch::SettingsAction::BrightnessPreview:
    case watch::SettingsAction::BrightnessCommit:
      watch_settings.brightness = static_cast<uint8_t>(std::max(0, std::min(100, event.value)));
      if (event.action == watch::SettingsAction::BrightnessCommit) saveWatchSettings();
      backlight(backlightTarget());
      break;
    case watch::SettingsAction::NextTimeout:
      switch (watch_settings.timeout_seconds) {
        case 5: watch_settings.timeout_seconds = 10; break;
        case 10: watch_settings.timeout_seconds = 15; break;
        case 15: watch_settings.timeout_seconds = 30; break;
        default: watch_settings.timeout_seconds = 5; break;
      }
      saveWatchSettings();
      break;
    case watch::SettingsAction::SyncTime:
      if (currentSettingsData().sync_available) {
        manual_sync_requested = true;
        manual_sync_presented = false;
        settings_menu.resetTouch();
        snprintf(settings_status, sizeof(settings_status), "Syncing...");
      }
      break;
    case watch::SettingsAction::ApplyTime: {
      const auto& t = settings_menu.editedTime();
      if (t.year >= 1 && t.year <= 9999 && t.month >= 1 && t.month <= 12 &&
          t.day >= 1 && t.day <= daysInMonth(t.year, t.month) &&
          t.hour >= 0 && t.hour < 24 && t.minute >= 0 && t.minute < 60) {
        applyWatchTime(t.year, t.month, t.day, t.hour, t.minute, 0);
        snprintf(settings_status, sizeof(settings_status), "Time set");
      }
      break;
    }
  }
  triggerBacklight(now_ms);
}

static void pollWatchTouch(uint32_t now_ms) {
  if (!screenTouchVisible() || !i2c_ready || !touch_ready ||
      button.shouldStayAwake() || (face_slide.active() && !face_slide.dragging()) ||
      (settings_slide.active() && !settings_slide.dragging())) {
    resetTouchInput();
    settings_menu.resetTouch();
    return;
  }
  if (now_ms - last_touch_poll_ms < TOUCH_POLL_MS) return;
  last_touch_poll_ms = now_ms;
  watch::TouchPoint point;
  if (!touch.read(point)) {
    // Do not turn an I2C failure into a release followed by an accidental tap.
    // An ACKed idle/invalid packet supplies no new contact information.
    if (touch.lastReadHadError()) {
      resetTouchInput();
      settings_menu.resetTouch();
      if (settings_menu.isOpen() && watch_settings.brightness != saved_settings.brightness) {
        watch_settings.brightness = saved_settings.brightness;
        backlight(backlightTarget());
        settings_dirty = true;
      }
    }
    return;
  }
  // A bounded I2C read can still cross the countdown deadline. An existing
  // contact must not start the configured timer that just replaced it.
  now_ms = lgfx::millis();
  updateTimer();
  if (point.pressed) {
    backlight_on_deadline = now_ms + backlightOnTimeMs();
    backlight_ramping = false;
    backlight_ramp_start = 0;
    if (backlight_level < backlightTarget()) backlight_triggered = true;
  }
  const bool opening_settings = settings_slide.dragging() && !settings_slide.originOpen();
  if (settings_menu.isOpen() && !opening_settings) {
    const auto event = settings_menu.update(currentSettingsData(), point.pressed,
                                            point.x, point.y, now_ms);
    if (event.drawer_dragging) dragSettings(true, event.drawer_offset, now_ms);
    else if (event.drawer_released) {
      releaseSettings(event.action == watch::SettingsAction::Close, now_ms);
    } else handleSettingsEvent(event, now_ms);
    return;
  }
  const auto input = face_touch.update(active_watchface == Watchface::Timer,
                                       timerUiState(), point.pressed,
                                       point.x, point.y, now_ms);
  if (input.settings_dragging) {
    dragSettings(false, input.settings_offset, now_ms);
    return;
  }
  if (input.settings_released) {
    releaseSettings(input.open_settings, now_ms);
    return;
  }
  if (input.dragging) {
    const bool starting = !face_slide.dragging();
    if (starting) outgoing_watchface = active_watchface;
    if (input.drag_offset != 0) {
      incoming_watchface = input.drag_offset < 0
          ? nextWatchface(outgoing_watchface) : previousWatchface(outgoing_watchface);
    }
    face_slide.drag(input.drag_offset, now_ms, clock_size);
    if (starting) triggerBacklight(now_ms);
    return;
  }
  if (input.drag_released) {
    if (face_slide.dragging()) {
      const bool commit = input.swipe != watch::SwipeDirection::None;
      if (commit) active_watchface = incoming_watchface;
      face_slide.release(commit, now_ms, clock_size);
      triggerBacklight(now_ms);
    }
    return;
  }
  handleTimerAction(input.timer_action, now_ms);
}

static void updateBacklight(uint32_t now_ms) {
  if (backlight_triggered || backlight_on_deadline == 0) {
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
      uint32_t duty = (backlightTarget() * (BACKLIGHT_FADE_MS - elapsed)) / BACKLIGHT_FADE_MS;
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

static watch::SettingsData currentSettingsData() {
  watch::SettingsData data;
  data.values = watch_settings;
  data.battery_percent = battery_percent;
  data.charging = is_charging;
  const uint64_t time_ms = watch_clock.at(esp_timer_get_time());
  const DateInfo date = advanceDays(start_date, static_cast<int32_t>(time_ms / 86400000ULL));
  const uint32_t second = static_cast<uint32_t>(time_ms / 1000ULL % 86400ULL);
  data.clock = watch::WatchDateTime(date.year, date.month, date.day,
                                  second / 3600U, second / 60U % 60U, second % 60U);
  data.sync_available = !watch_settings.airplane && !cts_sync_in_progress &&
      !manual_sync_requested && countdown.state() != watch::TimerState::Running &&
      !haptic_alert.active() && !haptic_requests.pending() && !deferCtsForStepReset() &&
      !activity_ble.closing() && !activity_ble.connected();
  data.sync_status = settings_status;
  if (!manual_sync_requested) {
    if (watch_settings.airplane)
      data.sync_status = activity_ble.ownsRadio() ? "Stopping radio..." : "Airplane mode on";
    else if (activity_ble.closing()) data.sync_status = "Stopping radio...";
    else if (activity_ble.connected()) data.sync_status = "Phone connected";
    else if (countdown.state() == watch::TimerState::Running) data.sync_status = "Timer running";
    else if (haptic_alert.active() || haptic_requests.pending()) data.sync_status = "Alert in progress";
    else if (deferCtsForStepReset()) data.sync_status = "Try after midnight";
  }
  data.has_synced = cts_has_synced;
  data.last_sync_age_s = cts_has_synced
      ? (lgfx::millis() - last_cts_sync_ms) / 1000U : 0;
  return data;
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

  clockbase.setFont(&lgfx::v1::fonts::Font2);
  clockbase.setTextDatum(lgfx::middle_center);
  clockbase.setTextColor(SECONDARY_COLOR, 6);
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
  clockbase.drawRect(x, y, icon_width - 4, icon_height, SECONDARY_COLOR);
  clockbase.fillRect(x + icon_width - 4, y + 3, 3, icon_height - 6, SECONDARY_COLOR);

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
  clockbase.setTextColor(SECONDARY_COLOR, 6);

  char buf[8];
  snprintf(buf, sizeof(buf), "%3u%%", percent);
  clockbase.drawString(buf, start_x + 20, start_y - 2);

  clockbase.setFont(prev_font);
  clockbase.setTextDatum(prev_datum);
}

static void drawStepCount(uint32_t steps) {
  char buf[12];
  if (steps >= 1000) {
    snprintf(buf, sizeof(buf), "%u,%03u", static_cast<unsigned>(steps / 1000),
             static_cast<unsigned>(steps % 1000));
  } else {
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(steps));
  }
  canvas.setFont(&lgfx::v1::fonts::Font2);
  canvas.setTextDatum(lgfx::middle_left);
  canvas.setTextColor(canvasColor(SECONDARY_COLOR), canvasColor(6));

  // Center the icon and number together, including when the digit count changes.
  const int row_width = STEP_ICON_WIDTH + STEP_ICON_GAP + canvas.textWidth(buf);
  const int row_x = clock_center - row_width / 2;
  canvas.drawBitmap(row_x, STEP_ROW_Y - STEP_ICON_HEIGHT / 2, step_shoe_bitmap,
                    STEP_ICON_WIDTH, STEP_ICON_HEIGHT, canvasColor(SECONDARY_COLOR));
  canvas.drawString(buf, row_x + STEP_ICON_WIDTH + STEP_ICON_GAP, STEP_ROW_Y);
}

static void drawInfoScreen(uint32_t now_ms) {
  // Compose a complete frame offscreen so refreshing never blanks the LCD.
  // Reuse the clock's canvas; its base and hand sprites remain separate.
  canvas.fillScreen(canvasColor(6));
  canvas.setTextColor(canvasColor(15), canvasColor(6));
  canvas.setFont(&lgfx::v1::fonts::Font2);
  canvas.setTextDatum(lgfx::middle_center);

  char line[80];
  const int cx = canvas.width() / 2;
  canvas.drawString("Debug", cx, 22);
  int y = 42;
  const int dy = 19;
  const auto stats = power_stats.snapshot(esp_timer_get_time());
  const uint64_t uptime_s = stats.uptime_us / 1000000ULL;
  const uint64_t screen_s = stats.screen_on_us / 1000000ULL;
  const uint64_t sleep_s = stats.light_sleep_us / 1000000ULL;
  const unsigned screen_percent = stats.uptime_us
      ? static_cast<unsigned>(stats.screen_on_us * 100ULL / stats.uptime_us) : 0;

  snprintf(line, sizeof(line), "Up %luh %lum", static_cast<unsigned long>(uptime_s / 3600),
           static_cast<unsigned long>(uptime_s / 60 % 60));
  canvas.drawString(line, cx, y);
  y += dy;
  snprintf(line, sizeof(line), "Screen %lum %lus (%u%%)",
           static_cast<unsigned long>(screen_s / 60),
           static_cast<unsigned long>(screen_s % 60), screen_percent);
  canvas.drawString(line, cx, y);
  y += dy;
  snprintf(line, sizeof(line), "Sleep %luh %lum", static_cast<unsigned long>(sleep_s / 3600),
           static_cast<unsigned long>(sleep_s / 60 % 60));
  canvas.drawString(line, cx, y);
  y += dy;
  snprintf(line, sizeof(line), "Wakes %lu: B%lu M%lu",
           static_cast<unsigned long>(stats.wake_count),
           static_cast<unsigned long>(stats.button_wakes),
           static_cast<unsigned long>(stats.motion_wakes));
  canvas.drawString(line, cx, y);
  y += dy;

  if (!imu_ready) snprintf(line, sizeof(line), "Steps: IMU unavailable");
  else if (!pedometer_enabled) snprintf(line, sizeof(line), "Steps: config error");
  else if (!step_read_valid) snprintf(line, sizeof(line), "Steps: read error");
  else snprintf(line, sizeof(line), "Steps: %lu (raw %u)",
                static_cast<unsigned long>(step_count), static_cast<unsigned>(raw_step_count));
  canvas.drawString(line, cx, y);
  y += dy;

  snprintf(line, sizeof(line), "Battery: %u%% %s", battery_percent, is_charging ? "chg" : "");
  canvas.drawString(line, cx, y);
  y += dy;
  if (cts_has_synced) {
    snprintf(line, sizeof(line), "CTS %lum ago",
             static_cast<unsigned long>((now_ms - last_cts_sync_ms) / 60000U));
  } else snprintf(line, sizeof(line), "CTS never");
  canvas.drawString(line, cx, y);
  y += dy;
  snprintf(line, sizeof(line), "Resets: %lu", static_cast<unsigned long>(reset_counter));
  canvas.drawString(line, cx, y);
  y += dy;
  snprintf(line, sizeof(line), "Face: %s", watchfaceName());
  canvas.drawString(line, cx, y);

  gfx.startWrite();
  canvas.pushSprite(0, 0); // Opaque copy also erases digits that became shorter.
  gfx.display();
  gfx.endWrite();

  last_info_render_ms = now_ms;
  info_dirty = false;
}

static void presentCanvas(int x = 0, int y = 0) {
  gfx.startWrite();
  canvas.pushSprite(&gfx, x, y); // Opaque, including black and clipped edges.
  gfx.display();
  // End the outer transaction before reusing the canvas: this waits for DMA.
  gfx.endWrite();
}

static void drawSettingsScreen() {
  watch::drawSettingsMenu(canvas, settings_menu, currentSettingsData(), canvas.hasPalette());
  presentCanvas();
  settings_dirty = false;
  if (manual_sync_requested) manual_sync_presented = true;
}

static void serviceManualSync() {
  if (!manual_sync_requested || !manual_sync_presented ||
      !settings_menu.isOpen() || settings_slide.active()) return;
  if (watch_settings.airplane || countdown.state() == watch::TimerState::Running ||
      haptic_alert.active() || haptic_requests.pending() || deferCtsForStepReset() ||
      activity_ble.closing() || activity_ble.connected()) {
    manual_sync_requested = false;
    manual_sync_presented = false;
    settings_dirty = true;
    return;
  }
  manual_sync_requested = false;
  manual_sync_presented = false;
  const bool synced = syncTimeFromCts("manual", true);
  snprintf(settings_status, sizeof(settings_status), "%s",
           synced ? "Time synced" : "No time server found");
  settings_menu.resetTouch();
  triggerBacklight(lgfx::millis());
}

static void composeDigitalClock(uint64_t time_ms) {
  const DateInfo date = advanceDays(start_date, static_cast<int32_t>(time_ms / 86400000ULL));
  const uint32_t minute_of_day = static_cast<uint32_t>((time_ms / 60000ULL) % 1440ULL);
  const watch::DigitalFaceData data{
    static_cast<int>(minute_of_day / 60), static_cast<int>(minute_of_day % 60),
    date.day, date.month, date.wday, step_count, battery_percent, is_charging,
    step_count * ESTIMATED_STEP_METERS / 1000.0f
  };
  watch::drawDigitalWatchface(canvas, data, canvas.hasPalette());
}

static void drawDigitalClock(uint64_t time_ms) {
  composeDigitalClock(time_ms);
  presentCanvas();
  last_digital_minute = time_ms / 60000ULL;
  digital_dirty = false;
}

static uint32_t timerSeconds(uint64_t now_us) {
  return (countdown.remainingMs(now_us) + 999U) / 1000U;
}

static void composeTimerScreen(uint32_t remaining_seconds) {
  const watch::TimerFaceData data{
    remaining_seconds, countdown.durationSeconds(), timerUiState(), timer_edit_seconds
  };
  watch::drawTimerWatchface(canvas, data, canvas.hasPalette());
}

static void drawTimerScreen(uint32_t remaining_seconds) {
  composeTimerScreen(remaining_seconds);
  presentCanvas();
  last_timer_seconds = remaining_seconds;
  timer_dirty = false;
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
  canvas.fillCircle(clock_center + sinx, clock_center + cosy, major_tick * 3 + 1,
                    canvasColor(palette));
}

static void composeClock(uint64_t time_ms) {
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
  drawBatteryGauge(battery_percent, charging);
  clockbase.pushSprite(0, 0);

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
  
  if (!canvas.hasPalette()) {
    needle1.pushRotateZoomWithAA(fhour, 1.0f, 1.0f, transpalette);
    needle3.pushRotateZoomWithAA(fmin , 1.0f, 1.0f, transpalette);
    needle2.pushRotateZoomWithAA(fsec , 0.7f, 1.0f, transpalette);
  } else {
    // LovyanGFX's palette AA assumes grayscale indices, unlike our color palette.
    needle1.pushRotateZoom(fhour, 1.0f, 1.0f, transpalette);
    needle3.pushRotateZoom(fmin , 1.0f, 1.0f, transpalette);
    needle2.pushRotateZoom(fsec , 0.7f, 1.0f, transpalette);
  }

  // highlight the second hand's pivot with a solid red cap
  canvas.fillCircle(clock_center, clock_center, 6, canvasColor(9));
  canvas.drawCircle(clock_center, clock_center, 2, canvasColor(1));
}

static void drawClock(uint64_t time_ms) {
  composeClock(time_ms);
  gfx.startWrite();
  // Copy black pixels too: RGB565 has no separate transparent-black index,
  // and skipping them would leave trails from the previous hand positions.
  canvas.pushRotateZoom(0.0f, zoom, zoom);
  gfx.display();
  gfx.endWrite();
}

static void composeWatchface(Watchface face, uint64_t now_us) {
  switch (face) {
    case Watchface::Analog:
      composeClock(watch_clock.at(now_us));
      break;
    case Watchface::Digital:
      composeDigitalClock(watch_clock.at(now_us));
      break;
    case Watchface::Timer:
      composeTimerScreen(timerSeconds(now_us));
      break;
  }
}

static void drawWatchfaceSlide(uint32_t now_ms) {
  if (!face_slide.due(now_ms)) return;
  const auto frame = face_slide.frame(now_ms, clock_size);
  // Count rendering/transfer time toward the cadence. Never wait for the
  // complete animation here; the main loop services timers and haptics first.
  face_slide.presented(now_ms);
  const uint64_t now_us = esp_timer_get_time();
  if (frame.outgoing_x > -clock_size && frame.outgoing_x < clock_size) {
    composeWatchface(outgoing_watchface, now_us);
    presentCanvas(frame.outgoing_x);
  }
  if (frame.incoming_x > -clock_size && frame.incoming_x < clock_size) {
    composeWatchface(incoming_watchface, now_us);
    presentCanvas(frame.incoming_x);
  }
  if (frame.finished) {
    face_slide.cancel();
    // Ignore contacts begun while controls were moving. The normal renderer
    // then refreshes the destination and commits its dirty/minute/second cache.
    resetTouchInput();
  }
}

static void drawSettingsSlide(uint32_t now_ms) {
  if (!settings_slide.due(now_ms)) return;
  const auto frame = settings_slide.frame(now_ms);
  settings_slide.presented(now_ms);
  if (frame.watchface_y > -clock_size && frame.watchface_y < clock_size) {
    composeWatchface(active_watchface, esp_timer_get_time());
    presentCanvas(0, frame.watchface_y);
  }
  if (frame.settings_y > -clock_size && frame.settings_y < clock_size) {
    watch::drawSettingsMenu(canvas, settings_menu, currentSettingsData(), canvas.hasPalette());
    presentCanvas(0, frame.settings_y);
  }
  if (frame.finished) {
    const bool open = settings_slide.targetOpen();
    settings_slide.cancel();
    if (open) settings_menu.resetTouch();
    else closeSettings(false);
    // Only the stationary destination can receive a fresh control contact.
    resetTouchInput();
    settings_dirty = true;
    digital_dirty = true;
    timer_dirty = true;
    info_dirty = true;
  }
}

static bool initSprites() {
  zoom = static_cast<float>(std::min(gfx.width(), gfx.height())) / clock_size;
  gfx.setPivot(gfx.width() >> 1, gfx.height() >> 1);

  clockbase.setColorDepth(lgfx::palette_4bit);
  needle1.setColorDepth(lgfx::palette_4bit);
  needle2.setColorDepth(lgfx::palette_4bit);
  needle3.setColorDepth(lgfx::palette_4bit);

  // Reserve the essential source sprites before spending extra RAM on AA.
  if (!clockbase.createSprite(clock_size, clock_size) ||
      !needle1.createSprite(9, 119) || !needle3.createSprite(9, 119) ||
      !needle2.createSprite(3, 119)) {
    WATCH_LOG_E("Unable to allocate watchface sprites");
    return false;
  }

#if HACKTOR_HAND_ANTIALIASING
  canvas.setColorDepth(lgfx::rgb565_2Byte);
  if (!canvas.createSprite(clock_size, clock_size)) {
    WATCH_LOG_W("RGB565 canvas allocation failed; disabling hand antialiasing");
#endif
    canvas.setColorDepth(lgfx::palette_4bit);
    if (!canvas.createSprite(clock_size, clock_size)) {
      WATCH_LOG_E("Unable to allocate watchface canvas");
      return false;
    }
#if HACKTOR_HAND_ANTIALIASING
  }
#endif
 
  if (canvas.hasPalette()) {
    applyPalette(canvas);
  }
  applyPalette(clockbase);
  applyPalette(needle1);
  applyPalette(needle2);
  applyPalette(needle3);


  canvas.setPivot(clock_center, clock_center);
  clockbase.setPivot(clock_center, clock_center);
  needle1.setPivot(3, 100);
  needle2.setPivot(1, 100);
  needle3.setPivot(3, 100);
  
  
  canvas.fillScreen(canvasColor(6));
  clockbase.fillScreen(transpalette);
  needle1.fillScreen(transpalette);
  needle3.fillScreen(transpalette);
 

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
 
  
  needle1.fillRect(0, 42, 7, 53, 15); // Short, broad white hour hand.
  needle1.fillCircle(3, 42, 3, 15);

  needle3.fillRect(2, 5, 3, 90, 15); // Long, slim white minute hand.
  needle3.fillCircle(3, 5, 1, 15);

  needle2.fillScreen(9); //seconds hand sprite
  needle2.fillRect(0, 99, 1, 2, 9);
  return true;
}

static bool parkPeripheralRail() {
  interruptSettingsMotion(true);
  face_slide.cancel();
#if HACKTOR_BACKLIGHT_RAIL_WORKAROUND
    if (!peripherals_powered) return true;
    // Q1's reversed body diode bypasses PWM. Remove its supply instead.
    // The IMU and fuel gauge stay powered by main 3V3/VBAT respectively.
    touch.end();
    haptics.end();
    touch_ready = false;
    resetTouchInput();
    const bool bus_was_ready = i2c_ready;
    const bool bus_stopped = Wire.end();
    if (bus_stopped || !bus_was_ready) {
      i2c_ready = false;
      // Keep main-rail I2C pullups from backfeeding the unpowered haptic/touch
      // ICs. Pull SCL down first so SDA falling cannot look like a START.
      driveLow(I2C_SCL);
      driveLow(I2C_SDA);
      gfx.releaseBus();
      const uint8_t display_pins[] = {
        LGFX_Hacktor::PIN_SCLK, LGFX_Hacktor::PIN_MOSI,
        LGFX_Hacktor::PIN_CS, LGFX_Hacktor::PIN_DC,
        LGFX_Hacktor::PIN_RST, TP_RST
      };
      for (uint8_t pin : display_pins) {
        driveLow(pin);
      }
      // This app does not use touch IRQ, external NOR, audio or the microphone.
      // Remove pulls/alternate output functions on their host signal pins too.
      const uint8_t unused_peripheral_pins[] = {1, 13, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42};
      for (uint8_t pin : unused_peripheral_pins) {
        pinMode(pin, INPUT);
        gpio_sleep_sel_dis(static_cast<gpio_num_t>(pin));
      }
      display_needs_init = true;
      power(LOW);
      display_sleep_start_ms = lgfx::millis();
      return true;
    } else {
      // Do not cut power if we could not first make the shared bus safe.
      WATCH_LOG_E("I2C shutdown failed; keeping peripheral rail on");
      // Wire.end() frees its buffers even if the driver reports an error.
      // Reopen it before letting any later read use those buffers.
      i2c_ready = false;
      if (startI2c() && !display_sleeping) initTouch();
    }
#endif
  return false;
}

static void sleepDisplay() {
  interruptSettingsMotion(true);
  if (settings_menu.isOpen()) closeSettings(false);
  face_slide.cancel();
  resetTouchInput();
  if (!display_sleeping) {
    gfx.sleep();
    display_sleep_start_ms = lgfx::millis();
    display_sleeping = true;
  }
  // The motor temporarily shares this rail; the LCD can sleep throughout.
  if (!haptic_alert.active() && !haptic_requests.pending()) parkPeripheralRail();
}

static bool wakeDisplay() {
  if (!display_sleeping) {
    return true;
  }

  // Recover a failed Wire shutdown/restart even in panel-only fallback mode.
  if (peripherals_powered && !i2c_ready && !startI2c()) {
    return false;
  }

  if (display_needs_init) {
    if (!peripherals_powered) {
      // An interrupt can arrive during shutdown. Allow the rail capacitors to
      // discharge before an immediate re-power so the panel gets a cold start.
      const uint32_t off_wait_ms = watch::remainingMs(lgfx::millis(), display_sleep_start_ms,
                                                      PERIPHERAL_POWER_OFF_MS);
      if (off_wait_ms != 0) {
        delay(off_wait_ms);
      }
      power(HIGH);
      delay(PERIPHERAL_POWER_SETTLE_MS);
    }
    // A haptic-only wake may already have restored the supply while keeping
    // display/touch in reset. Release touch only on this actual screen wake.
    digitalWrite(TP_RST, HIGH);
    pinMode(34, OUTPUT);
    digitalWrite(34, HIGH);
    driveLow(33);
    if (!i2c_ready && !startI2c()) {
      return false; // Leave the rail on and retry; never read a parked bus.
    }
    // SLP_OUT alone cannot recover a controller that has lost its supply.
    if (!gfx.init()) {
      WATCH_LOG_E("Display reinitialization failed");
      return false;
    }
    gfx.setColorDepth(16);
    gfx.setRotation(2);
    display_needs_init = false;
    display_sleeping = false;
    refresh_after_wake = true;
    info_dirty = true;
    digital_dirty = true;
    timer_dirty = true;
    initTouch();
    return true;
  }

  // The GC9A01 needs time after SLPIN even if a new gesture arrives at once.
  const uint32_t wait_ms = watch::remainingMs(lgfx::millis(), display_sleep_start_ms,
                                             PANEL_SLEEP_DELAY_MS);
  if (wait_ms != 0) {
    delay(wait_ms);
  }
  gfx.wakeup();
  delay(PANEL_WAKE_DELAY_MS);
  display_sleeping = false;
  refresh_after_wake = true;
  info_dirty = true;
  digital_dirty = true;
  timer_dirty = true;
  if (!touch_ready) initTouch();
  else resetTouchInput();
  return true;
}

static bool usbHostConnected() {
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  // Light sleep gates USB clocks. Keep the console and firmware upload usable
  // whenever plugged into a host, even if no serial monitor is open.
  return Serial.isPlugged();
#else
  return false;
#endif
}

static void captureImuWakeLevels() {
  if (!watch_settings.motion_wake) {
    imu_interrupt = false;
    imu_interrupt2 = false;
    return;
  }
  if (imu_ready) {
    if (digitalRead(IMU_INT1_PIN) == HIGH) {
      imu_interrupt = true;
    }
    if (digitalRead(IMU_INT2_PIN) == HIGH) {
      imu_interrupt2 = true;
    }
  }
}

static void restoreWakePins() {
  gpio_wakeup_disable(static_cast<gpio_num_t>(PIN_BTN));
  gpio_set_intr_type(static_cast<gpio_num_t>(PIN_BTN), GPIO_INTR_DISABLE);
  if (imu_ready) {
    // gpio_wakeup_enable changes the normal interrupt type too. Restore the
    // edge handlers before enabling interrupts, avoiding a latched-level ISR storm.
    gpio_wakeup_disable(static_cast<gpio_num_t>(IMU_INT1_PIN));
    gpio_wakeup_disable(static_cast<gpio_num_t>(IMU_INT2_PIN));
    gpio_set_intr_type(static_cast<gpio_num_t>(IMU_INT1_PIN), GPIO_INTR_POSEDGE);
    gpio_set_intr_type(static_cast<gpio_num_t>(IMU_INT2_PIN), GPIO_INTR_POSEDGE);
    gpio_intr_enable(static_cast<gpio_num_t>(IMU_INT1_PIN));
    gpio_intr_enable(static_cast<gpio_num_t>(IMU_INT2_PIN));
  }
}

static uint32_t nextCtsWaitMs(uint32_t now_ms) {
  if (watch_settings.airplane || activity_ble.closing() || activity_ble.connected())
    return MAX_STANDBY_SLEEP_MS;
  return watch::ctsWaitMs(now_ms, cts_has_synced, last_cts_sync_ms, last_cts_attempt_ms,
                         CTS_SYNC_INTERVAL_MS, CTS_RETRY_MS);
}

static void serviceActivityBle(uint32_t now_ms) {
  static bool was_connected = false;
  const bool owned_radio = activity_ble.ownsRadio();
  advertising_schedule.setEnabled(graphics_ready && watch_settings.ble_advertising &&
                                  !watch_settings.airplane);
  if (!activity_ble.ownsRadio() && advertising_schedule.shouldStart(now_ms))
    advertising_schedule.beginWindow(now_ms);
  const bool wanted = advertising_schedule.keepRadio(now_ms, activity_ble.connected());
  activity_ble.updateValues(step_count);
  if (!wanted) activity_ble.requestStop(now_ms);
  activity_ble.service(now_ms);
  // Short connectable windows leave the controller fully off between windows.
  // CTS can share an active host without deleting its GATT.
  if (wanted && !activity_ble.ownsRadio() && !NimBLEDevice::isInitialized()) {
    activity_ble.start(now_ms, step_count);
  }
  ble_initialized = NimBLEDevice::isInitialized();
  const bool connected = activity_ble.connected();
  if (owned_radio != activity_ble.ownsRadio() || was_connected != connected)
    settings_dirty = true;
  was_connected = connected;
}

static void sleepUntilNextTask() {
  if (!light_sleep_available || ble_initialized || activity_ble.ownsRadio() || cts_sync_in_progress ||
      usbHostConnected() || button.shouldStayAwake()) {
    delay(IDLE_RETRY_MS);
    return;
  }
  if (haptic_requests.pending() && !haptic_alert.active()) {
    return; // Start queued playback next loop; do not sleep through its request.
  }

  const uint32_t now_ms = lgfx::millis();
  // When I2C is parked, a battery poll would require re-powering the backlight.
  // Refresh it on user wake instead. Periodic short wakes still check for USB.
  // Running timers defer blocking CTS scans, so an overdue sync must not
  // prevent sleep. The countdown itself supplies a precise additional wake.
  const uint32_t cts_wait_ms = countdown.state() == watch::TimerState::Running ||
      haptic_alert.active() || haptic_requests.pending() || deferCtsForStepReset()
      ? MAX_STANDBY_SLEEP_MS : nextCtsWaitMs(now_ms);
  const uint32_t sleep_ms = std::min(MAX_STANDBY_SLEEP_MS, cts_wait_ms);
  // Battery reads are deferred until a visible wake, including when the rail
  // is temporarily on for haptics or remains on after a failed bus shutdown.
  uint64_t sleep_us = std::min<uint64_t>(
      static_cast<uint64_t>(sleep_ms) * 1000ULL, countdown.waitUs(esp_timer_get_time()));
  sleep_us = std::min<uint64_t>(sleep_us,
      static_cast<uint64_t>(haptic_alert.waitMs(lgfx::millis())) * 1000ULL);
  sleep_us = std::min<uint64_t>(sleep_us,
      static_cast<uint64_t>(nextDailyStepWaitMs(lgfx::millis(), esp_timer_get_time())) * 1000ULL);
  sleep_us = std::min<uint64_t>(sleep_us,
      static_cast<uint64_t>(advertising_schedule.waitMs(lgfx::millis())) * 1000ULL);
  if (sleep_us == 0) {
    return; // A scheduled task is already due; service it in the next loop.
  }

  if (imu_ready) {
    gpio_intr_disable(static_cast<gpio_num_t>(IMU_INT1_PIN));
    gpio_intr_disable(static_cast<gpio_num_t>(IMU_INT2_PIN));
  }
  const bool wake_on_motion = watch_settings.motion_wake && imu_ready && !haptic_alert.active() &&
      !imu_haptic_clear_pending;
  bool configured = gpio_wakeup_enable(static_cast<gpio_num_t>(PIN_BTN),
                                       GPIO_INTR_LOW_LEVEL) == ESP_OK;
  if (wake_on_motion) {
    configured &= gpio_wakeup_enable(static_cast<gpio_num_t>(IMU_INT1_PIN),
                                     GPIO_INTR_HIGH_LEVEL) == ESP_OK;
    configured &= gpio_wakeup_enable(static_cast<gpio_num_t>(IMU_INT2_PIN),
                                     GPIO_INTR_HIGH_LEVEL) == ESP_OK;
  }
  configured &= esp_sleep_enable_gpio_wakeup() == ESP_OK;
  configured &= esp_sleep_enable_timer_wakeup(sleep_us) == ESP_OK;
  if (!configured) {
    restoreWakePins();
    light_sleep_available = false;
    WATCH_LOG_E("Standby wake configuration failed; using idle delay");
    delay(IDLE_RETRY_MS);
    return;
  }

  // Check after arming wakeup. The latched IMU level also covers a gesture
  // arriving between this check and esp_light_sleep_start().
  captureImuWakeLevels();
  if ((wake_on_motion && (imu_interrupt || imu_interrupt2)) || digitalRead(PIN_BTN) == LOW) {
    restoreWakePins();
    delay(1);
    return;
  }

  const uint64_t sleep_started_us = esp_timer_get_time();
  const esp_err_t result = esp_light_sleep_start();
  if (result == ESP_OK) power_stats.slept(esp_timer_get_time() - sleep_started_us);
  // Normal edge ISRs were disabled while asleep; preserve the asserted source
  // before restoring them. A timer wake alone must not illuminate the screen.
  captureImuWakeLevels();
  restoreWakePins();
  if (result != ESP_OK) {
    delay(IDLE_RETRY_MS); // A rejected sleep must never turn into a busy loop.
  }
}

void setup() {
  power_stats.begin(esp_timer_get_time());
  power(HIGH);
  // Keep native USB initialized for uploads and the host/light-sleep guard.
  Serial.begin(115200);
#if HACKTOR_LOG_LEVEL > WATCH_LOG_LEVEL_OFF
  unsigned long wait_start = millis();
  while (!Serial && (millis() - wait_start) < 2000) {
    delay(10);
  }
#endif
  WATCH_LOG_I("Booting...");
  startI2c();
#if HACKTOR_LOG_LEVEL >= WATCH_LOG_LEVEL_DEBUG
  i2cScan();
#endif
  pinMode(PIN_CHG, INPUT_PULLUP);
  pinMode(IMU_INT1_PIN, INPUT_PULLUP);
  pinMode(IMU_INT2_PIN, INPUT_PULLUP);
  pinMode(PIN_BTN, INPUT_PULLUP);
  button.reset(digitalRead(PIN_BTN) == LOW, lgfx::millis());

  backlight(0);
  // Snapshot the existing counter before looking up its flash reference.
  // Touch stays reset throughout IMU probe/setup on the shared I2C bus.
  driveLow(TP_RST);
  delay(30);
  bool counter_retained = false;
  uint16_t retained_raw = 0;
  imu_ready = initImuWakeGesture(counter_retained, retained_raw);

  if (!prefs.begin("watch", false)) WATCH_LOG_W("Watch preferences unavailable");
  loadWatchSettings();
  watch::StepHistorySnapshot step_snapshot;
  const bool step_history_loaded = watch::loadStepHistory(prefs, step_snapshot);
  const bool steps_resumed = counter_retained && step_history_loaded &&
      daily_steps.resume(step_snapshot, retained_raw);
  if (!steps_resumed) {
    if (step_history_loaded) daily_steps.restore(step_snapshot);
    if (counter_retained && !setStepAnchorTrusted(false))
      WATCH_LOG_W("Legacy step baseline could not be marked pending");
  }
  publishDailySteps();
  step_checkpoint.begin(lgfx::millis());
  reset_counter = prefs.getUInt("reset_count", 0);
  ++reset_counter;
  prefs.putUInt("reset_count", reset_counter);
  
  WATCH_LOG_I("IMU startup: ready=%d pedometer=%d motion_setting=%d", imu_ready,
              pedometer_enabled, watch_settings.motion_wake);
  if (imu_ready) {
    attachInterrupt(digitalPinToInterrupt(IMU_INT1_PIN), imuInterruptHandler, RISING);
    attachInterrupt(digitalPinToInterrupt(IMU_INT2_PIN), imuInterruptHandler2, RISING);
    WATCH_LOG_I("INT1/INT2 interrupts attached");
  } else {
    WATCH_LOG_E("IMU init failed; backlight will only respond to manual triggers");
  }

  digitalWrite(TP_RST, HIGH);
  delay(50);
  gfx.init();
  gfx.setColorDepth(16);
  gfx.setRotation(2);  // flip display for watch orientation
  gfx.fillScreen(TFT_BLACK);
  initTouch();

  start_date = compileDateInfo();
  calendar_base_day = watch::civilDayNumber(start_date.year, start_date.month, start_date.day);
  last_rendered_day = -1;
  initTrigTable();
  graphics_ready = initSprites();
  if (!graphics_ready) {
    return;
  }
  battery_percent = readBatteryPercent();
  last_batt_update_ms = lgfx::millis();
  if (step_history_loaded) {
    // There is no persistent RTC. Resume the last checkpoint's displayed date
    // until CTS/manual time can supply the elapsed time across the reset.
    const watch::CivilDate date = watch::civilDateFromDayNumber(step_snapshot.checkpoint_day);
    const uint32_t second = step_snapshot.checkpoint_second;
    applyWatchTime(date.year, date.month, date.day,
                   second / 3600U, second / 60U % 60U, second % 60U);
  } else {
    watch_clock.set(static_cast<uint64_t>(compileTimeSeconds()) * 1000ULL, esp_timer_get_time());
  }
  step_clock_ready = true;
  // Commit a cold/migrated baseline before potentially blocking boot CTS.
  // A later CTS date correction establishes its own dated anchor if needed.
  step_persistence_ready = true;
  serviceDailySteps(lgfx::millis());
  if (!watch_settings.airplane && !deferCtsForStepReset()) syncTimeFromCts("boot");
  serviceDailySteps(lgfx::millis());
  triggerBacklight(lgfx::millis(), watch::WakeReason::Boot);
}

void loop() {
  if (!graphics_ready) {
    delay(100); // Keep USB responsive if even the fallback canvas cannot fit.
    return;
  }
  uint32_t now_ms = lgfx::millis();

  if (imu_haptic_clear_pending && digitalRead(IMU_INT1_PIN) == LOW &&
      digitalRead(IMU_INT2_PIN) == LOW) {
    imu_haptic_clear_pending = false;
    imu_interrupt = false;
    imu_interrupt2 = false;
  }
  // A level may have woken the chip while its normal edge ISR was disabled.
  captureImuWakeLevels();
  // Wake on the debounced press before the blocking display power-on sequence.
  // Decide short/long separately so opening status never also switches faces.
  const auto button_events = button.update(digitalRead(PIN_BTN) == LOW, now_ms);
  if (button_events.pressed) {
    interruptSettingsMotion();
    face_slide.cancel();
    resetTouchInput();
    settings_menu.resetTouch();
    triggerBacklight(now_ms, watch::WakeReason::Button);
  }
  if (button_events.long_press) {
    if (settings_menu.isOpen()) closeSettings(false);
    info_mode = !info_mode;
    resetTouchInput();
    triggerBacklight(now_ms);
  } else if (button_events.short_press) {
    if (settings_menu.isOpen()) {
      closeSettings(false);
    } else if (info_mode) {
      info_mode = false; // Return to the face that opened status.
    } else {
      active_watchface = nextWatchface(active_watchface);
    }
    resetTouchInput();
    triggerBacklight(now_ms);
  }
  updateTimer();
  serviceHaptics(lgfx::millis());
  serviceDailySteps(lgfx::millis());
  now_ms = lgfx::millis();
  // Restore both the supply and I2C BEFORE acknowledging the IMU or polling
  // battery/steps. BLE/timer wakes alone leave the rail off and the bus parked.
  if (display_sleeping && (backlight_triggered ||
      (watch_settings.motion_wake && !haptic_alert.active() && !imu_haptic_clear_pending &&
       (imu_interrupt || imu_interrupt2)))) {
    if (!wakeDisplay()) {
      delay(IDLE_RETRY_MS);
      return;
    }
    now_ms = lgfx::millis();
  }
  if (i2c_ready && (imu_interrupt || imu_interrupt2)) {
    imu_interrupt = false;
    imu_interrupt2 = false;
    uint8_t wake_src = 0;
    uint8_t status_reg = 0;
    const bool stale_haptic_motion = imu_haptic_clear_pending;
    if (imu_ready) {
      if (!imuReadRegister(0x1B, wake_src)) {
        WATCH_LOG_W("IMU: failed to read WAKE_UP_SRC");
      } else {
        imu_haptic_clear_pending = false;
      }
      if (!imuReadRegister(0x1E, status_reg)) {
        WATCH_LOG_W("IMU: failed to read STATUS_REG");
      }
    }
    bool within_cooldown = (now_ms - last_wake_trigger_ms) < WAKE_COOLDOWN_MS;
    WATCH_LOG_V("IMU interrupt @ %lu ms: WAKE_UP_SRC=0x%02X STATUS=0x%02X cooldown=%d",
                  static_cast<unsigned long>(now_ms), wake_src, status_reg, within_cooldown);
    if (watch_settings.motion_wake && !within_cooldown && !haptic_alert.active() && !stale_haptic_motion) {
      last_wake_trigger_ms = now_ms;
      triggerBacklight(now_ms, watch::WakeReason::Motion);
    }
  }

  // Expiry can fall inside the panel's blocking initialization. Process it
  // before touch controls, and keep the countdown independent of CTS time.
  now_ms = lgfx::millis();
  updateTimer();
  serviceDailySteps(now_ms);
  pollWatchTouch(now_ms);

  if (i2c_ready && (backlight_level > 0 || backlight_triggered || refresh_after_wake) &&
      (refresh_after_wake || now_ms - last_batt_update_ms >= BATTERY_REFRESH_MS)) {
    last_batt_update_ms = now_ms;
    const uint8_t percent = readBatteryPercent();
    if (percent != battery_percent) {
      battery_percent = percent;
      digital_dirty = true;
      info_dirty = true;
      settings_dirty = true;
    }
  }
  const bool charging = digitalRead(PIN_CHG) == LOW;
  if (charging != is_charging) {
    is_charging = charging;
    digital_dirty = true;
    info_dirty = true;
    settings_dirty = true;
  }

  if (i2c_ready && imu_ready && pedometer_enabled && (backlight_level > 0 || backlight_triggered) &&
      (refresh_after_wake || backlight_triggered || now_ms - last_step_poll_ms >= STEP_REFRESH_MS)) {
    uint16_t steps = 0;
    if (readStepCounter(steps)) {
      prepareStepDate(currentStepDay(esp_timer_get_time()));
      daily_steps.sample(currentStepDay(esp_timer_get_time()), steps);
      publishDailySteps();
      WATCH_LOG_D("Steps: raw=%u daily=%lu", static_cast<unsigned>(steps),
                  static_cast<unsigned long>(step_count));
    } else {
      WATCH_LOG_W("Step counter read failed");
    }
    last_step_poll_ms = now_ms;
  }

#if HACKTOR_LOG_LEVEL >= WATCH_LOG_LEVEL_VERBOSE
  if (backlight_level > 0 && now_ms - last_debug_ms > 2000) {
    last_debug_ms = now_ms;
    WATCH_LOG_V("DBG alive t=%lu bl=%u ramp=%d deadline=%lu imu_ready=%d pedo=%d irq1=%d irq2=%d steps=%u",
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
#endif

  // Finish a pending wake/face change before starting a blocking scan, and
  // never start one while a button gesture is being measured. Lit watchfaces
  // defer scans so a new swipe or timer tap cannot be blocked by BLE.
  if (!watch_settings.airplane && !settings_menu.isOpen() && !cts_sync_in_progress &&
      !activity_ble.ownsRadio() &&
      !backlight_triggered && !button.shouldStayAwake() &&
      !haptic_alert.active() && !haptic_requests.pending() &&
      countdown.state() != watch::TimerState::Running && !watchfaceTouchVisible() &&
      !face_slide.active() && !settings_slide.active() && !deferCtsForStepReset() &&
      digitalRead(PIN_BTN) != LOW && nextCtsWaitMs(now_ms) == 0) {
    syncTimeFromCts(cts_has_synced ? "periodic" : "retry");
    now_ms = lgfx::millis(); // BLE calls can block for several seconds.
  }

  now_ms = lgfx::millis();
  updateTimer();
  serviceHaptics(now_ms);
  serviceDailySteps(lgfx::millis());
  now_ms = lgfx::millis();
  updateBacklight(now_ms);
  if (backlight_level == 0 && !backlight_triggered) {
    serviceActivityBle(lgfx::millis());
    sleepDisplay();
    sleepUntilNextTask();
    return;
  }

  if (backlight_triggered) {
    if (!wakeDisplay()) {
      delay(IDLE_RETRY_MS);
      return;
    }
    now_ms = lgfx::millis();
  }
  if (info_mode) {
    if (info_dirty || (now_ms - last_info_render_ms) > 1000) {
      drawInfoScreen(now_ms);
    }
  } else if (settings_slide.active()) {
    drawSettingsSlide(now_ms);
  } else if (settings_menu.isOpen()) {
    if (settings_dirty) drawSettingsScreen();
  } else if (face_slide.active()) {
    drawWatchfaceSlide(now_ms);
  } else if (active_watchface == Watchface::Digital) {
    const uint64_t time_ms = watch_clock.at(esp_timer_get_time());
    if (digital_dirty || last_digital_minute != time_ms / 60000ULL) {
      drawDigitalClock(time_ms);
    }
  } else if (active_watchface == Watchface::Timer) {
    const uint32_t seconds = timerSeconds(esp_timer_get_time());
    if (timer_dirty || last_timer_seconds != seconds) {
      drawTimerScreen(seconds);
    }
  } else {
    drawClock(watch_clock.at(esp_timer_get_time()));
  }
  if (backlight_triggered) {
    backlight(backlightTarget());
    backlight_on_deadline = lgfx::millis() + backlightOnTimeMs();
    backlight_triggered = false;
  }
  refresh_after_wake = false;
  serviceActivityBle(lgfx::millis());
  serviceManualSync();
  if (settings_slide.active()) {
    delay(settings_slide.dragging() ? 5 : 1);
  } else if (face_slide.active()) {
    delay(face_slide.dragging() ? 5 : 1); // Touch is polled every 20 ms.
  } else if (settings_menu.isOpen() || info_mode || active_watchface != Watchface::Analog) {
    delay(5); // Static screens need no animation loop; keep button polling quick.
  }
}
