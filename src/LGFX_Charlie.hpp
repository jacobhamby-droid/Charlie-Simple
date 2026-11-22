#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>

class LGFX_Charlie {
public:
  Arduino_DataBus *bus = nullptr;
  Arduino_CO5300  *gfx = nullptr;

  LGFX_Charlie() {
    bus = new Arduino_ESP32QSPI(
      12 /* CS */, 38 /* SCK */, 4 /* D0 */, 5 /* D1 */, 6 /* D2 */, 7 /* D3 */
    );
    gfx = new Arduino_CO5300(
      bus, 39 /* RST */, 0 /* rotation */, 466 /* width */, 466 /* height */,
      6 /* col_offset1 */, 0 /* row_offset1 */, 0 /* col_offset2 */, 0 /* row_offset2 */
    );
  }

  void init() {
    gfx->begin();
    gfx->fillScreen(BLACK);
  }

  void setBrightness(uint8_t b) {
    gfx->setBrightness(b);
  }

  bool getTouch(int16_t *x, int16_t *y) {
    Wire.beginTransmission(0x5A);
    Wire.write(0xD0);
    Wire.write(0x00);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(0x5A, 7) != 7) return false;
    uint8_t b0 = Wire.read();
    uint8_t b1 = Wire.read();
    uint8_t b2 = Wire.read();
    uint8_t b3 = Wire.read();
    uint8_t b4 = Wire.read();
    Wire.read();
    Wire.read();
    if ((b0 & 0x0F) == 0) return false;
    *x = ((uint16_t)(b1 & 0x0F) << 8) | b2;
    *y = ((uint16_t)(b3 & 0x0F) << 8) | b4;
    return (*x < 466 && *y < 466);
  }
};
