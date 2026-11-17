#pragma once

#include <cstdint>

#include "WatchSettings.hpp"

namespace watch {

enum class SettingsPage { Quick, Brightness, More, Date, Time };
enum class SettingsAction {
  None, Close, ToggleAirplane, ToggleVibration, ToggleMotionWake, ToggleBleAdvertising,
  BrightnessPreview, BrightnessCommit, NextTimeout, SyncTime, ApplyTime
};

struct SettingsEvent {
  SettingsAction action;
  int value;
  bool redraw;
  bool drawer_dragging = false;
  int drawer_offset = 0;
  bool drawer_released = false;
  SettingsEvent(SettingsAction a = SettingsAction::None, int v = 0, bool r = false)
      : action(a), value(v), redraw(r) {}
};

struct SettingsData {
  WatchSettings values;
  uint8_t battery_percent = 100;
  bool charging = false;
  WatchDateTime clock;
  bool sync_available = true;
  const char* sync_status = "";
  bool has_synced = false;
  uint32_t last_sync_age_s = 0;
};

namespace settings_detail {

struct Rect {
  int x, y, w, h;
  bool contains(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

static constexpr Rect airplane{44,60,72,56};
static constexpr Rect vibration{124,60,72,56};
static constexpr Rect motion{44,124,72,56};
static constexpr Rect brightness{124,124,72,56};
static constexpr Rect more{74,188,92,32};
static constexpr Rect back{42,23,46,30};
static constexpr Rect done{67,183,106,36};
static constexpr Rect slider{30,98,180,58};
static constexpr int slider_left = 45;
static constexpr int slider_right = 195;
static constexpr Rect timeout{42,58,156,31};
static constexpr Rect ble_advertising{42,94,156,31};
static constexpr Rect sync{42,130,156,31};
static constexpr Rect manual{42,166,156,31};
static constexpr Rect year{24,55,74,66};
static constexpr Rect month{101,55,54,66};
static constexpr Rect day{158,55,54,66};
static constexpr Rect hour{43,55,70,66};
static constexpr Rect minute{127,55,70,66};
static constexpr Rect minus{53,134,54,38};
static constexpr Rect plus{133,134,54,38};

inline bool inScreen(int x, int y) {
  if (x < 0 || y < 0 || x >= 240 || y >= 240) return false;
  const int dx = 2 * x - 239, dy = 2 * y - 239;
  return dx * dx + dy * dy <= 239 * 239;
}

enum class Hit {
  None, Airplane, Vibration, Motion, Brightness, More, Back, Done, Slider,
  Timeout, BleAdvertising, Sync, Manual, Year, Month, Day, Hour, Minute, Minus, Plus
};

inline Hit hitTest(SettingsPage page, int x, int y) {
  if (!inScreen(x, y)) return Hit::None;
  if (page == SettingsPage::Quick) {
    if (airplane.contains(x,y)) return Hit::Airplane;
    if (vibration.contains(x,y)) return Hit::Vibration;
    if (motion.contains(x,y)) return Hit::Motion;
    if (brightness.contains(x,y)) return Hit::Brightness;
    if (more.contains(x,y)) return Hit::More;
    return Hit::None;
  }
  if (back.contains(x,y)) return Hit::Back;
  if (page == SettingsPage::Brightness) {
    if (slider.contains(x,y)) return Hit::Slider;
    if (done.contains(x,y)) return Hit::Done;
  } else if (page == SettingsPage::More) {
    if (timeout.contains(x,y)) return Hit::Timeout;
    if (ble_advertising.contains(x,y)) return Hit::BleAdvertising;
    if (sync.contains(x,y)) return Hit::Sync;
    if (manual.contains(x,y)) return Hit::Manual;
  } else {
    if (minus.contains(x,y)) return Hit::Minus;
    if (plus.contains(x,y)) return Hit::Plus;
    if (done.contains(x,y)) return Hit::Done;
    if (page == SettingsPage::Date) {
      if (year.contains(x,y)) return Hit::Year;
      if (month.contains(x,y)) return Hit::Month;
      if (day.contains(x,y)) return Hit::Day;
    } else {
      if (hour.contains(x,y)) return Hit::Hour;
      if (minute.contains(x,y)) return Hit::Minute;
    }
  }
  return Hit::None;
}

inline int magnitude(int value) { return value < 0 ? -value : value; }
inline int sliderValue(int x) {
  if (x <= slider_left) return 0;
  if (x >= slider_right) return 100;
  return ((x - slider_left) * 100 + (slider_right - slider_left) / 2) /
      (slider_right - slider_left);
}

}  // namespace settings_detail

class SettingsMenu {
 public:
  void open() { open_ = true; page_ = SettingsPage::Quick; resetTouch(); }
  void close() { open_ = false; resetTouch(); }
  bool isOpen() const { return open_; }
  SettingsPage page() const { return page_; }
  const WatchDateTime& editedTime() const { return edited_; }
  unsigned dateField() const { return date_field_; }
  unsigned timeField() const { return time_field_; }

