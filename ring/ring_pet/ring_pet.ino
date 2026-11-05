// Ring Pet: a little creature living in the Freenove 8-LED ring, with a clicky wheel and a speaker, on the C3.
//  Ring, IN side only: V -> 5V, G -> GND, S -> GPIO10. Wheel: + -> 3V3, GND, CLK -> 3, DT -> 4, SW -> 5.
//  Speaker: GPIO20 -> 220 ohm -> speaker -> GPIO21.
//
//  Its face: two eyes at the top of the ring (they blink and look around), a mouth at the bottom.
//  The colour is its mood:  green = happy, cyan = excited, purple = bored, orange = hungry, dim blue = asleep.
//
//  Hold + turn the wheel . spins the face round the ring (to get the eyes on top); remembered
//  Turn the wheel ........ stroke it: the light swirls after your hand and it purrs
//  Tap the wheel ......... feed it: munch munch
//  Hold the wheel 1 s .... it tells you the time
//  Hold the wheel 3 s .... it looks at your laptop screen (with mac/run.sh + overlay running)
//  Hold BOOT 3 s ......... says the wiring
//  Laptop: run mac/run.sh and it reacts to what's on your screen (coding = concentrating, music = bopping,
//  YouTube = watching, games = excited, errors = worried, idle = falls asleep, switching apps a lot = dizzy).
//  It gets hungry every few hours and bored if ignored, sleeps 10pm-7am (wakes cross if you poke it),
//  and chirps for attention now and then in the day. Its mood is remembered when unplugged.
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include "secrets.h"
#include "clips.h"
#include "wiring.h"

#define RING 10
#define N 8
#define ENC_A 3
#define ENC_B 4
#define ENC_SW 5
#define BOOT 9
#define SPK_A 20
#define SPK_B 21
#define BRIGHT 45

// which ring LEDs make the face (LED 0 is wherever the ring's first LED sits; turn the ring so the eyes are on top)
const int EYE_L = 7, EYE_R = 1, MOUTH[3] = {3, 4, 5};

Adafruit_NeoPixel ring(N, RING, NEO_GRB + NEO_KHZ800);
Preferences prefs;

// ---------- sound ----------
void speakerIdle() {
  pinMode(SPK_A, OUTPUT); pinMode(SPK_B, OUTPUT);
  digitalWrite(SPK_A, LOW); digitalWrite(SPK_B, LOW);
}
void beep(int hz, int ms) {
  uint32_t half = 500000UL / hz, end = millis() + ms;
  while ((int32_t)(millis() - end) < 0) {
    digitalWrite(SPK_A, HIGH); digitalWrite(SPK_B, LOW); delayMicroseconds(half);
    digitalWrite(SPK_A, LOW); digitalWrite(SPK_B, HIGH); delayMicroseconds(half);
  }
  speakerIdle();
}
void slide(int from, int to, int ms) {  // pitch glide: the pet's "voice"
  for (int i = 0; i < ms; i += 8) beep(from + (to - from) * i / ms, 8);
}
void playRaw(const uint8_t *d, uint32_t len, int rate) {
  ledcAttach(SPK_A, 156250, 8); ledcAttach(SPK_B, 156250, 8); ledcOutputInvert(SPK_B, true);
  uint32_t t = micros();
  for (uint32_t i = 0; i < len; i++) {
    ledcWrite(SPK_A, d[i]); ledcWrite(SPK_B, d[i]);
    t += 1000000 / rate;
    while ((int32_t)(micros() - t) < 0) {}
  }
  ledcDetach(SPK_A); ledcDetach(SPK_B);
  speakerIdle();
}
void say(ClipId id) { playRaw(CLIPS[id].data, CLIPS[id].len, CLIP_RATE); delay(30); }
void sayNumber(int n) {
  if (n < 20) { say((ClipId)(W_ZERO + n)); return; }
  say((ClipId)(W_TWENTY + n / 10 - 2));
  if (n % 10) say((ClipId)(W_ZERO + n % 10));
}

