#include <Arduino.h>
#include <Wire.h>
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
#include "LGFX_Charlie.hpp"
#include <math.h>

#define QMI8658_ADDR 0x6B

LGFX_Charlie lcd;
XPowersPMU pmu;
Arduino_Canvas* canvas = nullptr;

float lookX = 0, lookY = 0;
float targetX = 0, targetY = 0;
float lidUpper = 0.08f, lidLower = 0.04f, lidAngle = 0.0f;
float rollDeg = 0.0f;
float breath = 0.0f;

bool blinking = false;
float blinkP = 0.0f;
uint32_t nextBlinkMs = 2500;
uint32_t nextGlanceMs = 3500;
uint32_t happyUntil = 0;
uint32_t annoyedUntil = 0;
uint32_t dizzyUntil = 0;

bool touching = false;
int16_t lastX = 0, lastY = 0;
float dragDist = 0.0f;
uint32_t touchStartMs = 0;

static void wakeHardware() {
  Wire.begin(15, 14);

  if (pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, 15, 14)) {
    pmu.setALDO1Voltage(3300); pmu.enableALDO1();
    pmu.setALDO2Voltage(3300); pmu.enableALDO2();
    pmu.setALDO3Voltage(3300); pmu.enableALDO3();
    pmu.setALDO4Voltage(3300); pmu.enableALDO4();
    pmu.setBLDO1Voltage(3300); pmu.enableBLDO1();
    pmu.setBLDO2Voltage(3300); pmu.enableBLDO2();
    pmu.setDLDO1Voltage(3300); pmu.enableDLDO1();
    delay(50);
  }

  // TCA9554 I/O expander reset release
  Wire.beginTransmission(0x20); Wire.write(0x03); Wire.write(0x00); Wire.endTransmission();
  Wire.beginTransmission(0x20); Wire.write(0x01); Wire.write(0x00); Wire.endTransmission();
  delay(50);
  Wire.beginTransmission(0x20); Wire.write(0x01); Wire.write(0xFF); Wire.endTransmission();
  delay(120);

  // QMI8658 motion sensor
  Wire.beginTransmission(QMI8658_ADDR); Wire.write(0x02); Wire.write(0x60); Wire.endTransmission();
  Wire.beginTransmission(QMI8658_ADDR); Wire.write(0x03); Wire.write(0x25); Wire.endTransmission();
  Wire.beginTransmission(QMI8658_ADDR); Wire.write(0x08); Wire.write(0x03); Wire.endTransmission();
}

static void drawEye(int cx, int cy, int w, int h, float uLid, float lLid, float angle, bool happy) {
  int r = w / 3;
  for (int i = 9; i >= 1; --i) {
    float a = powf((10 - i) / 10.0f, 1.8f) * 0.45f;
    uint16_t glowC = canvas->color565(0, (uint8_t)(212 * a), (uint8_t)(255 * a));
    canvas->fillRoundRect(cx - w/2 - i, cy - h/2 - i, w + i*2, h + i*2, r + i, glowC);
  }
  uint16_t mainC = canvas->color565(0, 212, 255);
  canvas->fillRoundRect(cx - w/2, cy - h/2, w, h, r, mainC);

  if (uLid > 0.01f || fabsf(angle) > 0.5f) {
    int lh = (int)(h * uLid);
    int tilt = (int)(tanf(angle * DEG_TO_RAD) * (w * 0.65f));
    int x0 = cx - w/2 - 14, x1 = cx + w/2 + 14, yTop = cy - h/2 - 25;
    canvas->fillTriangle(x0, yTop, x1, yTop, x0, cy - h/2 + lh + tilt, BLACK);
    canvas->fillTriangle(x1, yTop, x1, cy - h/2 + lh - tilt, x0, cy - h/2 + lh + tilt, BLACK);
  }

  if (lLid > 0.01f) {
    int lh = (int)(h * lLid);
    if (happy) {
      canvas->fillCircle(cx, cy + h/2 + (w/2) - lh, (int)(w * 0.62f), BLACK);
    } else {
      canvas->fillRect(cx - w/2 - 14, cy + h/2 - lh, w + 28, lh + 20, BLACK);
    }
  }
}

void setup() {
  Serial.begin(115200);
  wakeHardware();
  lcd.init();
  lcd.setBrightness(255);
  canvas = new Arduino_Canvas(466, 466, lcd.gfx);
  canvas->begin(GFX_SKIP_OUTPUT_BEGIN);
  canvas->fillScreen(BLACK);
  canvas->flush();
}

