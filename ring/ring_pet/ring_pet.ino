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
//  Hold the wheel 3 s .... looks at your laptop screen (with mac/run.sh running), or with no laptop, says something
//  No laptop: with Wi-Fi in secrets.h it shows the weather (sunny / rainy / snowy / stormy / cold) and, with a
//  Gemini key in secrets.h, chats out loud at 8:00, 12:30, 17:00 and 19:30 in a real voice.
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
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include "secrets.h"
#ifndef GEMINI_KEY
#define GEMINI_KEY ""        // no key: weather moods still work, it just doesn't talk
#endif
#ifndef LAT
#define LAT "51.5074"        // where the weather comes from (London)
#define LON "-0.1278"
#endif
#ifndef PET_OWNER
#define PET_OWNER "my human"
#endif
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
// ---------- on its own, with no laptop: Wi-Fi for a few seconds at a time ----------
// Every 30 minutes: time + weather. A few times a day (and when you hold the wheel 3 s with no laptop): it asks
// Gemini for something to say, fetches it as a real voice, saves it to flash, then plays it. All of this runs in
// its own task so the face keeps moving; Wi-Fi is off again within about 20 seconds.
volatile bool wantTalk = false, talkReady = false, netBusy = false;
volatile int weatherCode = -1;
volatile float tempC = 99;
uint8_t loud[256];  // makes the voice loud enough with no amplifier

bool wifiConfigured() { return strcmp(WIFI_SSID, "your-wifi-name") != 0; }

bool wifiUp() {
  WiFi.mode(WIFI_STA); WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 150 && WiFi.status() != WL_CONNECTED; i++) delay(100);
  return WiFi.status() == WL_CONNECTED;
}
void wifiDown() { WiFi.disconnect(true); WiFi.mode(WIFI_OFF); }

float numAfter(const String &s, const char *key, int from) {
  int i = s.indexOf(key, from);
  return i < 0 ? -999 : s.substring(i + strlen(key)).toFloat();
}

void fetchWeather() {
  HTTPClient http;
  http.begin("http://api.open-meteo.com/v1/forecast?latitude=" LAT "&longitude=" LON "&current=temperature_2m,weather_code");
  if (http.GET() == 200) {
    String s = http.getString();
    int cur = s.indexOf("\"current\":{");
    if (cur >= 0) {
      float t = numAfter(s, "\"temperature_2m\":", cur), w = numAfter(s, "\"weather_code\":", cur);
      if (t > -100) tempC = t;
      if (w > -100) weatherCode = (int)w;
      Serial.printf("weather %.1fC code %d\n", tempC, weatherCode);
    }
  }
  http.end();
}

const char *weatherWords() {
  int w = weatherCode;
  if (w < 0) return "unknown";
  if (w <= 1) return "sunny";
  if (w <= 3) return "cloudy";
  if (w <= 48) return "foggy";
  if (w <= 67 || (w >= 80 && w <= 82)) return "rainy";
  if (w <= 77 || w == 85 || w == 86) return "snowy";
  return "stormy";
}

String jsonEscape(const String &s) {
  String o;
  for (char ch : s) { if (ch == '"' || ch == '\\') o += '\\'; if (ch >= 32 && ch < 127) o += ch; }
  return o;
}

bool postGemini(const char *model, const String &body, HTTPClient &http, WiFiClientSecure &tls) {
  tls.setInsecure();  // hobby project: no certificate store on the C3
  http.setTimeout(60000);
  http.begin(tls, String("https://generativelanguage.googleapis.com/v1beta/models/") + model + ":generateContent");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-goog-api-key", GEMINI_KEY);
  int code = http.POST(body);
  if (code != 200) Serial.printf("%s: http %d\n", model, code);
  return code == 200;
}

