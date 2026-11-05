// DeskPet face engine for the board: a line-for-line port of face.js, so the simulator shows what this draws.
// id = eye*48 + mouth*4 + extra  (16 eyes x 12 mouths x 4 extras = 768 faces)
#pragma once
#include <Arduino_GFX_Library.h>
#include <math.h>

static const int FW = 172, FH = 320;

static uint16_t rgb(uint32_t c) { return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F); }
static uint32_t mix24(uint32_t a, uint32_t b, float t) {
  uint32_t out = 0;
  for (int s = 16; s >= 0; s -= 8) {
    int x = (a >> s) & 0xFF, y = (b >> s) & 0xFF;
    out |= (uint32_t)(x + (y - x) * t + 0.5f) << s;
  }
  return out;
}

static const uint32_t INK24 = 0x2b2235, WHITE24 = 0xffffff, PINK24 = 0xff7aa8, BLUE24 = 0x59b7ff, YEL24 = 0xffd34d;

struct FaceState {
  int id = 576 + 1;  // wink, smile, blush
  uint32_t bg = 0xffe9a8;
  int deco = 0, anim = 0;
  char caption[24] = "hi!";
  float lx = 0, ly = 0;
};

class Face {
 public:
  explicit Face(Arduino_GFX *g) : g(g) {}

  void draw(const FaceState &st, float t) {
    int eye = (st.id / 48) % 16, mouth = (st.id / 4) % 12, extra = st.id % 4;
    bg24 = st.bg; bg = rgb(st.bg);
    g->fillScreen(bg);
    dy = sinf(t * 2) * 2; dx = 0;                          // breathing
    if (st.anim == 1) dy = -fabsf(sinf(t * 6)) * 10;      // bounce
    if (st.anim == 2) dx = sinf(t * 18) * 3;              // shiver
    bool open = eye == 0 || eye == 3 || eye == 4 || (eye >= 9 && eye <= 11) || eye == 13 || eye == 14 || eye == 15;
    bool blink = open && fmodf(t, 4.3f) < 0.13f;
    drawEye(blink ? 2 : eye, 52, 140, -1, st.lx, st.ly);
    drawEye(blink ? 2 : eye, 120, 140, 1, st.lx, st.ly);
    drawMouth(mouth);
    drawExtra(extra);
    dx = dy = 0;  // decorations and caption don't move with the face
    drawDeco(st.deco, t);
    if (st.caption[0]) {
      String s = String(st.caption).substring(0, 13);
      int w = s.length() * 12 + 16;
      rrect(86 - w / 2, 276, w, 26, 13, rgb(mix24(bg24, INK24, 0.82f)));
      text(s.c_str(), 86, 290, 2, rgb(WHITE24));
    }
  }

 private:
  Arduino_GFX *g;
  uint16_t bg; uint32_t bg24;
  float dx = 0, dy = 0;
  const uint16_t INK = rgb(INK24), WHITE = rgb(WHITE24), PINK = rgb(PINK24), BLUE = rgb(BLUE24), YEL = rgb(YEL24);

