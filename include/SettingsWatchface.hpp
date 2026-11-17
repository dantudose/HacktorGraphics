#pragma once

#include <lgfx/v1/LGFX_Sprite.hpp>
#include <cstdio>

#include "SettingsMenu.hpp"

namespace watch {
namespace settings_detail {

inline uint32_t menuColor(bool indexed, uint8_t index, uint32_t rgb) {
  return indexed ? uint32_t(index) : rgb;
}

inline void menuButton(LGFX_Sprite& canvas, const Rect& rect, const char* label,
                       uint32_t foreground, uint32_t background, bool filled = false) {
  if (filled) {
    canvas.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 11, foreground);
    canvas.setTextColor(background, foreground);
  } else {
    canvas.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 11, foreground);
    canvas.setTextColor(foreground, background);
  }
  canvas.drawString(label, rect.x + rect.w / 2, rect.y + rect.h / 2);
}

// Smooth strokes on the normal RGB canvas. The palette fallback uses solid
// two-pixel strokes because its color indices are not a grayscale AA ramp.
inline void iconStroke(LGFX_Sprite& canvas, int x0, int y0, int x1, int y1,
                       uint32_t color) {
  if (!canvas.hasPalette()) {
    canvas.drawWideLine(x0,y0,x1,y1,0.85f,color);
  } else {
    canvas.drawLine(x0,y0,x1,y1,color);
    const int dx = x1 - x0;
    const int dy = y1 - y0;
    if (dx * dx >= dy * dy) canvas.drawLine(x0,y0 + 1,x1,y1 + 1,color);
    else canvas.drawLine(x0 + 1,y0,x1 + 1,y1,color);
  }
}

inline void iconRoundRect(LGFX_Sprite& canvas, int x, int y, int w, int h,
                          int radius, uint32_t color, uint32_t background) {
  if (canvas.hasPalette()) {
    canvas.fillRoundRect(x,y,w,h,radius,color);
    canvas.fillRoundRect(x + 2,y + 2,w - 4,h - 4,radius - 1,background);
  } else {
    canvas.fillSmoothRoundRect(x,y,w,h,radius,color);
    canvas.fillSmoothRoundRect(x + 2,y + 2,w - 4,h - 4,radius - 1,background);
  }
}

inline void sun(LGFX_Sprite& canvas, int x, int y, uint32_t color,
                 uint32_t background) {
  if (canvas.hasPalette()) {
    canvas.fillCircle(x,y,5,color);
    canvas.fillCircle(x,y,3,background);
  } else {
    canvas.fillSmoothCircle(x,y,5,color);
    canvas.fillSmoothCircle(x,y,3,background);
  }
  iconStroke(canvas,x,y - 11,x,y - 8,color);
  iconStroke(canvas,x,y + 8,x,y + 11,color);
  iconStroke(canvas,x - 11,y,x - 8,y,color);
  iconStroke(canvas,x + 8,y,x + 11,y,color);
  iconStroke(canvas,x - 8,y - 8,x - 6,y - 6,color);
  iconStroke(canvas,x + 6,y - 6,x + 8,y - 8,color);
  iconStroke(canvas,x - 8,y + 8,x - 6,y + 6,color);
  iconStroke(canvas,x + 6,y + 6,x + 8,y + 8,color);
}