  void resetTouch() {
    armed_ = false;
    down_ = false;
    tap_ok_ = false;
    slider_active_ = false;
    drawer_active_ = false;
    drawer_rejected_ = false;
    repeated_ = false;
    held_hit_ = settings_detail::Hit::None;
  }

  SettingsEvent update(const SettingsData& data, bool pressed, int x, int y,
                        uint32_t now_ms) {
    using namespace settings_detail;
    if (!open_) return SettingsEvent();
    if (!pressed) {
      SettingsEvent event;
      if (down_ && slider_active_) {
        event = SettingsEvent(SettingsAction::BrightnessCommit, slider_value_, true);
      } else if (down_ && drawer_active_) {
        event = drawerEvent(false);
        const int dy = last_y_ - start_y_;
        if (dy <= -45 && -dy >= 2 * magnitude(last_x_ - start_x_)) {
          // Leave the menu open until the caller finishes its closing slide.
          event.action = SettingsAction::Close;
          event.redraw = true;
        }
      } else if (down_ && tap_ok_ && !repeated_ && page_ == page_at_press_) {
        event = activate(held_hit_, data);
      }
      armed_ = true;
      down_ = false;
      slider_active_ = false;
      drawer_active_ = false;
      drawer_rejected_ = false;
      tap_ok_ = false;
      repeated_ = false;
      return event;
    }
    if (x < 0 || x >= 240 || y < 0 || y >= 240) {
      resetTouch();
      return SettingsEvent();
    }
    if (!armed_) return SettingsEvent();
    if (!down_) {
      down_ = true;
      tap_ok_ = true;
      repeated_ = false;
      drawer_active_ = false;
      drawer_rejected_ = false;
      start_x_ = last_x_ = x;
      start_y_ = last_y_ = y;
      page_at_press_ = page_;
      held_hit_ = hitTest(page_, x, y);
      last_repeat_ms_ = now_ms;
      slider_active_ = held_hit_ == Hit::Slider;
      if (slider_active_) {
        slider_value_ = sliderValue(x);
        return SettingsEvent(SettingsAction::BrightnessPreview, slider_value_, true);
      }
      return SettingsEvent();
    }
    last_x_ = x;
    last_y_ = y;
    if (slider_active_) {
      const int value = sliderValue(x);
      if (value == slider_value_) return SettingsEvent();
      slider_value_ = value;
      return SettingsEvent(SettingsAction::BrightnessPreview, value, true);
    }
    if (drawer_active_) return drawerEvent(true);
    if (!drawer_rejected_ && !repeated_) {
      const int dx = magnitude(x - start_x_);
      const int dy = y - start_y_;
      if (dx >= 12 && dx >= 2 * magnitude(dy)) {
        drawer_rejected_ = true;
      } else if (dy <= -12 && -dy >= 2 * dx) {
        drawer_active_ = true;
        tap_ok_ = false;
        return drawerEvent(true);
      }
    }
    if (page_ != page_at_press_ || magnitude(x - start_x_) > 10 ||
        magnitude(y - start_y_) > 10 || hitTest(page_, x, y) != held_hit_) {
      tap_ok_ = false;
    }
    if (!tap_ok_ || (held_hit_ != Hit::Plus && held_hit_ != Hit::Minus)) {
      return SettingsEvent();
    }
    const uint32_t interval = repeated_ ? 120U : 500U;
    if (now_ms - last_repeat_ms_ < interval) return SettingsEvent();
    repeated_ = true;
    last_repeat_ms_ = now_ms;
    adjust(held_hit_ == Hit::Plus ? 1 : -1);
    return SettingsEvent(SettingsAction::None, 0, true);
  }

