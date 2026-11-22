#include <Arduino.h>
#include <Wire.h>
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
#include "LGFX_Charlie.hpp"
#include <math.h>

#define QMI8658_ADDR 0x6B

LGFX_Charlie lcd;
XPowersPMU pmu;

float lookX = 0, lookY = 0;
float targetX = 0, targetY = 0;
float lidUpper = 0.08f, lidLower = 0.04f, lidAngle = 0.0f;
float rollDeg = 0.0f;
float breath = 0.0f;

int prevLx = 159, prevLy = 229, prevRx = 307, prevRy = 229;

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

  Wire.beginTransmission(0x20); Wire.write(0x03); Wire.write(0x00); Wire.endTransmission();
  Wire.beginTransmission(0x20); Wire.write(0x01); Wire.write(0x00); Wire.endTransmission();
  delay(50);
  Wire.beginTransmission(0x20); Wire.write(0x01); Wire.write(0xFF); Wire.endTransmission();
  delay(120);

  Wire.beginTransmission(QMI8658_ADDR); Wire.write(0x02); Wire.write(0x60); Wire.endTransmission();
  Wire.beginTransmission(QMI8658_ADDR); Wire.write(0x03); Wire.write(0x25); Wire.endTransmission();
  Wire.beginTransmission(QMI8658_ADDR); Wire.write(0x08); Wire.write(0x03); Wire.endTransmission();
}

static void drawEye(Arduino_GFX* g, int cx, int cy, int w, int h, float uLid, float lLid, float angle, bool happy) {
  int r = w / 3;

  // Soft electric-blue perimeter glow
  for (int i = 7; i >= 1; --i) {
    float a = powf((8 - i) / 8.0f, 1.8f) * 0.45f;
    uint16_t glowC = g->color565(0, (uint8_t)(212 * a), (uint8_t)(255 * a));
    g->fillRoundRect(cx - w/2 - i, cy - h/2 - i, w + i*2, h + i*2, r + i, glowC);
  }

  // Core electric-blue squircle
  uint16_t mainC = g->color565(0, 212, 255);
  g->fillRoundRect(cx - w/2, cy - h/2, w, h, r, mainC);

  // Upper eyelid (flat blink or angled glare)
  if (uLid > 0.01f || fabsf(angle) > 0.5f) {
    int lh = (int)(h * uLid);
    int tilt = (int)(tanf(angle * DEG_TO_RAD) * (w * 0.65f));
    int x0 = cx - w/2 - 12, x1 = cx + w/2 + 12, yTop = cy - h/2 - 20;
    g->fillTriangle(x0, yTop, x1, yTop, x0, cy - h/2 + lh + tilt, BLACK);
    g->fillTriangle(x1, yTop, x1, cy - h/2 + lh - tilt, x0, cy - h/2 + lh + tilt, BLACK);
  }

  // Lower eyelid (happy ^ ^ arch or flat squint)
  if (lLid > 0.01f) {
    int lh = (int)(h * lLid);
    if (happy) {
      g->fillCircle(cx, cy + h/2 + (w/2) - lh, (int)(w * 0.62f), BLACK);
    } else {
      g->fillRect(cx - w/2 - 12, cy + h/2 - lh, w + 24, lh + 18, BLACK);
    }
  }
}

void setup() {
  Serial.begin(115200);
  wakeHardware();
  lcd.init();
  lcd.setBrightness(255);
  lcd.gfx->fillScreen(BLACK);
}

void loop() {
  static uint32_t lastMs = millis();
  uint32_t now = millis();
  float dt = min(0.05f, (now - lastMs) / 1000.0f);
  lastMs = now;

  // 1. Motion sensor (gravity tilt + shake)
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

  int lx = 233 + (int)(offLx * cR - offLy * sR);
  int ly = 233 + (int)(offLx * sR + offLy * cR);
  int rx = 233 + (int)(offRx * cR - offRy * sR);
  int ry = 233 + (int)(offRx * sR + offRy * cR);

  // Erase only previous eye boxes so background stays pure OLED black with no flicker
  lcd.gfx->fillRect(prevLx - 65, prevLy - 82, 130, 164, BLACK);
  lcd.gfx->fillRect(prevRx - 65, prevRy - 82, 130, 164, BLACK);

  drawEye(lcd.gfx, lx, ly, (int)(98 * sc), (int)(126 * sc), lidUpper, lidLower, lidAngle, isHappy);
  drawEye(lcd.gfx, rx, ry, (int)(98 * sc), (int)(126 * sc), lidUpper, lidLower, -lidAngle, isHappy);

  prevLx = lx; prevLy = ly;
  prevRx = rx; prevRy = ry;

  delay(14);
}