void chirpHappy()   { slide(1400, 2400, 90); delay(40); slide(1800, 2800, 70); }
void chirpCall()    { slide(1200, 2200, 120); delay(120); slide(1200, 2200, 120); }
void chirpSad()     { slide(1600, 700, 350); }
void chirpGrumpy()  { beep(400, 120); delay(50); beep(330, 200); }
void chirpYawn()    { slide(900, 1500, 250); slide(1500, 500, 500); }
void munch()        { for (int i = 0; i < 3; i++) { beep(300 + random(150), 40); delay(90); } slide(1500, 2500, 80); }
void purr()         { for (int i = 0; i < 6; i++) { beep(120 + random(30), 12); delay(8); } }

// ---------- time ----------
bool haveTime(struct tm &t) { return getLocalTime(&t, 0) && t.tm_year > 120; }
void setClock(time_t when) {
  struct timeval tv = {when, 0}; settimeofday(&tv, NULL);
  setenv("TZ", TIMEZONE, 1); tzset();
}
void sayTime() {
  struct tm t;
  if (!haveTime(t)) { chirpSad(); return; }
  int h = t.tm_hour % 12; if (h == 0) h = 12;
  say(W_IT_S); sayNumber(h);
  if (t.tm_min == 0) say(W_O_CLOCK);
  else { if (t.tm_min < 10) say(W_OH); sayNumber(t.tm_min); }
  say(t.tm_hour < 12 ? W_AM : W_PM);
}
bool wWait = false;
uint32_t wStarted = 0, nextSync = 0;
void startSync() {
  if (strcmp(WIFI_SSID, "your-wifi-name") == 0) { nextSync = millis() + 0x7fffffff; return; }
  WiFi.mode(WIFI_STA); WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTzTime(TIMEZONE, "pool.ntp.org", "time.google.com");
  wWait = true; wStarted = millis();
}
void serviceSync() {
  if (!wWait) { if ((int32_t)(millis() - nextSync) >= 0) startSync(); return; }
  bool got = WiFi.status() == WL_CONNECTED && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
  if (got || millis() - wStarted > 30000) {
    WiFi.disconnect(true); WiFi.mode(WIFI_OFF); wWait = false;
    nextSync = millis() + (got ? 6UL * 3600 * 1000 : 10UL * 60 * 1000);
  }
}
// ---------- what's on the laptop screen (sent by mac/run.sh as "M <mood>") ----------
struct Look { const char *names; uint8_t r, g, b, eyes, mouth, sound; };
// eyes: 0 normal, 1 closed, 2 darting, 3 spinning, 4 steady (concentrating)
// mouth: 0 smile, 1 flat, 2 bopping to music, 3 wobbly
// sound when it starts: 0 none, 1 happy chirp, 2 sad, 3 yawn, 4 call
const Look LOOKS[] = {
  {" happy proud charging ",              20, 255, 60, 0, 0, 0},
  {" love chatting shy ",                 255, 60, 140, 0, 0, 1},
  {" celebrate laughing ",                255, 200, 0, 2, 0, 1},
  {" excited gaming shopping hungry ",    0, 220, 255, 2, 0, 1},
  {" focused thinking reading learning nerdy building creative mail news streak calm ", 120, 160, 255, 4, 1, 0},
  {" watching curious ",                  150, 60, 255, 0, 1, 0},
  {" music ",                             255, 0, 200, 0, 2, 0},
  {" worried lowbattery angry ",          255, 60, 0, 2, 3, 2},
  {" dizzy ",                             255, 255, 0, 3, 3, 0},
  {" bored waiting ",                     150, 0, 255, 0, 1, 4},
  {" sleepy late asleep night ",          0, 10, 90, 1, 1, 3},
};
int screenLook = -1;          // -1 = no laptop
uint32_t screenAt = 0;
void playSound(int s);
bool screenLive();

void readSerial() {  // "T<unix time>" from settime.py, "M <mood>" from brain.py
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n'); line.trim();
  if (line.startsWith("T")) { setClock((time_t)line.substring(1).toInt()); Serial.println("time set from Mac"); return; }
  if (!line.startsWith("M ")) return;
  String key = " " + line.substring(2) + " ";
  for (int i = 0; i < (int)(sizeof(LOOKS) / sizeof(LOOKS[0])); i++)
    if (strstr(LOOKS[i].names, key.c_str())) {
      if (i != screenLook) playSound(LOOKS[i].sound);
      screenLook = i; screenAt = millis();
      Serial.printf("screen mood %s\n", line.c_str() + 2);
      return;
    }
}