inline void tileIcon(LGFX_Sprite& canvas, Hit icon, int x, int y, uint32_t color,
                      uint32_t background) {
  if (icon == Hit::Airplane) {
    // A diagonal fuselage, swept wings and a separate tail silhouette.
    canvas.fillTriangle(x + 2,y - 5,x - 10,y - 7,x - 12,y - 5,color);
    canvas.fillTriangle(x + 2,y - 5,x - 12,y - 5,x - 2,y - 1,color);
    canvas.fillTriangle(x + 5,y - 2,x + 7,y + 10,x + 5,y + 12,color);
    canvas.fillTriangle(x + 5,y - 2,x + 5,y + 12,x + 1,y + 2,color);
    canvas.fillTriangle(x - 7,y + 4,x - 12,y + 5,x - 8,y + 8,color);
    canvas.fillTriangle(x - 8,y + 8,x - 5,y + 12,x - 4,y + 7,color);
    if (canvas.hasPalette()) {
      canvas.fillTriangle(x - 9,y + 7,x + 7,y - 10,x + 10,y - 7,color);
      canvas.fillTriangle(x - 9,y + 7,x + 10,y - 7,x - 7,y + 9,color);
    } else {
      canvas.drawWideLine(x - 8,y + 8,x + 8,y - 8,1.7f,color);
    }
  } else if (icon == Hit::Vibration) {
    iconRoundRect(canvas,x - 5,y - 10,11,21,3,color,background);
    canvas.fillCircle(x,y + 6,1,color);
    for (int direction : {-1,1}) {
      iconStroke(canvas,x + direction * 9,y - 5,x + direction * 10,y,color);
      iconStroke(canvas,x + direction * 10,y,x + direction * 9,y + 5,color);
      iconStroke(canvas,x + direction * 11,y - 8,x + direction * 13,y - 3,color);
      iconStroke(canvas,x + direction * 13,y - 3,x + direction * 13,y + 3,color);
      iconStroke(canvas,x + direction * 13,y + 3,x + direction * 11,y + 8,color);
    }
  } else if (icon == Hit::Motion) {
    // Watch case, wrist strap and clock hands beside an upward rotation arrow.
    iconStroke(canvas,x - 8,y - 11,x - 8,y - 7,color);
    iconStroke(canvas,x - 2,y - 11,x - 2,y - 7,color);
    iconStroke(canvas,x - 8,y - 11,x - 2,y - 11,color);
    iconStroke(canvas,x - 8,y + 8,x - 8,y + 12,color);
    iconStroke(canvas,x - 2,y + 8,x - 2,y + 12,color);
    iconStroke(canvas,x - 8,y + 12,x - 2,y + 12,color);
    iconRoundRect(canvas,x - 11,y - 7,13,16,4,color,background);
    iconStroke(canvas,x - 5,y - 3,x - 5,y + 1,color);
    iconStroke(canvas,x - 5,y + 1,x - 2,y + 3,color);
    iconStroke(canvas,x + 6,y + 9,x + 10,y + 5,color);
    iconStroke(canvas,x + 10,y + 5,x + 12,y,color);
    iconStroke(canvas,x + 12,y,x + 11,y - 5,color);
    iconStroke(canvas,x + 11,y - 5,x + 7,y - 9,color);
    iconStroke(canvas,x + 7,y - 9,x + 7,y - 4,color);
    iconStroke(canvas,x + 7,y - 9,x + 12,y - 9,color);
  } else sun(canvas,x,y,color,background);
}

inline void menuTile(LGFX_Sprite& canvas, const Rect& bounds, const char* label,
                     Hit icon, bool enabled, uint32_t accent, uint32_t secondary,
                     uint32_t white, uint32_t black) {
  if (enabled) canvas.fillRoundRect(bounds.x,bounds.y,bounds.w,bounds.h,12,accent);
  else canvas.drawRoundRect(bounds.x,bounds.y,bounds.w,bounds.h,12,secondary);
  const uint32_t ink = enabled ? black : white;
  canvas.setTextColor(ink,enabled ? accent : black);
  tileIcon(canvas,icon,bounds.x + bounds.w / 2,bounds.y + 18,ink,
           enabled ? accent : black);
  canvas.drawString(label,bounds.x + bounds.w / 2,bounds.y + 43);
}

}  // namespace settings_detail