void loop() {
  static uint32_t lastMs = millis();
  uint32_t now = millis();
  float dt = min(0.05f, (now - lastMs) / 1000.0f);
  lastMs = now;

  // 1. Motion sensor
  static float avgMag = 1.0f;
  Wire.beginTransmission(QMI8658_ADDR);
  Wire.write(0x35);
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom(QMI8658_ADDR, 6) == 6) {
    float ax = (int16_t)(Wire.read() | (Wire.read() << 8)) / 4096.0f;
    float ay = (int16_t)(Wire.read() | (Wire.read() << 8)) / 4096.0f;
    float az = (int16_t)(Wire.read() | (Wire.read() << 8)) / 4096.0f;
    float targetRoll = atan2f(ax, ay) * RAD_TO_DEG;
    rollDeg += (targetRoll - rollDeg) * min(1.0f, dt * 8.0f);

    float mag = sqrtf(ax*ax + ay*ay + az*az);
    if (fabsf(mag - avgMag) > 1.35f) dizzyUntil = now + 2200;
    avgMag += (mag - avgMag) * 0.15f;
  }

  // 2. Touch screen
  int16_t tx, ty;
  if (lcd.getTouch(&tx, &ty)) {
    if (!touching) {
      touching = true;
      lastX = tx; lastY = ty;
      dragDist = 0;
      touchStartMs = now;
    } else {
      dragDist += hypotf(tx - lastX, ty - lastY);
      lastX = tx; lastY = ty;
      targetX = constrain((tx - 233) / 200.0f, -1.0f, 1.0f);
      targetY = constrain((ty - 233) / 200.0f, -1.0f, 1.0f);
      if (dragDist > 55.0f) happyUntil = now + 2200;
    }
  } else if (touching) {
    touching = false;
    if ((now - touchStartMs) < 300 && dragDist < 25.0f) {
      annoyedUntil = now + 1800;
      blinking = true; blinkP = 0;
    }
  }

  // 3. Expressions
  bool isHappy = (now < happyUntil);
  bool isAnnoyed = (now < annoyedUntil && !isHappy);
  bool isDizzy = (now < dizzyUntil);

  if (isDizzy) {
    targetX = cosf(now * 0.02f) * 0.85f;
    targetY = sinf(now * 0.02f) * 0.85f;
  } else if (!touching && now > nextGlanceMs) {
    targetX = random(-65, 66) / 100.0f;
    targetY = random(-45, 46) / 100.0f;
    nextGlanceMs = now + random(2000, 4500);
  }

  lookX += (targetX - lookX) * min(1.0f, dt * 7.0f);
  lookY += (targetY - lookY) * min(1.0f, dt * 7.0f);

  float goalU = isAnnoyed ? 0.28f : (isHappy ? 0.02f : 0.08f);
  float goalL = isHappy ? 0.30f : (isAnnoyed ? 0.14f : 0.04f);
  float goalAng = isAnnoyed ? 22.0f : 0.0f;

  if (now > nextBlinkMs && !blinking) {
    blinking = true; blinkP = 0;
    nextBlinkMs = now + random(2200, 5500);
  }
  if (blinking) {
    blinkP += dt * 11.0f;
    if (blinkP >= 1.0f) { blinking = false; blinkP = 0; }
    float b = (blinkP < 0.5f) ? blinkP * 2.0f : (1.0f - blinkP) * 2.0f;
    goalU = max(goalU, b);
  }

  lidUpper += (goalU - lidUpper) * min(1.0f, dt * 14.0f);
  lidLower += (goalL - lidLower) * min(1.0f, dt * 14.0f);
  lidAngle += (goalAng - lidAngle) * min(1.0f, dt * 12.0f);

  breath += dt * 1.8f;
  float sc = 1.0f + sinf(breath) * 0.015f;
  float rad = -rollDeg * DEG_TO_RAD;
  float cR = cosf(rad), sR = sinf(rad);

  float offLx = -74 + lookX * 24.0f;
  float offLy =  -4 + lookY * 18.0f;
  float offRx =  74 + lookX * 24.0f;
  float offRy =  -4 + lookY * 18.0f;

  canvas->fillScreen(BLACK);
  drawEye(233 + (int)(offLx * cR - offLy * sR), 233 + (int)(offLx * sR + offLy * cR),
          (int)(98 * sc), (int)(126 * sc), lidUpper, lidLower, lidAngle, isHappy);
  drawEye(233 + (int)(offRx * cR - offRy * sR), 233 + (int)(offRx * sR + offRy * cR),
          (int)(98 * sc), (int)(126 * sc), lidUpper, lidLower, -lidAngle, isHappy);
  canvas->flush();

  delay(12);
}