String moodWords();
String askLine() {  // one short thing to say
  struct tm t; char when[40] = "";
  if (haveTime(t)) strftime(when, sizeof(when), "%A %H:%M", &t);
  String prompt = String("You are Pip, a tiny cheerful pet like a five-year-old who lives in a glowing LED ring on ") +
    PET_OWNER + "'s desk. It is " + when + ". The weather outside is " + weatherWords() + ", " + String(tempC, 0) +
    " degrees C. You feel " + moodWords() + ". Say ONE short thing out loud to " + PET_OWNER +
    " (max 18 words): a greeting, a fun fact, a tiny joke, something about the weather or the time of day. "
    "Only the words you say, nothing else.";
  String body = "{\"contents\":[{\"parts\":[{\"text\":\"" + jsonEscape(prompt) +
                "\"}]}],\"generationConfig\":{\"thinkingConfig\":{\"thinkingBudget\":0}}}";
  WiFiClientSecure tls; HTTPClient http;
  if (!postGemini("gemini-3.6-flash", body, http, tls)) { http.end(); return ""; }
  String s = http.getString(); http.end();
  int i = s.indexOf("\"text\": \""); if (i < 0) return "";
  String out;
  for (i += 9; i < (int)s.length() && s[i] != '"'; i++) {
    if (s[i] == '\\' && i + 1 < (int)s.length()) {
      char e = s[++i];
      if (e == 'u') { i += 4; out += '\''; } else if (e == 'n') out += ' '; else out += e;
    } else out += s[i];
  }
  out.trim();
  Serial.println("Pip says: " + out);
  return out;
}

int b64(char ch) {
  if (ch >= 'A' && ch <= 'Z') return ch - 'A';
  if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
  if (ch >= '0' && ch <= '9') return ch - '0' + 52;
  if (ch == '+') return 62;
  if (ch == '/') return 63;
  return -1;
}

bool fetchVoice(const String &line) {  // Gemini voice (24 kHz 16-bit WAV) -> /say.raw (12 kHz 8-bit, loud)
  String body = "{\"contents\":[{\"parts\":[{\"text\":\"Say it cheerfully, like a tiny excited pet: " + jsonEscape(line) +
    "\"}]}],\"generationConfig\":{\"responseModalities\":[\"AUDIO\"],\"speechConfig\":{\"voiceConfig\":"
    "{\"prebuiltVoiceConfig\":{\"voiceName\":\"Leda\"}}}}}";
  WiFiClientSecure tls; HTTPClient http;
  if (!postGemini("gemini-3.8-flash-tts", body, http, tls)) { http.end(); return false; }
  File f = LittleFS.open("/say.raw", "w");
  if (!f) { http.end(); return false; }
  WiFiClient *s = http.getStreamPtr();
  const char *key = "\"data\": \"";
  int matched = 0, quad[4], qn = 0, pairN = 0, bn = 0;
  bool inData = false, haveLo = false, finished = false;
  uint32_t bytesOut = 0, idle = millis();
  uint8_t lo = 0, buf[512], in[256];
  int32_t pairSum = 0;
  while (!finished && millis() - idle < 15000) {
    int av = s->available();
    int n = av > 0 ? s->readBytes(in, min(256, av)) : 0;
    if (n <= 0) { if (!http.connected()) break; delay(2); continue; }
    idle = millis();
    for (int k = 0; k < n && !finished; k++) {
      char ch = in[k];
      if (!inData) {
        matched = (ch == key[matched]) ? matched + 1 : (ch == key[0] ? 1 : 0);
        if (!key[matched]) inData = true;
        continue;
      }
      if (ch == '"') { finished = true; break; }
      int v = b64(ch); if (v < 0) continue;
      quad[qn++] = v; if (qn < 4) continue; qn = 0;
      uint8_t three[3] = {(uint8_t)(quad[0] << 2 | quad[1] >> 4), (uint8_t)(quad[1] << 4 | quad[2] >> 2), (uint8_t)(quad[2] << 6 | quad[3])};
      for (uint8_t by : three) {
        if (bytesOut++ < 44) continue;  // the WAV header
        if (!haveLo) { lo = by; haveLo = true; continue; }
        haveLo = false;
        pairSum += (int16_t)(by << 8 | lo);
        if (++pairN < 2) continue;  // 24 kHz -> 12 kHz
        int16_t smp = pairSum / 2; pairSum = 0; pairN = 0;
        buf[bn++] = loud[(smp >> 8) + 128];
        if (bn == (int)sizeof(buf)) { f.write(buf, bn); bn = 0; }
      }
    }
  }
  if (bn) f.write(buf, bn);
  uint32_t len = f.size(); f.close(); http.end();
  Serial.printf("voice saved: %u bytes\n", len);
  return len > 2000;
}

