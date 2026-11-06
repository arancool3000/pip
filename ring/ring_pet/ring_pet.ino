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
//  Plugged into the Mac (mac/run.sh): it uses the Mac's internet for the time, weather and its voice.
//  Away from the Mac: with Wi-Fi in secrets.h it shows the weather (sunny / rainy / snowy / stormy / cold) and, with a
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

// ---------- animation state (the animations themselves are further down) ----------
enum Anim { A_NONE, A_YAWN, A_SNEEZE, A_WIGGLE, A_NOD, A_SHIVER, A_STRETCH, A_RAINBOW, A_HEART, A_CHOMP, A_THINK,
            A_LISTEN, A_PING, A_CLOCK, A_LIGHTNING, A_SPIN, A_DOUBLEBLINK, A_HOLD, A_CANCEL, A_ZZZ, A_SURPRISE,
            A_DISCO, A_CONFETTI, A_FIREWORK, A_WAVE, A_TWINKLE, A_HEARTEYES, A_KISS, A_LAUGH, A_GIGGLE, A_CRY, A_ANGRY,
            A_SCARED, A_CURIOUS, A_PEEK, A_WINK, A_EYEROLL, A_TYPING, A_CHEER, A_CLAP, A_FACEPALM, A_SHY, A_PROUD,
            A_HELLO, A_BOW, A_SLEEPYNOD, A_ALARM, A_LOWBATT, A_CHARGE, A_UNPLUG, A_LOADING, A_SUCCESS, A_ERROR,
            A_MAIL, A_CALL, A_FLASH, A_DOWNLOAD, A_AWAY, A_BACK, A_HEADBANG };
// dancing is a mode, not a one-off: it runs for the whole song, on top of the face
bool dancing = false; float bpm = 118; uint32_t danceStart = 0; int danceStyle = 0;
float beatPhase() { return fmodf((millis() - danceStart) * bpm / 60000.0f, 1); }  // 0..1 through each beat
int beatNo() { return (int)((millis() - danceStart) * bpm / 60000.0f); }
Anim anim = A_NONE; uint32_t animStart = 0, animLen = 0; float animArg = 0;
void startAnim(Anim a, uint32_t ms, float arg = 0) { anim = a; animStart = millis(); animLen = ms; animArg = arg; }
bool animating() { return anim != A_NONE && millis() - animStart < animLen; }
float animF() { return animating() ? (millis() - animStart) / (float)animLen : 1; }


