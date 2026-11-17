#pragma once

#include <lgfx/v1/LGFX_Sprite.hpp>
#include <stdint.h>
#include <stdio.h>

#include "DigitalDigits.hpp"

namespace watch {

struct DigitalFaceData {
  int hour;
  int minute;
  int day;
  int month;  // 1..12
  int wday;   // Sunday = 0, as in tm_wday
  uint32_t steps;
  uint8_t battery_percent;
  bool charging;
  float distance_km;
};

namespace digital_detail {

inline uint32_t color(bool indexed, uint8_t index, uint32_t rgb) {
  return indexed ? static_cast<uint32_t>(index) : rgb;
}

// The digits are composited straight onto the shared black canvas. A small
// coverage lookup on the stack replaces any extra glyph or full-screen sprite.
inline void drawTime(LGFX_Sprite& canvas, int value, int x, int y,
                     bool indexed, bool mint) {
  uint32_t shades[16];
  for (unsigned i = 0; i < 16; ++i) {
    const unsigned r = ((mint ? 160U : 255U) * i + 7) / 15;
    const unsigned g = ((mint ? 235U : 255U) * i + 7) / 15;
    const unsigned b = ((mint ? 170U : 255U) * i + 7) / 15;
    shades[i] = indexed ? (mint ? 7U : 15U) : lgfx::color888(r, g, b);
  }
  const unsigned number = static_cast<unsigned>(value < 0 ? 0 : value) % 100;
  for (unsigned place = 0; place < 2; ++place) {
    const unsigned digit = place == 0 ? number / 10 : number % 10;
    unsigned offset = digit_offsets[digit];
    for (int row = 0; row < 68; ++row) {
      int column = 0;
      while (column < 44) {
        const uint8_t run = digit_coverage[offset++];
        const unsigned coverage = run >> 4;
        const int length = (run & 15) + 1;
        if (coverage >= (indexed ? 8U : 1U)) {
          canvas.drawFastHLine(x + place * 44 + column, y + row, length,
                               shades[coverage]);
        }
        column += length;
      }
    }
  }
}

}  // namespace digital_detail

// Palette mode uses the existing shared palette: black 6, mint 7, divider 4,
// secondary text 13 and white 15. RGB565 mode preserves the digits' soft edges.
inline void drawDigitalWatchface(LGFX_Sprite& canvas,
                                 const DigitalFaceData& data,
                                 bool palette_mode) {
  const auto previous_font = canvas.getFont();
  const auto previous_style = canvas.getTextStyle();
  const uint32_t black = digital_detail::color(palette_mode, 6, 0x000000U);
  const uint32_t white = digital_detail::color(palette_mode, 15, 0xffffffU);
  const uint32_t mint = digital_detail::color(palette_mode, 7, 0xa0ebaaU);
  const uint32_t secondary = digital_detail::color(palette_mode, 13, 0xc0c0c0U);
  const uint32_t divider = digital_detail::color(palette_mode, 4, 0x404040U);

  canvas.fillScreen(black);
  canvas.setTextSize(1);
  canvas.setTextPadding(0);
  canvas.setTextDatum(lgfx::baseline_left);

  digital_detail::drawTime(canvas, data.hour, 28, 61, palette_mode, false);
  digital_detail::drawTime(canvas, data.minute, 28, 133, palette_mode, true);

  static const char* const months[] = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  static const char* const weekdays[] = {
      "Sunday", "Monday", "Tuesday", "Wednesday",
      "Thursday", "Friday", "Saturday"};
  const int month = data.month >= 1 && data.month <= 12 ? data.month - 1 : 0;
  const int wday = data.wday >= 0 && data.wday <= 6 ? data.wday : 0;
  constexpr int right_x = 134;
  char text[16];

  canvas.setFont(&lgfx::fonts::Roboto_Thin_24);
  canvas.setTextColor(mint, black);
  snprintf(text, sizeof(text), "%d", data.day);
  const int day_width = canvas.drawString(text, right_x, 49);
  canvas.setFont(&lgfx::fonts::Font2);
  canvas.setTextColor(white, black);
  canvas.drawString(months[month], right_x + day_width + 4, 49);
  canvas.setTextColor(secondary, black);
  canvas.drawString(weekdays[wday], right_x, 69);
  canvas.drawFastHLine(right_x, 78, 79, divider);

  canvas.setFont(&lgfx::fonts::Roboto_Thin_24);
  canvas.setTextColor(mint, black);
  snprintf(text, sizeof(text), "%u", static_cast<unsigned>(data.steps));
  canvas.drawString(text, right_x, 103);
  canvas.setFont(&lgfx::fonts::Font2);
  canvas.setTextColor(secondary, black);
  canvas.drawString("Steps", right_x, 121);
  canvas.drawFastHLine(right_x, 128, 79, divider);

  canvas.setFont(&lgfx::fonts::Roboto_Thin_24);
  canvas.setTextColor(mint, black);
  snprintf(text, sizeof(text), "%u%%", static_cast<unsigned>(data.battery_percent));
  canvas.drawString(text, right_x, 154);
  canvas.setFont(&lgfx::fonts::Font2);
  canvas.setTextColor(secondary, black);
  canvas.drawString(data.charging ? "Charging" : "Power", right_x, 172);
  canvas.drawFastHLine(right_x, 179, 67, divider);

  canvas.setFont(&lgfx::fonts::Roboto_Thin_24);
  canvas.setTextColor(mint, black);
  snprintf(text, sizeof(text), "%.1f", static_cast<double>(data.distance_km));
  canvas.drawString(text, right_x, 204);
  canvas.setFont(&lgfx::fonts::Font2);
  canvas.setTextColor(secondary, black);
  canvas.drawString("km est.", right_x, 221);

  canvas.setFont(previous_font);
  canvas.setTextStyle(previous_style);
}

}  // namespace watch
