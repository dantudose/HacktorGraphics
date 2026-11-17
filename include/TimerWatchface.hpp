#pragma once

#include <lgfx/v1/LGFX_Sprite.hpp>
#include <stdint.h>

#include "DigitalDigits.hpp"
#include "TimerLayout.hpp"

namespace watch {

namespace timer_detail {

static constexpr uint32_t ACCENT_RGB = 0xffc280U;
static constexpr uint8_t ACCENT_PALETTE = 11;

inline uint32_t color(bool indexed, uint8_t index, uint32_t rgb) {
  return indexed ? static_cast<uint32_t>(index) : rgb;
}

inline void drawPair(LGFX_Sprite& canvas, unsigned value, int x, int y,
                     bool indexed, bool highlighted) {
  uint32_t shades[16];
  for (unsigned i = 0; i < 16; ++i) {
    const unsigned r = ((highlighted ? (ACCENT_RGB >> 16) & 255U : 255U) * i + 7) / 15;
    const unsigned g = ((highlighted ? (ACCENT_RGB >> 8) & 255U : 255U) * i + 7) / 15;
    const unsigned b = ((highlighted ? ACCENT_RGB & 255U : 255U) * i + 7) / 15;
    shades[i] = indexed ? (highlighted ? ACCENT_PALETTE : 15U) : lgfx::color888(r, g, b);
  }
  for (unsigned place = 0; place < 2; ++place) {
    const unsigned digit = place == 0 ? (value / 10) % 10 : value % 10;
    unsigned offset = digital_detail::digit_offsets[digit];
    for (int row = 0; row < 68; ++row) {
      int column = 0;
      while (column < 44) {
        const uint8_t run = digital_detail::digit_coverage[offset++];
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

inline void button(LGFX_Sprite& canvas, const Rect& bounds, const char* text,
                   uint32_t foreground, uint32_t background, bool filled) {
  if (filled) {
    canvas.fillRoundRect(bounds.x, bounds.y, bounds.w, bounds.h, 15, foreground);
    canvas.setTextColor(background, foreground);
  } else {
    canvas.drawRoundRect(bounds.x, bounds.y, bounds.w, bounds.h, 15, foreground);
    canvas.setTextColor(foreground, background);
  }
  canvas.drawString(text, bounds.x + bounds.w / 2, bounds.y + bounds.h / 2);
}

}  // namespace timer_detail

// Compose onto the app's shared canvas. No sprite allocation or font loading;
// the large digits reuse the digital face's flash-only coverage data.
inline void drawTimerWatchface(LGFX_Sprite& canvas, const TimerFaceData& data,
                               bool palette_mode) {
  const auto previous_font = canvas.getFont();
  const auto previous_style = canvas.getTextStyle();
  using namespace timer_detail;
  const uint32_t black = color(palette_mode, 6, 0x000000U);
  const uint32_t white = color(palette_mode, 15, 0xffffffU);
  const uint32_t accent = color(palette_mode, ACCENT_PALETTE, ACCENT_RGB);
  const uint32_t secondary = color(palette_mode, 13, 0xc0c0c0U);
  const uint32_t divider = color(palette_mode, 4, 0x404040U);
  const bool ready = data.state == TimerUiState::Ready;
  const bool paused = data.state == TimerUiState::Paused;
  const uint32_t seconds = data.remaining_seconds > 5999 ? 5999 : data.remaining_seconds;

  canvas.fillScreen(black);
  canvas.setTextSize(1);
  canvas.setTextPadding(0);
  canvas.setTextDatum(lgfx::middle_center);
  canvas.setFont(&lgfx::fonts::Font2);
  canvas.setTextColor(white, black);
  canvas.drawString("Timer", 120, 28);

  if (ready) {
    canvas.setTextColor(data.edit_seconds ? secondary : accent, black);
    canvas.drawString("min", 69, 52);
    canvas.setTextColor(data.edit_seconds ? accent : secondary, black);
    canvas.drawString("sec", 171, 52);
  } else {
    canvas.setTextColor(secondary, black);
    canvas.drawString(paused ? "Paused" : "Running", 120, 50);
  }

  drawPair(canvas, seconds / 60, 25, 66, palette_mode, !ready || !data.edit_seconds);
  drawPair(canvas, seconds % 60, 127, 66, palette_mode, !ready || data.edit_seconds);
  canvas.fillCircle(120, 89, 2, secondary);
  canvas.fillCircle(120, 111, 2, secondary);

  if (ready) {
    canvas.drawFastHLine(data.edit_seconds ? 131 : 29, 137, 79, accent);
    button(canvas, minus_button, "-", accent, black, false);
    button(canvas, plus_button, "+", accent, black, false);
    button(canvas, primary_button, "Start", seconds ? accent : divider, black, true);
  } else {
    const uint32_t total = data.duration_seconds ? data.duration_seconds : 1;
    const uint32_t remaining = seconds > total ? total : seconds;
    canvas.fillRoundRect(52, 145, 136, 3, 1, divider);
    const unsigned width = 136ULL * remaining / total;
    if (width) canvas.fillRoundRect(52, 145, width, 3, 1, accent);
    button(canvas, pause_button, paused ? "Resume" : "Pause", accent, black, true);
    button(canvas, reset_button, "Reset", accent, black, false);
  }

  canvas.setFont(previous_font);
  canvas.setTextStyle(previous_style);
}

}  // namespace watch