// ---------- the pet ----------
float food = 80, fun = 80;  // 0..100, go down over time
uint32_t lastTouch = 0, lastCall = 0, excitedUntil = 0, crossUntil = 0;
float swirl = -1;            // where the stroking light is, -1 = none
uint32_t swirlAt = 0;
float lookX = 0, lookTarget = 0;
uint32_t nextLook = 0, nextBlink = 0, blinkUntil = 0;

enum Mood { HAPPY, EXCITED, BORED, HUNGRY, ASLEEP, CROSS };
bool asleepNow() { struct tm t; return haveTime(t) && (t.tm_hour >= 22 || t.tm_hour < 7); }
Mood mood() {
  if (millis() < crossUntil) return CROSS;
  if (millis() < excitedUntil) return EXCITED;
  if (asleepNow()) return ASLEEP;
  if (food < 30) return HUNGRY;
  if (fun < 30) return BORED;
  return HAPPY;
}
void moodColour(Mood m, float &r, float &g, float &b) {
  switch (m) {
    case HAPPY:   r = 20;  g = 255; b = 60;  break;
    case EXCITED: r = 0;   g = 220; b = 255; break;
    case BORED:   r = 150; g = 0;   b = 255; break;
    case HUNGRY:  r = 255; g = 90;  b = 0;   break;
    case ASLEEP:  r = 0;   g = 10;  b = 90;  break;
    case CROSS:   r = 255; g = 0;   b = 0;   break;
  }
}

float px[N][3];
int rot = 0;  // turns the face round the ring; hold the wheel and turn to change it
void add(int i, float r, float g, float b) { i = ((i + rot) % N + N) % N; px[i][0] += r; px[i][1] += g; px[i][2] += b; }
void addAt(float pos, float r, float g, float b) {
  pos = fmodf(pos + N * 4, N);
  int a = (int)pos; float f = pos - a;
  add(a, r * (1 - f), g * (1 - f), b * (1 - f)); add(a + 1, r * f, g * f, b * f);
}
void show() {
  for (int i = 0; i < N; i++) ring.setPixelColor(i, min(255, (int)px[i][0]), min(255, (int)px[i][1]), min(255, (int)px[i][2]));
  ring.show(); memset(px, 0, sizeof(px));
}

void drawPet() {
  Mood m = mood();
  float r, g, b; moodColour(m, r, g, b);
  uint32_t now = millis();
  int eyes = m == ASLEEP ? 1 : 0, mouthStyle = -1;
  if (screenLive() && (m == HAPPY || m == BORED || m == ASLEEP)) {
    const Look &L = LOOKS[screenLook];
    r = L.r; g = L.g; b = L.b; eyes = L.eyes; mouthStyle = L.mouth;
    if (eyes == 1) m = ASLEEP; else if (m == ASLEEP) m = HAPPY;
  }
  float breathe = m == ASLEEP ? 0.5f + 0.5f * sinf(now / 1600.0f) : 0.75f + 0.25f * sinf(now / 900.0f);

  // eyes: look around now and then, blink every few seconds (asleep = closed, just a faint glow)
  if (eyes == 2 || m == EXCITED) { if (now > nextLook) { lookTarget = random(-10, 11) / 14.0f; nextLook = now + 200 + random(400); } }
  else if (eyes == 4) lookTarget = 0;
  else if (now > nextLook) { lookTarget = random(-10, 11) / 20.0f; nextLook = now + 1500 + random(4000); }
  lookX += (lookTarget - lookX) * (eyes == 2 ? 0.3f : 0.08f);
  if (now > nextBlink) { blinkUntil = now + 120; nextBlink = now + (eyes == 4 ? 6000 : 2500) + random(4000); }
  bool closed = eyes == 1 || now < blinkUntil;
  float e = closed ? 0.08f : 1.0f;
  if (eyes == 3) {  // dizzy: eyes chase each other round the ring
    float p = (now % 900) * N / 900.0f;
    addAt(p, 255, 255, 255); addAt(p + N / 2, 255, 255, 255);
  } else {  // each eye is exactly one LED; looking sideways just makes one eye a little brighter than the other
    float l = 1 - max(0.0f, lookX) * 0.6f, rr = 1 + min(0.0f, lookX) * 0.6f;
    add(EYE_L, 255 * e * l, 255 * e * l, 255 * e * l);
    add(EYE_R, 255 * e * rr, 255 * e * rr, 255 * e * rr);
  }

  // mouth in the mood colour: happy/excited = wide smile, hungry = opening and closing, bored/cross = one flat LED
  if (mouthStyle == 2) { int k = (now / 300) % 3; add(MOUTH[1], r, g, b); add(MOUTH[k == 1 ? 0 : 2], r, g, b); }
  else if (mouthStyle == 3) { float w = 0.5f + 0.5f * sinf(now / 120.0f); add(MOUTH[0], r * w, g * w, b * w); add(MOUTH[1], r, g, b); add(MOUTH[2], r * (1 - w), g * (1 - w), b * (1 - w)); }
  else if (mouthStyle == 1) add(MOUTH[1], r * breathe, g * breathe, b * breathe);
  else if (m == HAPPY || m == EXCITED) for (int i = 0; i < 3; i++) add(MOUTH[i], r * breathe, g * breathe, b * breathe);
  else if (m == HUNGRY) { float o = 0.5f + 0.5f * sinf(now / 250.0f); add(MOUTH[1], r, g, b); add(MOUTH[0], r * o, g * o, b * o); add(MOUTH[2], r * o, g * o, b * o); }
  else add(MOUTH[1], r * breathe, g * breathe, b * breathe);


  // the stroking light: follows the wheel, fades when you stop
  if (swirl >= 0) {
    float age = (now - swirlAt) / 600.0f;
    if (age > 1) swirl = -1;
    else for (int k = 0; k < 3; k++) addAt(swirl - k * 0.5f, 255 * (1 - age) / (k + 1), 200 * (1 - age) / (k + 1), 255 * (1 - age) / (k + 1));
  }
  ring.setBrightness(m == ASLEEP ? 8 : BRIGHT);
  show();
}

