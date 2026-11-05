// DeskPet for the Waveshare ESP32-S3-LCD-1.47(B).
// The Mac's brain.py sends one line per face over USB:  F <id> <bgRRGGBB> <deco> <anim>|<caption>
// On its own the pet still breathes and blinks, looks where you tilt it, gets dizzy if you shake it,
// giggles when you press BOOT, and falls asleep if the Mac goes quiet.
//
// Arduino IDE: board "ESP32S3 Dev Module", USB CDC On Boot: Enabled, PSRAM: OPI PSRAM, Flash Size: 16MB.
// Library: "GFX Library for Arduino" (moononournation).
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include "face.h"

// LCD pins from Waveshare's wiki for this board (ST7789, 172x320)
#define LCD_MOSI 45
#define LCD_SCLK 40
#define LCD_CS 42
#define LCD_DC 41
#define LCD_RST 39
#define LCD_BL 46
#define BOOT_BTN 0

Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *panel = new Arduino_ST7789(bus, LCD_RST, 0 /* rotation */, true /* IPS */, 172, 320, 34, 0, 34, 0);
Arduino_Canvas *canvas = new Arduino_Canvas(172, 320, panel);  // drawn off-screen, then sent whole: no flicker
Face face(canvas);

FaceState fromMac, shown;
uint32_t lastMsg = 0, pokeUntil = 0, dizzyUntil = 0;
bool gotMsg = false;
String rx;

// ---- QMI8658 motion sensor (the "B" board). Waveshare's wiki doesn't list its I2C pins, so look for it. ----
int imuAddr = 0;
float ax = 0, ay = 0, az = 1, lastMag = 1;

bool imuWrite(uint8_t reg, uint8_t v) { Wire.beginTransmission(imuAddr); Wire.write(reg); Wire.write(v); return Wire.endTransmission() == 0; }

void imuFind() {
  const int pins[][2] = {{11, 10}, {48, 47}, {6, 7}, {8, 9}, {4, 5}, {1, 2}, {13, 12}};
  for (auto &p : pins) {
    Wire.begin(p[0], p[1], 400000);
    for (int a : {0x6B, 0x6A}) {
      Wire.beginTransmission(a); Wire.write(0x00);
      if (Wire.endTransmission(false) == 0 && Wire.requestFrom(a, 1) == 1 && Wire.read() == 0x05) {  // WHO_AM_I
        imuAddr = a;
        imuWrite(0x02, 0x60);  // CTRL1: auto-increment addresses
        imuWrite(0x03, 0x15);  // CTRL2: accel +-4g, ~250 Hz
        imuWrite(0x08, 0x01);  // CTRL7: accel on
        Serial.printf("IMU at 0x%02X on SDA %d SCL %d\n", a, p[0], p[1]);
        return;
      }
    }
    Wire.end();
  }
  Serial.println("no IMU found (fine: no tilt/shake)");
}

void imuRead() {
  if (!imuAddr) return;
  Wire.beginTransmission(imuAddr); Wire.write(0x35);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(imuAddr, 6) != 6) return;
  int16_t v[3];
  for (int i = 0; i < 3; i++) { uint8_t lo = Wire.read(), hi = Wire.read(); v[i] = (int16_t)(hi << 8 | lo); }
  ax = v[0] / 8192.0f; ay = v[1] / 8192.0f; az = v[2] / 8192.0f;
  float mag = sqrtf(ax * ax + ay * ay + az * az);
  if (fabsf(mag - lastMag) > 0.9f) dizzyUntil = millis() + 3000;  // shaken
  lastMag = mag;
}

// ---- messages from the Mac ----
void handleLine(const String &line) {
  if (!line.startsWith("F ")) return;
  int bar = line.indexOf('|');
  String head = bar < 0 ? line : line.substring(0, bar);
  int id, deco, anim; char bgs[8] = {0};
  if (sscanf(head.c_str(), "F %d %6s %d %d", &id, bgs, &deco, &anim) != 4) return;
  fromMac.id = constrain(id, 0, 767);
  fromMac.bg = strtoul(bgs, nullptr, 16);
  fromMac.deco = deco; fromMac.anim = anim;
  String cap = bar < 0 ? "" : line.substring(bar + 1);
  cap.trim();
  strlcpy(fromMac.caption, cap.c_str(), sizeof(fromMac.caption));
  lastMsg = millis(); gotMsg = true;
}

void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BTN, INPUT_PULLUP);
  pinMode(LCD_BL, OUTPUT); digitalWrite(LCD_BL, HIGH);
  if (!canvas->begin(80000000)) Serial.println("canvas failed: turn on PSRAM in Tools");
  imuFind();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') { handleLine(rx); rx = ""; }
    else if (rx.length() < 120) rx += c;
  }
  if (digitalRead(BOOT_BTN) == LOW) pokeUntil = millis() + 1200;
  imuRead();

  uint32_t now = millis();
  shown = fromMac;
  if (!gotMsg) { shown.id = 576 + 1; shown.bg = 0xffe9a8; strcpy(shown.caption, "plug me in!"); }
  else if (now - lastMsg > 120000) {  // the Mac went quiet (asleep or brain.py stopped): nap
    shown.id = 2 * 48 + 2 * 4; shown.bg = 0x9fa8d8; shown.deco = 1; shown.anim = 0; shown.caption[0] = 0;
  }
  if (now < dizzyUntil) { shown.id = 7 * 48 + 5 * 4 + 2; shown.bg = 0xe9ffd1; shown.deco = 4; shown.anim = 2; strcpy(shown.caption, "so dizzy"); }
  if (now < pokeUntil) { shown.id = 1 * 48 + 1 * 4 + 1; shown.deco = 6; shown.anim = 1; strcpy(shown.caption, "hehe!"); }
  // eyes look the way it's tilted (screen facing you, USB on top)
  shown.lx = constrain(-ax * 8, -4.0f, 4.0f);
  shown.ly = constrain(ay * 8 - 2, -4.0f, 4.0f);

  face.draw(shown, now / 1000.0f);
  canvas->flush();
  delay(10);
}