// Compose directly onto the shared canvas; caller owns presentation and power.
inline void drawSettingsMenu(LGFX_Sprite& canvas, const SettingsMenu& menu,
                             const SettingsData& data, bool indexed) {
  const auto previous_font = canvas.getFont();
  const auto previous_style = canvas.getTextStyle();
  using namespace settings_detail;
  const uint32_t black = menuColor(indexed,6,0x000000U);
  const uint32_t white = menuColor(indexed,15,0xffffffU);
  const uint32_t gray = menuColor(indexed,13,0xc0c0c0U);
  const uint32_t dark = menuColor(indexed,4,0x404040U);
  const uint32_t accent = menuColor(indexed,7,0xa0ebaaU);
  const uint32_t orange = menuColor(indexed,11,0xffc280U);
  canvas.fillScreen(black);
  canvas.setTextSize(1);
  canvas.setTextPadding(0);
  canvas.setTextDatum(lgfx::middle_center);
  canvas.setFont(&lgfx::fonts::Font2);
  canvas.setTextColor(white,black);
  char text[48];

  if (menu.page() == SettingsPage::Quick) {
    canvas.drawString("Settings",120,21);
    std::snprintf(text,sizeof(text),"%u%%%s",unsigned(data.battery_percent),
                  data.charging ? "  Charging" : " battery");
    canvas.setTextColor(data.charging ? accent : gray,black);
    canvas.drawString(text,120,41);
    menuTile(canvas,airplane,"Airplane",Hit::Airplane,data.values.airplane,accent,gray,white,black);
    menuTile(canvas,vibration,"Vibration",Hit::Vibration,data.values.vibration,accent,gray,white,black);
    menuTile(canvas,motion,"Wrist wake",Hit::Motion,data.values.motion_wake,accent,gray,white,black);
    menuTile(canvas,brightness,"Brightness",Hit::Brightness,false,accent,gray,white,black);
    menuButton(canvas,more,"More",gray,black);
  } else {
    menuButton(canvas,back,"<",gray,black);
    canvas.setTextColor(white,black);
    if (menu.page() == SettingsPage::Brightness) {
      canvas.drawString("Brightness",145,37);
      canvas.setFont(&lgfx::fonts::Roboto_Thin_24);
      std::snprintf(text,sizeof(text),"%u",unsigned(data.values.brightness));
      canvas.drawString(text,120,81);
      canvas.setFont(&lgfx::fonts::Font2);
      const int value = data.values.brightness > 100 ? 100 : data.values.brightness;
      const int position = slider_left + value * (slider_right - slider_left) / 100;
      canvas.fillRoundRect(slider_left,123,slider_right - slider_left,6,3,dark);
      if (position > slider_left) canvas.fillRoundRect(slider_left,123,position - slider_left,6,3,accent);
      canvas.fillCircle(position,126,10,accent);
      canvas.setTextColor(gray,black);
      canvas.drawString("0",slider_left,153);
      canvas.drawString("100",slider_right,153);
      menuButton(canvas,done,"Done",accent,black,true);
    } else if (menu.page() == SettingsPage::More) {
      canvas.drawString("More",145,37);
      std::snprintf(text,sizeof(text),"Screen: %u sec",unsigned(data.values.timeout_seconds));
      menuButton(canvas,timeout,text,gray,black);
      std::snprintf(text,sizeof(text),"BLE advertising: %s",data.values.ble_advertising ? "On" : "Off");
      menuButton(canvas,ble_advertising,text,data.values.ble_advertising ? accent : gray,black);
      menuButton(canvas,sync,"Sync time now",data.sync_available ? accent : dark,black);
      menuButton(canvas,manual,"Set date / time",gray,black);
      canvas.setTextColor(gray,black);
      if (data.sync_status) canvas.drawString(data.sync_status,120,206);
      if (data.has_synced) {
        if (data.last_sync_age_s < 60) std::snprintf(text,sizeof(text),"Last sync: %lus",static_cast<unsigned long>(data.last_sync_age_s));
        else if (data.last_sync_age_s < 3600) std::snprintf(text,sizeof(text),"Last sync: %lum",static_cast<unsigned long>(data.last_sync_age_s / 60));
        else if (data.last_sync_age_s < 86400) std::snprintf(text,sizeof(text),"Last sync: %luh",static_cast<unsigned long>(data.last_sync_age_s / 3600));
        else if (data.last_sync_age_s < 86400000U) std::snprintf(text,sizeof(text),"Last sync: %lud",static_cast<unsigned long>(data.last_sync_age_s / 86400));
        else std::snprintf(text,sizeof(text),"Last sync: >999d");
      } else std::snprintf(text,sizeof(text),"Not synced");
      canvas.drawString(text,120,224);
    } else {
      const bool date_page = menu.page() == SettingsPage::Date;
      canvas.drawString(date_page ? "Set date" : "Set time",145,37);
      const WatchDateTime& draft = menu.editedTime();
      if (date_page) {
        const int centers[] = {61,128,185};
        const char* const names[] = {"Year","Month","Day"};
        const int values[] = {draft.year,draft.month,draft.day};
        for (unsigned field = 0; field < 3; ++field) {
          const bool selected = menu.dateField() == field;
          canvas.setTextColor(selected ? orange : gray,black);
          canvas.drawString(names[field],centers[field],64);
          canvas.setFont(&lgfx::fonts::Roboto_Thin_24);
          std::snprintf(text,sizeof(text),field == 0 ? "%04d" : "%02d",values[field]);
          canvas.drawString(text,centers[field],94);
          canvas.setFont(&lgfx::fonts::Font2);
          if (selected) canvas.drawFastHLine(centers[field] - (field == 0 ? 27 : 17),115,
                                             field == 0 ? 54 : 34,orange);
        }
      } else {
        for (unsigned field = 0; field < 2; ++field) {
          const int x = field == 0 ? 78 : 162;
          const bool selected = menu.timeField() == field;
          canvas.setTextColor(selected ? orange : gray,black);
          canvas.drawString(field == 0 ? "Hour" : "Minute",x,64);
          canvas.setFont(&lgfx::fonts::Roboto_Thin_24);
          std::snprintf(text,sizeof(text),"%02d",field == 0 ? draft.hour : draft.minute);
          canvas.drawString(text,x,94);
          canvas.setFont(&lgfx::fonts::Font2);
          if (selected) canvas.drawFastHLine(x - 18,115,36,orange);
        }
        canvas.setTextColor(gray,black);
        canvas.drawString(":",120,94);
      }
      menuButton(canvas,minus,"-",orange,black);
      menuButton(canvas,plus,"+",orange,black);
      menuButton(canvas,done,date_page ? "Next" : "Save",accent,black,true);
    }
  }
  // A small upward chevron is a reminder of the dismissal gesture.
  if (menu.page() != SettingsPage::More) {
    canvas.drawLine(114,231,120,227,gray);
    canvas.drawLine(120,227,126,231,gray);
  }
  canvas.setFont(previous_font);
  canvas.setTextStyle(previous_style);
}

}  // namespace watch