// ---------- sound ----------
void speakerIdle() {
  pinMode(SPK_A, OUTPUT); pinMode(SPK_B, OUTPUT);
  digitalWrite(SPK_A, LOW); digitalWrite(SPK_B, LOW);
}
bool soundOn = true;  // the Mac's menu bar can switch Pip's sounds off ("S sound 0")
void beep(int hz, int ms) {
  if (!soundOn) { delay(ms); return; }
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
  if (!soundOn) return;
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
                "\"}]}]}";  // no thinkingBudget: 3.5 Flash Lite rejects 0
  WiFiClientSecure tls; HTTPClient http;
  // Flash Lites have far more free requests a day; the second one is the fallback when the first is busy
  if (!postGemini("gemini-3.5-flash-lite", body, http, tls)) {
    http.end();
    if (!postGemini("gemini-3.1-flash-lite", body, http, tls)) { http.end(); return ""; }
  }
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

// (on its own Wi-Fi it uses the TTS model: only ~20 a day, but it talks 4 times a day. Through the Mac it uses Live.)
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
int readWheel();
bool asleepNow();
extern uint32_t lastTouch;

void readSerial() {  // "T<unix time>" from settime.py, "M <mood>" from brain.py
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n'); line.trim();
  if (line.startsWith("T")) { setClock((time_t)line.substring(1).toInt()); Serial.println("time set from Mac"); return; }
  if (line.startsWith("W ")) {  // "W <code> <temp>": the weather, from the Mac's internet
    int sp = line.indexOf(' ', 2);
    weatherCode = line.substring(2, sp).toInt(); tempC = line.substring(sp + 1).toFloat();
    return;
  }
  if (line.startsWith("S sound ")) { soundOn = line.endsWith("1"); return; }
  if (line.startsWith("E ")) {  // something you did on the Mac
    String e = line.substring(2); int sp = e.indexOf(' '); String arg = sp > 0 ? e.substring(sp + 1) : ""; if (sp > 0) e = e.substring(0, sp);
    lastTouch = millis();
    if (e == "song_start") { dancing = true; bpm = arg.length() ? arg.toFloat() : 118; danceStart = millis(); animArg = random(5); startAnim(A_CHEER, 900); if (!asleepNow()) chirpHappy(); }
    else if (e == "song_stop") { dancing = false; startAnim(A_BOW, 1000); }
    else if (e == "typing") startAnim(A_TYPING, 4000);
    else if (e == "typing_stop") { if (anim == A_TYPING) anim = A_NONE; }
    else if (e == "app_switch") startAnim(A_CURIOUS, 900, random(2) ? 1 : -1);
    else if (e == "charging") { startAnim(A_CHARGE, 1600); if (!asleepNow()) slide(800, 1800, 150); }
    else if (e == "unplugged") startAnim(A_UNPLUG, 900);
    else if (e == "low_batt") { startAnim(A_LOWBATT, 2500); if (!asleepNow()) chirpSad(); }
    else if (e == "away") startAnim(A_AWAY, 2500);
    else if (e == "back") { startAnim(A_BACK, 1200); if (!asleepNow()) chirpHappy(); }
    else if (e == "screenshot") startAnim(A_FLASH, 300);
    else if (e == "download") startAnim(A_DOWNLOAD, 1200);
    else if (e == "success") { startAnim(A_SUCCESS, 1200); if (!asleepNow()) chirpHappy(); }
    else if (e == "error") { startAnim(A_ERROR, 1400); if (!asleepNow()) chirpSad(); }
    else if (e == "mail") startAnim(A_MAIL, 900);
    else if (e == "call") startAnim(A_CALL, 1600);
    else if (e == "celebrate") { startAnim(A_CONFETTI, 2000); if (!asleepNow()) chirpHappy(); }
    else if (e == "laugh") startAnim(A_LAUGH, 1500);
    else if (e == "love") startAnim(A_HEARTEYES, 2000);
    else if (e == "worried") startAnim(A_CRY, 1800);
    else if (e == "angry") startAnim(A_ANGRY, 1500);
    else if (e == "scared") startAnim(A_SCARED, 1200);
    else if (e == "dizzy") startAnim(A_EYEROLL, 1200);
    else if (e == "facepalm") startAnim(A_FACEPALM, 1500);
    else if (e == "clap") startAnim(A_CLAP, 1200);
    else if (e == "firework") startAnim(A_FIREWORK, 1000, random(100) / 100.0f);
    else if (e == "disco") startAnim(A_DISCO, 2000);
    else if (e == "hello") startAnim(A_HELLO, 1300);
    else if (e == "alarm") startAnim(A_ALARM, 1500);
    return;
  }
  if (line.startsWith("A ")) {  // what the Mac side is doing: listening (fn held), thinking, done
    String s = line.substring(2);
    if (s == "listen") startAnim(A_LISTEN, 60000);
    else if (s == "think") startAnim(A_THINK, 60000, 12);
    else if (s == "done") { if (anim == A_THINK || anim == A_LISTEN) { anim = A_NONE; startAnim(A_PING, 500); } }
    else if (s == "quiet") { if (anim == A_THINK || anim == A_LISTEN) anim = A_NONE; }
    return;
  }
  if (line.startsWith("V ")) {  // "V <bytes>" then that many bytes of 12 kHz 8-bit voice
    int len = line.substring(2).toInt();
    File f = LittleFS.open("/say.raw", "w");
    uint8_t buf[512]; int got = 0;
    while (got < len) {
      int n = Serial.readBytes(buf, min((int)sizeof(buf), len - got));
      if (n <= 0) break;  // the Mac stopped sending (timeout)
      if (f) f.write(buf, n);
      got += n;
    }
    if (f) f.close();
    Serial.printf("voice from Mac: %d bytes\n", got);
    if (got == len) talkReady = true;
    return;
  }
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
uint32_t lastTouch = 0; uint32_t lastCall = 0, excitedUntil = 0, crossUntil = 0;
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

// ---------- the drawing engine ----------
// Every frame the face is built up in px[] (any amount of light per LED, in float), then gamma-corrected and shown.
// Animations are events with a start time and a length; drawAnim() layers them over the face at fraction f = 0..1,
// so nothing blocks: the wheel, the buttons and the Mac keep working while anything plays.
float px[N][3];
int rot = 0;  // turns the face round the ring; hold the wheel and turn to change it
uint8_t gammaT[256];
int volume = 80;  // 10..100, turn the wheel while it talks

void add(int i, float r, float g, float b) { i = ((i + rot) % N + N) % N; px[i][0] += r; px[i][1] += g; px[i][2] += b; }
void addAt(float pos, float r, float g, float b) {
  pos = fmodf(pos + N * 4, N);
  int a = (int)pos; float f = pos - a;
  add(a, r * (1 - f), g * (1 - f), b * (1 - f)); add(a + 1, r * f, g * f, b * f);
}
void addAll(float r, float g, float b) { for (int i = 0; i < N; i++) add(i, r, g, b); }
void hsv(float h, float s, float v, float &r, float &g, float &b) {  // h 0..1
  h = fmodf(h + 10, 1) * 6; int i = (int)h; float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  switch (i) { case 0: r = v; g = t; b = p; break; case 1: r = q; g = v; b = p; break; case 2: r = p; g = v; b = t; break;
               case 3: r = p; g = q; b = v; break; case 4: r = t; g = p; b = v; break; default: r = v; g = p; b = q; }
  r *= 255; g *= 255; b *= 255;
}
float easeInOut(float x) { return x < 0.5f ? 2 * x * x : 1 - powf(-2 * x + 2, 2) / 2; }
float easeOut(float x) { return 1 - powf(1 - x, 3); }
float bounce(float x) { return sinf(x * 3.14159f); }  // 0 -> 1 -> 0
float lastBright = BRIGHT;
void show(float level = BRIGHT) {
  lastBright += (level - lastBright) * 0.1f;  // brightness never jumps
  ring.setBrightness((int)lastBright);
  for (int i = 0; i < N; i++)
    ring.setPixelColor(i, gammaT[min(255, (int)px[i][0])], gammaT[min(255, (int)px[i][1])], gammaT[min(255, (int)px[i][2])]);
  ring.show(); memset(px, 0, sizeof(px));
}

// ---------- animations ----------
bool faceHidden() {  // these animations take the whole ring
  return animating() && (anim == A_RAINBOW || anim == A_CLOCK || anim == A_SPIN || anim == A_THINK || anim == A_HOLD || anim == A_CANCEL
                         || anim == A_DISCO || anim == A_FIREWORK || anim == A_WAVE || anim == A_CHEER || anim == A_ALARM
                         || anim == A_CHARGE || anim == A_UNPLUG || anim == A_LOADING || anim == A_SUCCESS || anim == A_ERROR
                         || anim == A_FLASH || anim == A_DOWNLOAD || anim == A_CONFETTI);
}

void drawEyes(float e, float l, float r_, float w) {  // e open 0..1, l/r per-eye brightness, w white level
  add(EYE_L, w * e * l, w * e * l, w * e * l);
  add(EYE_R, w * e * r_, w * e * r_, w * e * r_);
}

// the weather lives in the background, faint, behind the face
void drawWeather(uint32_t now) {
  if (weatherCode < 0 || screenLive()) return;
  const char *w = weatherWords();
  if (!strcmp(w, "rainy")) {        // drops running down both sides
    for (int k = 0; k < 2; k++) { float p = fmodf((now / 900.0f) + k * 0.5f, 1); addAt(EYE_L + 1 + p * 2, 0, 20 * (1 - p), 60 * (1 - p)); addAt(EYE_R - 1 - p * 2, 0, 20 * (1 - p), 60 * (1 - p)); }
  } else if (!strcmp(w, "snowy")) { // slow sparkles drifting
    for (int k = 0; k < 3; k++) { float p = fmodf(now / 2200.0f + k * 0.33f, 1); float v = bounce(p) * 50; addAt(k * 2.7f + p * 1.5f, v, v, v + 20); }
  } else if (!strcmp(w, "sunny")) { // warm glow breathing at the top
    float v = 18 + 10 * sinf(now / 1300.0f); add(0, v, v * 0.7f, 0); add(1, v * 0.5f, v * 0.35f, 0); add(7, v * 0.5f, v * 0.35f, 0);
  } else if (!strcmp(w, "stormy")) {
    addAll(6, 0, 12);
    if (random(1200) == 0 && !animating()) startAnim(A_LIGHTNING, 350);
  }
  if (tempC < 4 && random(900) == 0 && !animating()) startAnim(A_SHIVER, 700);
}

void drawAnim(uint32_t now, float r, float g, float b) {
  if (!animating()) { if (anim != A_NONE) anim = A_NONE; return; }
  float f = animF();
  switch (anim) {
    case A_YAWN: {  // mouth opens wide and slow, eyes close
      float o = bounce(easeInOut(f));
      for (int i = 0; i < 3; i++) add(MOUTH[i], r * o, g * o, b * o);
      add(2, r * o * 0.4f, g * o * 0.4f, b * o * 0.4f); add(6, r * o * 0.4f, g * o * 0.4f, b * o * 0.4f);
      break; }
    case A_SNEEZE: {  // builds up... then everything flashes out
      if (f < 0.7f) { float v = f / 0.7f; drawEyes(1 - v * 0.8f, 1, 1, 255); add(MOUTH[1], r * v, g * v, b * v); }
      else { float v = 1 - (f - 0.7f) / 0.3f; addAll(255 * v, 255 * v, 255 * v); }
      break; }
    case A_WIGGLE: {  // the whole face shakes side to side
      float s = sinf(f * 25) * (1 - f) * 0.7f;
      addAt(EYE_L + s, 255, 255, 255); addAt(EYE_R + s, 255, 255, 255);
      for (int i = 0; i < 3; i++) addAt(MOUTH[i] + s, r, g, b);
      break; }
    case A_NOD: {  // eyes dip down twice
      float d = fabsf(sinf(f * 6.28f)) * 0.8f;
      addAt(EYE_L - d, 255, 255, 255); addAt(EYE_R + d, 255, 255, 255);
      for (int i = 0; i < 3; i++) add(MOUTH[i], r, g, b);
      break; }
    case A_SHIVER: {  // quick cold tremble, blue tinge
      float s = sinf(f * 60) * 0.3f;
      addAt(EYE_L + s, 200, 220, 255); addAt(EYE_R + s, 200, 220, 255);
      add(MOUTH[1], 120, 160, 255); addAll(0, 0, 15);
      break; }
    case A_STRETCH: {  // light spreads out from the mouth to the eyes and back
      float o = bounce(easeInOut(f));
      for (int i = 0; i < 3; i++) add(MOUTH[i], r * 0.6f, g * 0.6f, b * 0.6f);
      addAt(MOUTH[0] - o * 2, r * o, g * o, b * o); addAt(MOUTH[2] + o * 2, r * o, g * o, b * o);
      drawEyes(1, 1, 1, 255 * (0.3f + 0.7f * (1 - o)));
      break; }
    case A_RAINBOW: {  // the ring spins through every colour
      for (int i = 0; i < N; i++) { float rr, gg, bb; hsv(f * 2 + i / (float)N, 1, 1, rr, gg, bb); add(i, rr, gg, bb); }
      break; }
    case A_HEART: {  // a pink heartbeat: two thumps per cycle
      float ph = fmodf(f * animArg, 1), v = ph < 0.15f ? bounce(ph / 0.15f) : ph < 0.4f ? bounce((ph - 0.25f) / 0.15f) * 0.7f : 0;
      if (ph > 0.25f && ph < 0.4f) v = bounce((ph - 0.25f) / 0.15f) * 0.7f; else if (ph >= 0.15f) v = 0;
      addAll(255 * v * 0.5f, 40 * v, 120 * v * 0.5f);
      break; }
    case A_CHOMP: {  // eating: mouth corners and middle alternate, orange crumbs
      int k = (int)(f * 6) % 2;
      add(MOUTH[0], 255 * k, 180 * k, 0); add(MOUTH[2], 255 * k, 180 * k, 0); add(MOUTH[1], 255 * !k, 180 * !k, 0);
      drawEyes(0.6f + 0.4f * k, 1, 1, 255);
      break; }
    case A_THINK: {  // three dots chase round while the Mac is thinking
      for (int k = 0; k < 3; k++) { float p = f * animArg + k * 0.8f; addAt(p, 60 / (k + 1), 160 / (k + 1), 255 / (k + 1)); }
      break; }
    case A_LISTEN: {  // soft cyan breathing, eyes wide and steady: it's listening to you
      float v = 0.5f + 0.5f * sinf(now / 250.0f);
      drawEyes(1, 1, 1, 255); addAll(0, 25 * v, 35 * v); add(MOUTH[1], 0, 120, 160);
      break; }
    case A_PING: {  // one bright ripple out from the top
      float d = easeOut(f) * 4;
      addAt(0 + d, 255 * (1 - f), 255 * (1 - f), 255 * (1 - f)); addAt(0 - d, 255 * (1 - f), 255 * (1 - f), 255 * (1 - f));
      break; }
    case A_CLOCK: {  // telling the time: blue hour hand, green minute hand sweep into place, then hold
      struct tm t; if (!haveTime(t)) break;
      float sweep = easeOut(min(1.0f, f * 3));
      float h = ((t.tm_hour % 12) + t.tm_min / 60.0f) * N / 12, m = t.tm_min * N / 60;
      for (int i = 0; i < N; i++) add(i, 4, 4, 6);
      addAt(h * sweep, 0, 0, 255); addAt(m * sweep, 0, 255, 0); add(0, 60, 60, 60);
      break; }
    case A_LIGHTNING: {  // two flashes then dark
      float v = f < 0.1f ? 1 : f < 0.25f ? 0 : f < 0.32f ? 0.8f : 0;
      addAll(255 * v, 255 * v, 255 * v);
      break; }
    case A_SPIN: {  // a comet in the given hue
      float rr, gg, bb; hsv(animArg, 1, 1, rr, gg, bb);
      float p = f * N * 3;
      for (int k = 0; k < 4; k++) addAt(p - k * 0.6f, rr / (k + 1), gg / (k + 1), bb / (k + 1));
      break; }
    case A_DOUBLEBLINK: {
      bool open = fmodf(f * 2, 1) > 0.45f;
      drawEyes(open ? 1 : 0.08f, 1, 1, 255); for (int i = 0; i < 3; i++) add(MOUTH[i], r, g, b);
      break; }
    case A_HOLD: {  // the ring fills up while you hold a button, so you know when to let go
      float fill = f * N;
      for (int i = 0; i < N; i++) { float v = constrain(fill - i, 0, 1); add(i, 255 * v * (animArg ? 1 : 0.3f), 200 * v, 255 * v * 0.3f); }
      break; }
    case A_CANCEL: {  // a quick red wipe: stopped
      float v = 1 - f; addAll(255 * v, 30 * v, 30 * v);
      break; }
    case A_ZZZ: {  // asleep: a Z drifts up from the mouth over the right eye and fades
      float p = easeOut(f) * 3, v = 1 - f;
      addAt(MOUTH[2] + p, 30 * v, 30 * v, 90 * v);
      break; }
    case A_SURPRISE: {  // eyes pop, mouth small, brief
      drawEyes(1, 1.3f, 1.3f, 255); add(MOUTH[1], r, g, b);
      break; }
    case A_DISCO: {  // random LEDs flash in random colours
      for (int i = 0; i < N; i++) if (random(3) == 0) { float rr, gg, bb; hsv(random(100) / 100.0f, 1, 1, rr, gg, bb); add(i, rr, gg, bb); }
      break; }
    case A_CONFETTI: {  // coloured bits popping everywhere, thinning out
      for (int i = 0; i < N; i++) if (random(100) < 40 * (1 - f)) { float rr, gg, bb; hsv((i * 37 + beatNo()) % 100 / 100.0f, 1, 1, rr, gg, bb); add(i, rr, gg, bb); }
      break; }
    case A_FIREWORK: {  // a burst from the top, spreading both ways and fading
      float d = easeOut(f) * N / 2, v = 1 - f; float rr, gg, bb; hsv(animArg, 0.8f, 1, rr, gg, bb);
      addAt(0 + d, rr * v, gg * v, bb * v); addAt(0 - d, rr * v, gg * v, bb * v);
      if (f < 0.15f) add(0, 255, 255, 255);
      break; }
    case A_WAVE: {  // a smooth wave of the mood colour going round twice
      for (int i = 0; i < N; i++) { float v = 0.5f + 0.5f * sinf((i / (float)N - f * 2) * 6.28f); add(i, r * v, g * v, b * v); }
      break; }
    case A_TWINKLE: {  // soft white sparkles, like stars
      for (int i = 0; i < N; i++) { float v = powf(0.5f + 0.5f * sinf(now / 150.0f + i * 1.7f), 6) * 120; add(i, v, v, v); }
      drawEyes(1, 1, 1, 255); for (int i = 0; i < 3; i++) add(MOUTH[i], r * 0.7f, g * 0.7f, b * 0.7f);
      break; }
    case A_HEARTEYES: {  // eyes go pink and pulse
      float v = 0.6f + 0.4f * sinf(now / 120.0f);
      add(EYE_L, 255 * v, 40 * v, 120 * v); add(EYE_R, 255 * v, 40 * v, 120 * v);
      for (int i = 0; i < 3; i++) add(MOUTH[i], 255, 60, 140);
      break; }
    case A_KISS: {  // mouth puckers to one LED, then a pink dot floats up and away
      if (f < 0.4f) { add(MOUTH[1], 255, 80, 160); drawEyes(0.3f, 1, 1, 255); }
      else { float p = (f - 0.4f) / 0.6f; addAt(MOUTH[1] + p * 4, 255 * (1 - p), 80 * (1 - p), 160 * (1 - p)); drawEyes(1, 1, 1, 255); }
      break; }
    case A_LAUGH: {  // the whole face bounces fast, mouth wide open and shut
      float bob = fabsf(sinf(f * 40)) * 0.6f; bool open = fmodf(f * 8, 1) < 0.5f;
      addAt(EYE_L + bob, 255, 255, 255); addAt(EYE_R - bob, 255, 255, 255);
      for (int i = 0; i < 3; i++) if (open || i == 1) addAt(MOUTH[i] + bob * (i - 1), r, g, b);
      add(2, r * 0.3f, g * 0.3f, b * 0.3f); add(6, r * 0.3f, g * 0.3f, b * 0.3f);
      break; }
    case A_GIGGLE: {  // tiny quick shakes, eyes squeezed
      float s = sinf(f * 50) * 0.25f;
      addAt(EYE_L + s, 255 * 0.4f, 255 * 0.4f, 255 * 0.4f); addAt(EYE_R + s, 255 * 0.4f, 255 * 0.4f, 255 * 0.4f);
      for (int i = 0; i < 3; i++) addAt(MOUTH[i] + s, r, g, b);
      break; }
    case A_CRY: {  // blue tears run from the eyes down the sides, mouth wobbles
      float p = fmodf(f * 3, 1);
      addAt(EYE_L - 1 - p * 2, 0, 60 * (1 - p), 255 * (1 - p)); addAt(EYE_R + 1 + p * 2, 0, 60 * (1 - p), 255 * (1 - p));
      drawEyes(0.5f, 1, 1, 200); float w = 0.5f + 0.5f * sinf(now / 90.0f); add(MOUTH[0], 60 * w, 60 * w, 255 * w); add(MOUTH[2], 60 * (1 - w), 60 * (1 - w), 255 * (1 - w));
      break; }
    case A_ANGRY: {  // red pulses, narrowed eyes, mouth a hard line
      float v = 0.5f + 0.5f * sinf(now / 110.0f);
      addAll(60 * v, 0, 0); drawEyes(0.5f, 1, 1, 255); add(MOUTH[1], 255, 0, 0);
      break; }
    case A_SCARED: {  // dim, shaking, eyes huge
      float s = sinf(f * 70) * 0.2f;
      addAt(EYE_L + s, 255, 255, 255); addAt(EYE_R + s, 255, 255, 255); add(MOUTH[1], r * 0.4f, g * 0.4f, b * 0.4f);
      break; }
    case A_CURIOUS: {  // head tilt: the face slides a little to one side and one eye brightens
      float t_ = easeInOut(min(1.0f, f * 2)) * 0.6f * (animArg >= 0 ? 1 : -1);
      addAt(EYE_L + t_, 255, 255, 255); addAt(EYE_R + t_, 160, 160, 160);
      for (int i = 0; i < 3; i++) addAt(MOUTH[i] + t_, r, g, b);
      break; }
    case A_PEEK: {  // eyes shut... one opens slowly... then both
      float a = f < 0.4f ? 0.08f : f < 0.8f ? easeOut((f - 0.4f) / 0.4f) : 1, b_ = f < 0.8f ? 0.08f : easeOut((f - 0.8f) / 0.2f);
      add(EYE_L, 255 * a, 255 * a, 255 * a); add(EYE_R, 255 * b_, 255 * b_, 255 * b_); add(MOUTH[1], r, g, b);
      break; }
    case A_WINK: {
      drawEyes(1, f < 0.6f ? 0.08f : 1, 1, 255); for (int i = 0; i < 3; i++) add(MOUTH[i], r, g, b);
      break; }
    case A_EYEROLL: {  // both eyes travel up over the top and back
      float p = sinf(f * 3.14159f) * 1.2f;
      addAt(EYE_L + p, 255, 255, 255); addAt(EYE_R - p, 255, 255, 255); add(MOUTH[1], r, g, b);
      break; }
    case A_TYPING: {  // typing along with you: little dots pop at the mouth in a quick rhythm
      int k = (int)(now / 90) % 3; add(MOUTH[k], r, g, b); add(MOUTH[(k + 1) % 3], r * 0.3f, g * 0.3f, b * 0.3f);
      drawEyes(0.8f, 1, 1, 255);
      break; }
    case A_CHEER: {  // the ring pumps three times through the rainbow
      float v = powf(bounce(fmodf(f * 3, 1)), 0.6f);
      for (int i = 0; i < N; i++) { float rr, gg, bb; hsv(i / (float)N + f, 1, 1, rr, gg, bb); add(i, rr * v, gg * v, bb * v); }
      break; }
    case A_CLAP: {  // left and right halves flash alternately
      bool l = fmodf(f * 6, 1) < 0.5f;
      for (int i = 0; i < N; i++) { bool left = ((i + rot) % N) >= N / 2; if (left == l) add(i, 255, 220, 120); }
      break; }
    case A_FACEPALM: {  // eyes close, everything dims, a slow sigh
      float v = 1 - bounce(f) * 0.7f;
      drawEyes(0.08f, 1, 1, 255); add(MOUTH[1], r * v, g * v, b * v);
      break; }
    case A_SHY: {  // dims, pink cheeks pulse, eyes look down
      float v = 0.5f + 0.5f * sinf(now / 300.0f);
      drawEyes(0.6f, 1, 1, 180); add(2, 255 * v * 0.6f, 60 * v, 100 * v * 0.6f); add(6, 255 * v * 0.6f, 60 * v, 100 * v * 0.6f); add(MOUTH[1], r, g, b);
      break; }
    case A_PROUD: {  // a glow swells out from the mouth, chin up
      float o = bounce(easeInOut(f));
      drawEyes(1, 1, 1, 255); for (int i = 0; i < 3; i++) add(MOUTH[i], r, g, b);
      addAt(MOUTH[0] - o * 1.5f, r * o * 0.6f, g * o * 0.6f, b * o * 0.6f); addAt(MOUTH[2] + o * 1.5f, r * o * 0.6f, g * o * 0.6f, b * o * 0.6f);
      break; }
    case A_HELLO: {  // one eye waves: it sweeps out and back three times like a hand
      float w = sinf(f * 18.85f) * 1.2f;
      add(EYE_L, 255, 255, 255); addAt(EYE_R + w, 255, 255, 255); for (int i = 0; i < 3; i++) add(MOUTH[i], r, g, b);
      break; }
    case A_BOW: {  // the face dips down toward the mouth and comes back
      float d = bounce(easeInOut(f)) * 1.5f;
      addAt(EYE_L - d, 255, 255, 255); addAt(EYE_R + d, 255, 255, 255); add(MOUTH[1], r, g, b);
      break; }
    case A_SLEEPYNOD: {  // eyes droop slowly... and jerk open
      float e = f < 0.8f ? 1 - easeInOut(f / 0.8f) * 0.9f : 1;
      drawEyes(e, 1, 1, 255); add(MOUTH[1], r * 0.6f, g * 0.6f, b * 0.6f);
      break; }
    case A_ALARM: { float v = fmodf(f * 8, 1) < 0.5f; addAll(255 * v, 0, 0); break; }
    case A_LOWBATT: {  // one red LED at the bottom slowly fading
      float v = 0.3f + 0.7f * (1 - f); add(MOUTH[1], 255 * v, 0, 0); drawEyes(0.6f, 1, 1, 150);
      break; }
    case A_CHARGE: {  // green fills up from the bottom, twice
      float fill = fmodf(f * 2, 1) * N;
      for (int i = 0; i < N; i++) { float v = constrain(fill - i, 0, 1); add(MOUTH[1] + i, 0, 255 * v, 60 * v); }
      break; }
    case A_UNPLUG: {  // green drains away
      float fill = (1 - f) * N;
      for (int i = 0; i < N; i++) { float v = constrain(fill - i, 0, 1); add(MOUTH[1] + i, 0, 200 * v, 40 * v); }
      break; }
    case A_LOADING: { float p = f * N * 4; for (int k = 0; k < 3; k++) addAt(p - k, 90 / (k + 1), 90 / (k + 1), 110 / (k + 1)); break; }
    case A_SUCCESS: {  // a green sweep round, then a bright flash
      float p = min(1.0f, f * 1.4f) * N;
      for (int i = 0; i < N; i++) if (i < p) add(i, 0, 255, 80);
      if (f > 0.75f) addAll(80 * (1 - f) * 4, 255 * (1 - f) * 4, 120 * (1 - f) * 4);
      break; }
    case A_ERROR: {  // two sharp red flashes, then a sad face
      if (f < 0.4f) { float v = fmodf(f * 5, 1) < 0.5f; addAll(255 * v, 0, 0); }
      else { drawEyes(0.6f, 1, 1, 255); add(MOUTH[0], 255, 40, 40); add(MOUTH[2], 255, 40, 40); }
      break; }
    case A_MAIL: {  // a yellow ping from the top, like a notification
      float d = easeOut(f) * 3, v = 1 - f;
      addAt(0 + d, 255 * v, 200 * v, 0); addAt(0 - d, 255 * v, 200 * v, 0); add(0, 255 * v, 220 * v, 60 * v);
      break; }
    case A_CALL: {  // ring-ring: two blue double pulses
      float v = fmodf(f * 4, 1) < 0.5f ? bounce(fmodf(f * 8, 1)) : 0; addAll(0, 80 * v, 255 * v);
      break; }
    case A_FLASH: { float v = 1 - f; addAll(255 * v, 255 * v, 255 * v); break; }
    case A_DOWNLOAD: {  // a dot drops from the top to the bottom and lands
      float p = easeInOut(f) * N / 2; addAt(0 + p, 60, 200, 255); addAt(0 - p, 60, 200, 255);
      if (f > 0.9f) add(MOUTH[1], 200, 255, 255);
      break; }
    case A_AWAY: {  // you left: it looks left, then right, then droops
      float l = f < 0.33f ? 1 : f < 0.66f ? -1 : 0, e = f < 0.66f ? 1 : 1 - (f - 0.66f) * 2;
      drawEyes(e, 1 - max(0.0f, l) * 0.6f, 1 + min(0.0f, l) * 0.6f, 255); add(MOUTH[1], r * e, g * e, b * e);
      break; }
    case A_BACK: {  // you're back: eyes pop open wide and a big smile grows
      float e = easeOut(min(1.0f, f * 2)), s = easeOut(f);
      drawEyes(e, 1.2f, 1.2f, 255); add(MOUTH[1], r, g, b); addAt(MOUTH[0] + (1 - s), r * s, g * s, b * s); addAt(MOUTH[2] - (1 - s), r * s, g * s, b * s);
      break; }
    case A_HEADBANG: {  // hard nods on the beat
      float d = bounce(beatPhase()) * 1.2f;
      addAt(EYE_L - d, 255, 255, 255); addAt(EYE_R + d, 255, 255, 255); for (int i = 0; i < 3; i++) add(MOUTH[i], r, g, b);
      break; }
    default: break;
  }
}

// little things it does by itself, picked for the mood and the weather
void idleEvents(uint32_t now) {
  static uint32_t next = 5000;
  if (now < next || animating()) return;
  next = now + 4000 + random(9000);
  Mood m = mood();
  float r, g, b; moodColour(m, r, g, b);
  int pick = random(100);
  if (m == ASLEEP) { if (pick < 50) startAnim(A_ZZZ, 2200); return; }
  if (m == BORED || food < 50) { if (pick < 35) { startAnim(A_YAWN, 1600); chirpYawn(); } else if (pick < 55) startAnim(A_STRETCH, 1400); return; }
  if (m == EXCITED) { if (pick < 60) startAnim(A_WIGGLE, 900); else startAnim(A_RAINBOW, 1200); return; }
  if (weatherCode >= 0 && tempC < 4 && pick < 25) { startAnim(A_SNEEZE, 1100); beep(2500, 30); beep(600, 120); return; }
  static const Anim cute[] = {A_DOUBLEBLINK, A_NOD, A_STRETCH, A_WIGGLE, A_WINK, A_PEEK, A_CURIOUS, A_EYEROLL, A_GIGGLE, A_TWINKLE,
                              A_SHY, A_PROUD, A_HELLO, A_BOW, A_KISS, A_HEARTEYES, A_SURPRISE};
  static const int lens[] = {500, 900, 1400, 700, 700, 1500, 1400, 900, 600, 2000, 1800, 1300, 1200, 1200, 1300, 1500, 500};
  if (pick < 55) { int i = random(sizeof(cute) / sizeof(cute[0])); startAnim(cute[i], lens[i], random(2) ? 1 : -1); }
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
  float level = m == ASLEEP ? 8 : BRIGHT;
  if (faceHidden()) { drawAnim(now, r, g, b); show(level); return; }

  if (dancing && !animating()) {  // bopping to the music: the face bounces on the beat, colours cycle, every 8 bars it changes style
    float ph = beatPhase(), bob = bounce(ph); int bar = beatNo() / 32; danceStyle = (bar + (int)animArg) % 5;
    float rr, gg, bb; hsv(fmodf(beatNo() / 16.0f, 1), 0.8f, 1, rr, gg, bb);
    float e = ph < 0.1f ? 0.5f : 1;
    switch (danceStyle) {
      case 0: { float s = bob * 0.5f * (beatNo() % 2 ? 1 : -1);  // sway side to side
        addAt(EYE_L + s, 255 * e, 255 * e, 255 * e); addAt(EYE_R + s, 255 * e, 255 * e, 255 * e); for (int i = 0; i < 3; i++) addAt(MOUTH[i] + s, rr, gg, bb); break; }
      case 1: { float d = bob * 0.8f;  // bounce up and down
        addAt(EYE_L - d, 255 * e, 255 * e, 255 * e); addAt(EYE_R + d, 255 * e, 255 * e, 255 * e); for (int i = 0; i < 3; i++) add(MOUTH[i], rr * (0.5f + 0.5f * bob), gg * (0.5f + 0.5f * bob), bb * (0.5f + 0.5f * bob)); break; }
      case 2: { drawEyes(e, 1, 1, 255); for (int i = 0; i < N; i++) { float v = 0.5f + 0.5f * sinf((i / (float)N - ph) * 6.28f); add(i, rr * v * 0.5f, gg * v * 0.5f, bb * v * 0.5f); } break; }  // a wave going round
      case 3: { drawEyes(e, 1, 1, 255); add(MOUTH[1], rr, gg, bb); if (beatNo() % 2) add(MOUTH[0], rr, gg, bb); else add(MOUTH[2], rr, gg, bb);  // mouth bops left-right
        for (int i = 0; i < N; i++) if (random(100) < 8 * bob) add(i, 120, 120, 120); break; }
      default: { float d = bounce(ph) * 1.2f;  // headbang
        addAt(EYE_L - d, 255, 255, 255); addAt(EYE_R + d, 255, 255, 255); for (int i = 0; i < 3; i++) add(MOUTH[i], rr, gg, bb); }
    }
    add(2, rr * 0.15f * bob, gg * 0.15f * bob, bb * 0.15f * bob); add(6, rr * 0.15f * bob, gg * 0.15f * bob, bb * 0.15f * bob);
    show(level); return;
  }

  drawWeather(now);
  float breathe = m == ASLEEP ? 0.5f + 0.5f * sinf(now / 1600.0f) : 0.78f + 0.22f * sinf(now / 900.0f);

  // eyes: glance around with easing, blink now and then (asleep = closed, a faint glow)
  if (eyes == 2 || m == EXCITED) { if (now > nextLook) { lookTarget = random(-10, 11) / 14.0f; nextLook = now + 200 + random(400); } }
  else if (eyes == 4) lookTarget = 0;
  else if (now > nextLook) { lookTarget = random(-10, 11) / 20.0f; nextLook = now + 1500 + random(4000); }
  lookX += (lookTarget - lookX) * (eyes == 2 ? 0.3f : 0.08f);
  if (now > nextBlink) { blinkUntil = now + 120; nextBlink = now + (eyes == 4 ? 6000 : 2500) + random(4000); }
  bool closed = eyes == 1 || now < blinkUntil;
  float e = closed ? 0.08f : eyes == 4 ? 0.7f : 1.0f;  // concentrating = slightly narrowed
  bool animEyes = animating() && anim != A_ZZZ && anim != A_HEART && anim != A_PING && anim != A_LIGHTNING && anim != A_YAWN;
  if (eyes == 3) {  // dizzy: eyes chase each other round the ring
    float p = (now % 900) * N / 900.0f;
    addAt(p, 255, 255, 255); addAt(p + N / 2, 255, 255, 255);
  } else if (!animEyes) {  // each eye is one LED; looking sideways makes one a little brighter than the other
    float l = 1 - max(0.0f, lookX) * 0.6f, rr = 1 + min(0.0f, lookX) * 0.6f;
    drawEyes(e, l, rr, 255);
  }

  // mouth in the mood colour
  bool animMouth = animating() && (anim == A_YAWN || anim == A_WIGGLE || anim == A_NOD || anim == A_SHIVER || anim == A_STRETCH
                                   || anim == A_CHOMP || anim == A_DOUBLEBLINK || anim == A_SNEEZE || anim == A_SURPRISE || anim == A_LISTEN);
  if (animMouth) {}
  else if (mouthStyle == 2) { int k = (now / 300) % 3; add(MOUTH[1], r, g, b); add(MOUTH[k == 1 ? 0 : 2], r, g, b); }
  else if (mouthStyle == 3) { float w = 0.5f + 0.5f * sinf(now / 120.0f); add(MOUTH[0], r * w, g * w, b * w); add(MOUTH[1], r, g, b); add(MOUTH[2], r * (1 - w), g * (1 - w), b * (1 - w)); }
  else if (mouthStyle == 1) add(MOUTH[1], r * breathe, g * breathe, b * breathe);
  else if (m == HAPPY || m == EXCITED) for (int i = 0; i < 3; i++) add(MOUTH[i], r * breathe, g * breathe, b * breathe);
  else if (m == HUNGRY) { float o = 0.5f + 0.5f * sinf(now / 250.0f); add(MOUTH[1], r, g, b); add(MOUTH[0], r * o, g * o, b * o); add(MOUTH[2], r * o, g * o, b * o); }
  else add(MOUTH[1], r * breathe, g * breathe, b * breathe);

  drawAnim(now, r, g, b);

  // the stroking light: follows the wheel, fades when you stop
  if (swirl >= 0) {
    float age = (now - swirlAt) / 600.0f;
    if (age > 1) swirl = -1;
    else for (int k = 0; k < 3; k++) addAt(swirl - k * 0.5f, 255 * (1 - age) / (k + 1), 200 * (1 - age) / (k + 1), 255 * (1 - age) / (k + 1));
  }
  show(level);
}

// a blocking spin, only used at start-up before the loop runs
void spinColour(float r, float g, float b, int ms) {
  uint32_t end = millis() + ms;
  while ((int32_t)(millis() - end) < 0) {
    float p = (millis() % 600) * N / 600.0f;
    for (int k = 0; k < 4; k++) addAt(p - k * 0.6f, r / (k + 1), g / (k + 1), b / (k + 1));
    show(BRIGHT); delay(15);
  }
}

void playSound(int s) {
  if (asleepNow()) return;
  if (s == 1) { chirpHappy(); startAnim(A_WIGGLE, 700); } else if (s == 2) chirpSad(); else if (s == 3) { chirpYawn(); startAnim(A_YAWN, 1500); } else if (s == 4) chirpCall();
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

// something to say: over its own Wi-Fi if it has some, otherwise the Mac fetches it (if plugged in)
void askToTalk() {
  if (wifiConfigured()) wantTalk = true;
  else Serial.println("TALK " + moodWords());
}

// plays /say.raw with the mouth moving to the voice
bool anyPress() { return digitalRead(ENC_SW) == LOW || digitalRead(BOOT) == LOW; }

void playVoice() {
  if (!soundOn) return;
  File f = LittleFS.open("/say.raw", "r");
  if (!f) return;
  while (anyPress()) delay(10);  // don't start while a finger is still on a button
  ledcAttach(SPK_A, 156250, 8); ledcAttach(SPK_B, 156250, 8); ledcOutputInvert(SPK_B, true);
  uint8_t buf[256]; uint32_t t = micros();
  float r, g, b; moodColour(HAPPY, r, g, b);
  int n; bool stopped = false;
  while (!stopped && (n = f.read(buf, sizeof(buf))) > 0) {
    int peak = 0;
    for (int i = 0; i < n; i++) {
      int s = 128 + (buf[i] - 128) * volume / 100;
      ledcWrite(SPK_A, s); ledcWrite(SPK_B, s);
      peak = max(peak, abs(buf[i] - 128));
      t += 1000000 / 12000;
      while ((int32_t)(micros() - t) < 0) {}
    }
    int turn = readWheel();  // turning the wheel while it talks = volume
    if (turn) { volume = constrain(volume + turn * 10, 10, 100); prefs.putInt("vol", volume); }
    if (anyPress()) stopped = true;  // any press: stop talking
    float o = min(1.0f, peak / 90.0f);  // mouth opens with the sound
    add(EYE_L, 255, 255, 255); add(EYE_R, 255, 255, 255);
    add(MOUTH[1], r, g, b); add(MOUTH[0], r * o, g * o, b * o); add(MOUTH[2], r * o, g * o, b * o);
    for (int i = 0; i < N; i++) add(i, 0, 0, 0);
    if (turn) { float v = volume * N / 100.0f; for (int i = 0; i < N; i++) { float k = constrain(v - i, 0, 1); add(i, 0, 60 * k, 80 * k); } }
    show(BRIGHT);
  }
  f.close();
  ledcDetach(SPK_A); ledcDetach(SPK_B);
  speakerIdle();
  if (stopped) { startAnim(A_CANCEL, 300); while (anyPress()) delay(10); }
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
  Serial.setRxBufferSize(4096);  // voice clips arrive from the Mac in a hurry
  Serial.begin(115200);
  Serial.setTimeout(500);
  speakerIdle();
  pinMode(BOOT, INPUT_PULLUP);
  pinMode(ENC_A, INPUT_PULLUP); pinMode(ENC_B, INPUT_PULLUP); pinMode(ENC_SW, INPUT_PULLUP);
  lastAB = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  randomSeed(esp_random());
  prefs.begin("pet");
  rot = prefs.getInt("rot", 0); volume = prefs.getInt("vol", 80);
  for (int i = 0; i < 256; i++) gammaT[i] = (uint8_t)(powf(i / 255.0f, 2.2f) * 255 + 0.5f); food = prefs.getFloat("food", 80); fun = prefs.getFloat("fun", 80);
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
    if (slot >= 0 && slot != lastTalkSlot) { lastTalkSlot = slot; askToTalk(); }
  }
  uint32_t now = millis();

  // BOOT: hold 3 s for the wiring; the ring fills while you hold so you can see how long is left
  static uint32_t bootDown = 0;
  if (digitalRead(BOOT) == LOW) {
    if (!bootDown) { bootDown = now; startAnim(A_HOLD, 3000, 0); }
    if (now - bootDown >= 3000) {
      anim = A_NONE; ring.clear(); ring.show();
      playRaw(WIRING, sizeof(WIRING), WIRING_RATE);
      while (digitalRead(BOOT) == LOW) delay(10);
      bootDown = 0;
    }
  } else if (bootDown) { bootDown = 0; if (anim == A_HOLD) anim = A_NONE; }

  // stroking: turn the wheel (faster turning = a bigger, brighter swirl)
  int turn = readWheel();
  static bool rotating = false;
  if (turn && digitalRead(ENC_SW) == LOW) {  // hold the wheel and turn: spin the face round until the eyes are on top
    rot = ((rot + turn) % N + N) % N; rotating = true;
    prefs.putInt("rot", rot); beep(2093, 6);
    turn = 0;
  }
  static uint32_t lastTurn = 0;
  if (turn) {
    touched();
    if (!asleepNow()) {
      float speed = constrain(300.0f / (float)max((uint32_t)1, now - lastTurn), 0.3f, 2.0f);  // clicks per 0.3 s
      lastTurn = now;
      swirl = swirl < 0 ? (turn > 0 ? EYE_R : EYE_L) : swirl + turn * 0.5f * speed;
      swirlAt = now;
      fun = min(100.0f, fun + 1.5f);
      static uint32_t lastPurr = 0;
      if (now - lastPurr > 250) { lastPurr = now; purr(); }
      static int strokes = 0; static uint32_t strokeStart = 0;
      if (now - strokeStart > 3000) { strokeStart = now; strokes = 0; }
      if (++strokes == 16) { excitedUntil = now + 4000; chirpHappy(); startAnim(A_HEART, 2400, 2); }  // a good long stroke
    }
  }

  // wheel button: tap = feed, hold 1 s (one beep) = the time, hold 3 s (two beeps) = AI looks / says something.
  // The ring fills while you hold: a quarter full at 1 s, full at 3 s.
  static bool swWas = false, held = false, held3 = false;
  static uint32_t swDown = 0;
  bool sw = digitalRead(ENC_SW) == LOW;
  if (sw && !swWas) { swDown = now; held = held3 = false; }
  if (sw && !rotating && now - swDown >= 250 && anim != A_HOLD && !held3) startAnim(A_HOLD, 2750, 1);
  if (sw && !held && now - swDown >= 1000) { held = true; beep(1319, 40); }
  if (sw && !held3 && now - swDown >= 3000) { held3 = true; beep(1760, 40); anim = A_NONE; startAnim(A_PING, 600); }
  if (!sw && swWas && rotating) { rotating = false; held = true; swWas = sw; anim = A_NONE; return; }  // that was a rotate, not a press
  if (sw && rotating) { held = held3 = true; anim = A_NONE; }
  if (!sw && swWas) { if (anim == A_HOLD) anim = A_NONE; }
  if (!sw && swWas && held) {
    touched();
    if (held3) {  // laptop there: it looks at the screen; no laptop: it says something by itself
      if (screenLive()) { Serial.println("ASK"); startAnim(A_THINK, 20000, 12); } else { askToTalk(); startAnim(A_THINK, 20000, 12); }
      slide(900, 2400, 200);
    } else { startAnim(A_CLOCK, 4000); sayTime(); }
  }
  if (!sw && swWas && !held) {
    touched();
    if (!asleepNow()) {
      if (food > 90) { chirpGrumpy(); startAnim(A_NOD, 600); }  // full up
      else {
        food = min(100.0f, food + 35);
        startAnim(A_CHOMP, 900); munch();
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
    if (m == HUNGRY || m == BORED) { chirpCall(); startAnim(A_PING, 700); } else if (random(3) == 0) { chirpHappy(); startAnim(A_WIGGLE, 700); }
  }
  static Mood lastMood = HAPPY;
  if (m == ASLEEP && lastMood != ASLEEP) { chirpYawn(); startAnim(A_YAWN, 1800); }
  if (m == EXCITED && lastMood != EXCITED) startAnim(A_RAINBOW, 1000);
  lastMood = m;

  idleEvents(now);

  // fixed 50 fps
  static uint32_t lastDraw = 0;
  if (now - lastDraw >= 20) { lastDraw = now; drawPet(); }
  delay(1);
}