void spinColour(float r, float g, float b, int ms) {
  uint32_t end = millis() + ms;
  while ((int32_t)(millis() - end) < 0) {
    float p = (millis() % 600) * N / 600.0f;
    for (int k = 0; k < 4; k++) addAt(p - k * 0.6f, r / (k + 1), g / (k + 1), b / (k + 1));
    ring.setBrightness(BRIGHT); show(); delay(15);
  }
}

void playSound(int s) {
  if (asleepNow()) return;
  if (s == 1) chirpHappy(); else if (s == 2) chirpSad(); else if (s == 3) chirpYawn(); else if (s == 4) chirpCall();
}
bool screenLive() { return screenLook >= 0 && millis() - screenAt < 1800000UL; }  // laptop spoke in the last 30 min

void touched() {
  lastTouch = millis();
  if (asleepNow() && millis() > crossUntil) { crossUntil = millis() + 3000; chirpGrumpy(); }
}

int lastAB = 0, encAcc = 0;
int readWheel() {
  int ab = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  static const int8_t step[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
  encAcc += step[(lastAB << 2) | ab]; lastAB = ab;
  if (encAcc >= 4) { encAcc = 0; return 1; }
  if (encAcc <= -4) { encAcc = 0; return -1; }
  return 0;
}

void setup() {
  Serial.begin(115200);
  speakerIdle();
  pinMode(BOOT, INPUT_PULLUP);
  pinMode(ENC_A, INPUT_PULLUP); pinMode(ENC_B, INPUT_PULLUP); pinMode(ENC_SW, INPUT_PULLUP);
  lastAB = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  randomSeed(esp_random());
  prefs.begin("pet");
  rot = prefs.getInt("rot", 0); food = prefs.getFloat("food", 80); fun = prefs.getFloat("fun", 80);
  ring.begin(); ring.clear(); ring.show();
  setenv("TZ", TIMEZONE, 1); tzset();
  setCpuFrequencyMhz(80);
  spinColour(0, 220, 255, 800);   // waking up
  chirpHappy();
  lastTouch = millis();
  startSync();
}

void loop() {
  readSerial();
  serviceSync();
  uint32_t now = millis();

  // BOOT is only for the wiring: hold it 3 s
  static uint32_t bootDown = 0;
  if (digitalRead(BOOT) == LOW) {
    if (!bootDown) bootDown = now;
    if (now - bootDown >= 3000) {
      ring.clear(); ring.show();
      playRaw(WIRING, sizeof(WIRING), WIRING_RATE);
      while (digitalRead(BOOT) == LOW) delay(10);
      bootDown = 0;
    }
  } else bootDown = 0;

  // stroking: turn the wheel
  int turn = readWheel();
  static bool rotating = false;
  if (turn && digitalRead(ENC_SW) == LOW) {  // hold the wheel and turn: spin the face round until the eyes are on top
    rot = ((rot + turn) % N + N) % N; rotating = true;
    prefs.putInt("rot", rot); beep(2093, 6);
    turn = 0;
  }
  if (turn) {
    touched();
    if (!asleepNow()) {
      swirl = swirl < 0 ? (turn > 0 ? EYE_R : EYE_L) : swirl + turn * 0.5f;
      swirlAt = now;
      fun = min(100.0f, fun + 1.5f);
      static uint32_t lastPurr = 0;
      if (now - lastPurr > 250) { lastPurr = now; purr(); }
      static int strokes = 0; static uint32_t strokeStart = 0;
      if (now - strokeStart > 3000) { strokeStart = now; strokes = 0; }
      if (++strokes == 16) { excitedUntil = now + 4000; chirpHappy(); }  // a good long stroke
    }
  }

  // wheel button: tap = feed, hold 1 s (one beep) = the time, hold 3 s (two beeps) = AI looks at the laptop screen
  static bool swWas = false, held = false, held3 = false;
  static uint32_t swDown = 0;
  bool sw = digitalRead(ENC_SW) == LOW;
  if (sw && !swWas) { swDown = now; held = held3 = false; }
  if (sw && !held && now - swDown >= 1000) { held = true; beep(1319, 40); }
  if (sw && !held3 && now - swDown >= 3000) { held3 = true; beep(1760, 40); }
  if (!sw && swWas && rotating) { rotating = false; held = true; swWas = sw; return; }  // that was a rotate, not a press
  if (sw && rotating) held = held3 = true;  // no beeps or time while turning
  if (!sw && swWas && held) {
    touched();
    if (held3) { Serial.println("ASK"); excitedUntil = now + 3000; slide(900, 2400, 200); }
    else sayTime();
  }
  if (!sw && swWas && !held) {
    touched();
    if (!asleepNow()) {
      if (food > 90) { chirpGrumpy(); }  // full up
      else {
        food = min(100.0f, food + 35);
        for (int i = 0; i < 3; i++) {  // chomp animation
          float r, g, b; moodColour(HAPPY, r, g, b);
          add(MOUTH[0], 255, 180, 0); add(MOUTH[2], 255, 180, 0); if (i % 2) add(MOUTH[1], 255, 180, 0);
          add(EYE_L, 255, 255, 255); add(EYE_R, 255, 255, 255);
          ring.setBrightness(BRIGHT); show(); delay(120); drawPet(); delay(60);
        }
        munch();
        excitedUntil = now + 2500;
      }
    }
  }
  swWas = sw;

  // needs drop over time: hungry after ~3 hours, bored after ~45 minutes alone (much slower asleep)
  static uint32_t lastTickAt = 0;
  if (now - lastTickAt >= 10000) {
    lastTickAt = now;
    float k = asleepNow() ? 0.2f : 1.0f;
    food = max(0.0f, food - 0.06f * k);
    if (now - lastTouch > 60000) fun = max(0.0f, fun - 0.25f * k);
    static uint32_t lastSave = 0;
    if (now - lastSave > 300000) { lastSave = now; prefs.putFloat("food", food); prefs.putFloat("fun", fun); }
  }

  // calls for attention in the day: hungry/bored every ~10 min, otherwise a happy chirp now and then
  Mood m = mood();
  if (m != ASLEEP && now - lastTouch > 120000 && now - lastCall > 600000) {
    lastCall = now;
    if (m == HUNGRY || m == BORED) chirpCall(); else if (random(3) == 0) chirpHappy();
  }
  static Mood lastMood = HAPPY;
  if (m == ASLEEP && lastMood != ASLEEP) chirpYawn();
  lastMood = m;

  static uint32_t lastDraw = 0;
  if (now - lastDraw > 30) { lastDraw = now; drawPet(); }
  delay(1);
}