 private:
  SettingsEvent drawerEvent(bool pressed) const {
    SettingsEvent event;
    const int dy = last_y_ - start_y_;
    event.drawer_dragging = pressed;
    event.drawer_offset = dy < 0 ? dy : 0;
    event.drawer_released = !pressed;
    return event;
  }

  SettingsEvent activate(settings_detail::Hit hit, const SettingsData& data) {
    using settings_detail::Hit;
    switch (hit) {
      case Hit::Airplane: return SettingsEvent(SettingsAction::ToggleAirplane, 0, true);
      case Hit::Vibration: return SettingsEvent(SettingsAction::ToggleVibration, 0, true);
      case Hit::Motion: return SettingsEvent(SettingsAction::ToggleMotionWake, 0, true);
      case Hit::Brightness: page_ = SettingsPage::Brightness; break;
      case Hit::More: page_ = SettingsPage::More; break;
      case Hit::Back:
        page_ = page_ == SettingsPage::Time ? SettingsPage::Date :
            (page_ == SettingsPage::Date ? SettingsPage::More : SettingsPage::Quick);
        break;
      case Hit::Done:
        if (page_ == SettingsPage::Date) page_ = SettingsPage::Time;
        else if (page_ == SettingsPage::Time) {
          edited_.second = 0;
          page_ = SettingsPage::More;
          return SettingsEvent(SettingsAction::ApplyTime, 0, true);
        } else page_ = SettingsPage::Quick;
        break;
      case Hit::Timeout: return SettingsEvent(SettingsAction::NextTimeout, 0, true);
      case Hit::BleAdvertising:
        return SettingsEvent(SettingsAction::ToggleBleAdvertising, 0, true);
      case Hit::Sync:
        return SettingsEvent(data.sync_available ? SettingsAction::SyncTime : SettingsAction::None,
                             0, true);
      case Hit::Manual:
        edited_ = sanitizeDateTime(data.clock);
        date_field_ = 0;
        time_field_ = 0;
        page_ = SettingsPage::Date;
        break;
      case Hit::Year: date_field_ = 0; break;
      case Hit::Month: date_field_ = 1; break;
      case Hit::Day: date_field_ = 2; break;
      case Hit::Hour: time_field_ = 0; break;
      case Hit::Minute: time_field_ = 1; break;
      case Hit::Minus: adjust(-1); break;
      case Hit::Plus: adjust(1); break;
      default: return SettingsEvent();
    }
    return SettingsEvent(SettingsAction::None, 0, true);
  }

  void adjust(int delta) {
    if (page_ == SettingsPage::Date) {
      if (date_field_ == 0) edited_.year += delta;
      else if (date_field_ == 1) edited_.month += delta;
      else edited_.day += delta;
    } else if (page_ == SettingsPage::Time) {
      if (time_field_ == 0) edited_.hour += delta;
      else edited_.minute += delta;
    }
    edited_ = sanitizeDateTime(edited_);
  }

  bool open_ = false;
  bool armed_ = false;
  bool down_ = false;
  bool tap_ok_ = false;
  bool slider_active_ = false;
  bool drawer_active_ = false;
  bool drawer_rejected_ = false;
  bool repeated_ = false;
  SettingsPage page_ = SettingsPage::Quick;
  SettingsPage page_at_press_ = SettingsPage::Quick;
  settings_detail::Hit held_hit_ = settings_detail::Hit::None;
  int start_x_ = 0, start_y_ = 0, last_x_ = 0, last_y_ = 0;
  int slider_value_ = 100;
  uint32_t last_repeat_ms_ = 0;
  WatchDateTime edited_;
  unsigned date_field_ = 0, time_field_ = 0;
};

}  // namespace watch