void netTask(void *) {
  uint32_t nextRun = 0;
  for (;;) {
    if (wifiConfigured() && ((int32_t)(millis() - nextRun) >= 0 || wantTalk)) {
      netBusy = true;
      bool talk = wantTalk;
      if (wifiUp()) {
        struct tm t;
        if (!haveTime(t)) {
          configTzTime(TIMEZONE, "pool.ntp.org", "time.google.com");
          for (int i = 0; i < 100 && sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED; i++) delay(100);
        }
        fetchWeather();
        if (talk && GEMINI_KEY[0]) { String line = askLine(); if (line.length() && fetchVoice(line)) talkReady = true; }
      }
      wifiDown();
      if (talk) wantTalk = false;
      nextRun = millis() + 30UL * 60 * 1000;
      netBusy = false;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
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
  // the weather, when there's no laptop
  {" sunny ",                             255, 200, 0, 0, 0, 0},
  {" rainy foggy ",                       30, 80, 255, 0, 1, 0},
  {" snowy cold ",                        180, 230, 255, 2, 3, 0},
  {" stormy ",                            140, 0, 255, 2, 3, 0},
};
int weatherLook() {
  if (weatherCode < 0) return -1;
  const char *w = tempC < 4 ? "cold" : weatherWords();
  if (!strcmp(w, "cloudy")) return -1;  // nothing special
  String key = String(" ") + w + " ";
  for (int i = 0; i < (int)(sizeof(LOOKS) / sizeof(LOOKS[0])); i++) if (strstr(LOOKS[i].names, key.c_str())) return i;
  return -1;
}
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
  int lk = screenLive() ? screenLook : (m == ASLEEP ? -1 : weatherLook());
  if (lk >= 0 && (m == HAPPY || m == BORED || m == ASLEEP)) {
    const Look &L = LOOKS[lk];
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

String moodWords() {
  switch (mood()) {
    case HUNGRY: return "hungry";
    case BORED: return "a bit bored";
    case EXCITED: return "excited";
    case ASLEEP: return "sleepy";
    case CROSS: return "grumpy";
    default: return "happy";
  }
}

// plays /say.raw with the mouth moving to the voice
void playVoice() {
  File f = LittleFS.open("/say.raw", "r");
  if (!f) return;
  ledcAttach(SPK_A, 156250, 8); ledcAttach(SPK_B, 156250, 8); ledcOutputInvert(SPK_B, true);
  uint8_t buf[256]; uint32_t t = micros();
  float r, g, b; moodColour(HAPPY, r, g, b);
  int n;
  while ((n = f.read(buf, sizeof(buf))) > 0) {
    int peak = 0;
    for (int i = 0; i < n; i++) {
      ledcWrite(SPK_A, buf[i]); ledcWrite(SPK_B, buf[i]);
      peak = max(peak, abs(buf[i] - 128));
      t += 1000000 / 12000;
      while ((int32_t)(micros() - t) < 0) {}
    }
    float o = min(1.0f, peak / 90.0f);  // mouth opens with the sound
    add(EYE_L, 255, 255, 255); add(EYE_R, 255, 255, 255);
    add(MOUTH[1], r, g, b); add(MOUTH[0], r * o, g * o, b * o); add(MOUTH[2], r * o, g * o, b * o);
    ring.setBrightness(BRIGHT); show();
  }
  f.close();
  ledcDetach(SPK_A); ledcDetach(SPK_B);
  speakerIdle();
}

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
  for (int i = 0; i < 256; i++) loud[i] = (uint8_t)(127.5f + 127 * tanhf(4 * (i - 128) / 128.0f) / tanhf(4));
  LittleFS.begin(true);
  xTaskCreate(netTask, "net", 16384, NULL, 1, NULL);
}

void loop() {
  readSerial();
  if (talkReady) { talkReady = false; playVoice(); lastTouch = millis(); }

  // talks by itself a few times a day when there's no laptop around (8:00, 12:30, 17:00, 19:30)
  static int lastTalkSlot = -1;
  struct tm tt;
  if (!screenLive() && haveTime(tt)) {
    int hm = tt.tm_hour * 60 + tt.tm_min, slot = -1;
    const int SLOTS[] = {8 * 60, 12 * 60 + 30, 17 * 60, 19 * 60 + 30};
    for (int i = 0; i < 4; i++) if (hm == SLOTS[i]) slot = i;
    if (slot >= 0 && slot != lastTalkSlot) { lastTalkSlot = slot; wantTalk = true; }
  }
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
    if (held3) {  // laptop there: it looks at the screen; no laptop: it says something by itself
      if (screenLive()) Serial.println("ASK"); else wantTalk = true;
      excitedUntil = now + 3000; slide(900, 2400, 200);
    }
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