  // primitives (shifted by the face's breathing/bounce offset), same as canvasGfx in face.js
  void circle(float x, float y, float r, uint16_t c) { g->fillCircle(x + dx, y + dy, r, c); }
  void ellipse(float x, float y, float rx, float ry, uint16_t c) { g->fillEllipse(x + dx, y + dy, rx, ry, c); }
  void rrect(float x, float y, float w, float h, float r, uint16_t c) {
    if (r <= 0) g->fillRect(x + dx, y + dy, w, h, c); else g->fillRoundRect(x + dx, y + dy, w, h, r, c);
  }
  void tri(float x1, float y1, float x2, float y2, float x3, float y3, uint16_t c) {
    g->fillTriangle(x1 + dx, y1 + dy, x2 + dx, y2 + dy, x3 + dx, y3 + dy, c);
  }
  void line(float x1, float y1, float x2, float y2, float w, uint16_t c) {  // round-capped thick line
    float r = w / 2, vx = x2 - x1, vy = y2 - y1, len = sqrtf(vx * vx + vy * vy);
    circle(x1, y1, r, c); circle(x2, y2, r, c);
    if (len < 0.5f) return;
    float nx = -vy / len * r, ny = vx / len * r;
    tri(x1 + nx, y1 + ny, x1 - nx, y1 - ny, x2 + nx, y2 + ny, c);
    tri(x1 - nx, y1 - ny, x2 - nx, y2 - ny, x2 + nx, y2 + ny, c);
  }
  void arc(float cx, float cy, float rx, float ry, float a0, float a1, float w, uint16_t c) {
    const int n = 14;
    for (int i = 0; i < n; i++) {
      float t0 = (a0 + (a1 - a0) * i / n) * PI / 180, t1 = (a0 + (a1 - a0) * (i + 1) / n) * PI / 180;
      line(cx + rx * cosf(t0), cy + ry * sinf(t0), cx + rx * cosf(t1), cy + ry * sinf(t1), w, c);
    }
  }
  void text(const char *s, float x, float y, int size, uint16_t c) {
    g->setTextSize(size); g->setTextColor(c);
    g->setCursor(x + dx - strlen(s) * 3 * size, y + dy - 4 * size);
    g->print(s);
  }
  void heart(float x, float y, float s, uint16_t c) {
    circle(x - s * 0.45f, y - s * 0.2f, s * 0.5f, c); circle(x + s * 0.45f, y - s * 0.2f, s * 0.5f, c);
    tri(x - s * 0.93f, y, x + s * 0.93f, y, x, y + s * 0.95f, c);
  }
  void star(float x, float y, float r, uint16_t c) {
    float px[10], py[10];
    for (int i = 0; i < 10; i++) {
      float a = (-90 + i * 36) * PI / 180, rr = i % 2 ? r * 0.45f : r;
      px[i] = x + rr * cosf(a); py[i] = y + rr * sinf(a);
    }
    for (int i = 0; i < 10; i++) tri(x, y, px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], c);
  }
  void drop(float x, float y, float r, uint16_t c) { circle(x, y, r, c); tri(x - r * 0.9f, y - r * 0.3f, x + r * 0.9f, y - r * 0.3f, x, y - r * 2.2f, c); }

  void drawEye(int s, float x, float y, int side, float lx, float ly) {
    float ex = x + lx, ey = y + ly;
    switch (s) {
      case 0: ellipse(ex, ey, 14, 19, INK); circle(ex - 5, ey - 7, 5, WHITE); circle(ex + 4, ey + 6, 2, WHITE); break;
      case 1: arc(x, y + 8, 15, 15, 180, 360, 6, INK); break;
      case 2: line(x - 14, y, x + 14, y, 6, INK); break;
      case 3: circle(x, y, 21, INK); circle(x, y, 18, WHITE); circle(x + lx * 1.4f, y + ly * 1.4f, 9, INK); circle(x + lx * 1.4f - 3, y + ly * 1.4f - 3, 3, WHITE); break;
      case 4: ellipse(ex, ey, 14, 19, INK); rrect(x - 24, y - 28, 48, 27, 0, bg); line(x - 16, y - 1, x + 16, y - 1, 5, INK); break;
      case 5: heart(x, y, 20, PINK); circle(x - 8, y - 8, 3, WHITE); break;
      case 6: star(x, y, 22, YEL); circle(x, y, 4, WHITE); break;
      case 7: {
        float px = x, py = y;
        for (int i = 1; i <= 36; i++) { float a = i * 0.45f, r = 1 + i * 0.5f, nx = x + r * cosf(a * side), ny = y + r * sinf(a); line(px, py, nx, ny, 4, INK); px = nx; py = ny; }
        break;
      }
      case 8: line(x - 12, y - 12, x + 12, y + 12, 6, INK); line(x - 12, y + 12, x + 12, y - 12, 6, INK); break;
      case 9: ellipse(ex, ey, 15, 20, INK); circle(ex - 4, ey - 6, 7, WHITE); circle(ex + 5, ey + 7, 3, WHITE); arc(x, y + 4, 17, 20, 25, 155, 4, BLUE); break;
      case 10: ellipse(ex, ey, 14, 19, INK); circle(ex - 3 * side, ey + 2, 4, WHITE);
        tri(x - side * 24, y - 26, x + side * 24, y - 26, x + side * 24, y - 2, bg); line(x - side * 16, y - 20, x + side * 16, y - 6, 5, INK); break;
      case 11: ellipse(ex, ey, 14, 19, INK); circle(ex + 3 * side, ey - 2, 4, WHITE);
        tri(x + side * 24, y - 26, x - side * 24, y - 26, x - side * 24, y - 2, bg); line(x + side * 16, y - 20, x - side * 16, y - 6, 5, INK); break;
      case 12: if (side < 0) drawEye(0, x, y, side, lx, ly); else arc(x, y + 8, 15, 15, 180, 360, 6, INK); break;
      case 13: { float sx = x + (lx >= 0 ? 7 : -7); ellipse(sx, y + 3, 13, 16, INK); rrect(x - 24, y - 26, 48, 22, 0, bg); line(x - 17, y - 4, x + 17, y - 4, 5, INK); break; }
      case 14: ellipse(ex, ey, 16, 21, INK); circle(ex - 6, ey - 7, 6, WHITE); circle(ex + 6, ey + 7, 3, WHITE); star(ex + 5, ey - 9, 5, WHITE); break;
      case 15: circle(ex, ey, 7, INK); break;
    }
  }

  void drawMouth(int m) {
    const float x = 86, y = 205;
    switch (m) {
      case 0: arc(x, y - 10, 18, 14, 20, 160, 6, INK); break;
      case 1: ellipse(x, y - 8, 26, 24, INK); rrect(x - 30, y - 34, 60, 26, 0, bg); ellipse(x, y + 8, 12, 6, PINK); break;
      case 2: ellipse(x, y, 9, 11, INK); ellipse(x, y + 4, 5, 4, PINK); break;
      case 3: line(x - 16, y, x + 16, y, 6, INK); break;
      case 4: arc(x, y + 10, 17, 13, 200, 340, 6, INK); break;
      case 5: { float px = x - 22, py = y; for (int i = 1; i <= 16; i++) { float nx = x - 22 + i * 44.0f / 16, ny = y + 4 * sinf(i * PI / 4); line(px, py, nx, ny, 5, INK); px = nx; py = ny; } break; }
      case 6: arc(x - 8, y - 5, 8, 8, 0, 180, 5, INK); arc(x + 8, y - 5, 8, 8, 0, 180, 5, INK); break;
      case 7: rrect(x - 8, y - 2, 16, 16, 8, PINK); line(x, y + 2, x, y + 9, 2, rgb(mix24(PINK24, INK24, 0.35f))); arc(x, y - 10, 18, 12, 20, 160, 6, INK); break;
      case 8: rrect(x - 26, y - 13, 52, 26, 9, INK); rrect(x - 23, y - 10, 46, 20, 6, WHITE);
        line(x - 22, y, x + 22, y, 2, INK); for (int d = -11; d <= 11; d += 11) line(x + d, y - 9, x + d, y + 9, 2, INK); break;
      case 9: ellipse(x, y + 2, 17, 21, INK); ellipse(x, y + 14, 10, 6, PINK); break;
      case 10: line(x - 15, y + 4, x + 13, y - 4, 6, INK); arc(x + 15, y - 10, 5, 6, 30, 120, 4, INK); break;
      case 11: arc(x - 3, y - 6, 6, 5, -90, 90, 4, INK); arc(x - 3, y + 4, 6, 5, -90, 90, 4, INK); break;
    }
  }

  void drawExtra(int e) {
    if (e == 1) { uint16_t c = rgb(mix24(bg24, PINK24, 0.65f)); ellipse(36, 178, 14, 8, c); ellipse(136, 178, 14, 8, c); }
    if (e == 2) drop(146, 98, 8, BLUE);
    if (e == 3) { drop(46, 172, 6, BLUE); drop(126, 172, 6, BLUE); drop(44, 196, 4, BLUE); }
  }

  void drawDeco(int d, float t) {
    float bob = sinf(t * 3) * 4;
    if (d == 1) { text("z", 132, 70 + bob, 2, INK); text("Z", 148, 50 - bob, 3, INK); }
    if (d == 2) {
      for (int k = 0; k < 2; k++) {
        float x = k ? 146 : 26, yy = (k ? 60 : 70) + sinf(t * 4 + k * 2) * 6;
        circle(x, yy + 10, 5, INK); line(x + 4, yy + 10, x + 4, yy - 8, 3, INK); line(x + 4, yy - 8, x + 12, yy - 4, 3, INK);
      }
    }
    if (d == 3) heart(146, 64 - fmodf(t * 20, 30), 10, PINK);
    if (d == 4) text("?", 148, 60 + bob, 4, INK);
    if (d == 5) text("!", 148, 60 + bob, 4, rgb(0xe0453a));
    if (d == 6) { star(26, 66 + bob, 7, YEL); star(150, 54 - bob, 9, YEL); star(140, 236 + bob, 5, YEL); }
  }
};
