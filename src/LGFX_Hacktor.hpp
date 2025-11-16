#pragma once
#include <LovyanGFX.hpp>

class LGFX_Hacktor : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI      _bus;

public:
  LGFX_Hacktor() {
    // --- Update to your wiring ---
    const int8_t PIN_SCLK = 12, PIN_MOSI = 11, PIN_MISO = -1;
    const int8_t PIN_CS   = 4, PIN_DC   = 5, PIN_RST  = 3;

    auto bcfg = _bus.config();
    bcfg.spi_host   = SPI3_HOST;       // ESP32-S3
    bcfg.spi_mode   = 0;
    bcfg.freq_write = 60000000;        // try 80 MHz after it's stable
    bcfg.freq_read  = 16000000;
    bcfg.pin_sclk   = PIN_SCLK;
    bcfg.pin_mosi   = PIN_MOSI;
    bcfg.pin_miso   = PIN_MISO;
    bcfg.pin_dc     = PIN_DC;
    bcfg.use_lock   = true;
    _bus.config(bcfg);
    _panel.setBus(&_bus);

    auto pcfg = _panel.config();
    pcfg.pin_cs     = PIN_CS;
    pcfg.pin_rst    = PIN_RST;
    pcfg.panel_width  = 240;
    pcfg.panel_height = 240;
    pcfg.memory_width = 240;
    pcfg.memory_height= 240;
    pcfg.offset_rotation = 0;
    pcfg.invert = true;      // panel colors are inverted without this
    _panel.config(pcfg);

    setPanel(&_panel);
  }
};
