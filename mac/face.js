// DeskPet face engine: the same drawing as firmware/DeskPet/face.h, so the simulator shows what the board will.
// A face is eye style (16) x mouth (12) x extra (4) = 768 faces; id = eye*48 + mouth*4 + extra.
// Screen is 172 x 320, portrait, like the ESP32-S3-LCD-1.47.
const W = 172, H = 320;
const INK = "#2b2235", WHITE = "#ffffff", PINK = "#ff7aa8", BLUE = "#59b7ff", YEL = "#ffd34d";
const EYES = ["round", "happy", "closed", "wide", "sleepy", "heart", "star", "dizzy", "x", "teary",
  "angry", "sad", "wink", "unimpressed", "sparkle", "dot"];
const MOUTHS = ["smile", "grin", "o", "flat", "frown", "wavy", "cat", "tongue", "teeth", "shout", "smirk", "kiss"];
const EXTRAS = ["", "blush", "sweat", "tears"];
const DECOS = ["", "zzz", "notes", "heart", "question", "exclaim", "sparkles"];
const FACE_COUNT = EYES.length * MOUTHS.length * EXTRAS.length;
const OPEN_EYES = new Set([0, 3, 4, 9, 10, 11, 13, 14, 15]);  // these blink

function faceParts(id) { return { eye: Math.floor(id / 48) % 16, mouth: Math.floor(id / 4) % 12, extra: id % 4 }; }
function faceId(eye, mouth, extra) { return eye * 48 + mouth * 4 + extra; }
function faceName(id) {
  const p = faceParts(id);
  return [EYES[p.eye] + " eyes", MOUTHS[p.mouth], EXTRAS[p.extra]].filter(Boolean).join(", ");
}

function hex(c) { return [1, 3, 5].map(i => parseInt(c.slice(i, i + 2), 16)); }
function mix(a, b, t) {
  const x = hex(a), y = hex(b);
  return "#" + x.map((v, i) => Math.round(v + (y[i] - v) * t).toString(16).padStart(2, "0")).join("");
}

// The drawing primitives the firmware also has (fillCircle, fillEllipse, fillRoundRect, fillTriangle, thick lines).
function canvasGfx(ctx) {
  const g = {
    fill(c) { ctx.fillStyle = c; ctx.fillRect(0, 0, W, H); },
    circle(x, y, r, c) { ctx.fillStyle = c; ctx.beginPath(); ctx.arc(x, y, r, 0, 7); ctx.fill(); },
    ellipse(x, y, rx, ry, c) { ctx.fillStyle = c; ctx.beginPath(); ctx.ellipse(x, y, rx, ry, 0, 0, 7); ctx.fill(); },
    rrect(x, y, w, h, r, c) { ctx.fillStyle = c; ctx.beginPath(); ctx.roundRect(x, y, w, h, r); ctx.fill(); },
    tri(x1, y1, x2, y2, x3, y3, c) { ctx.fillStyle = c; ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.lineTo(x3, y3); ctx.fill(); },
    line(x1, y1, x2, y2, w, c) { ctx.strokeStyle = c; ctx.lineWidth = w; ctx.lineCap = "round"; ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke(); },
    // angles in degrees, 0 = right, 90 = down (screen y grows downward)
    arc(cx, cy, rx, ry, a0, a1, w, c) {
      const n = 14;
      for (let i = 0; i < n; i++) {
        const t0 = (a0 + (a1 - a0) * i / n) * Math.PI / 180, t1 = (a0 + (a1 - a0) * (i + 1) / n) * Math.PI / 180;
        g.line(cx + rx * Math.cos(t0), cy + ry * Math.sin(t0), cx + rx * Math.cos(t1), cy + ry * Math.sin(t1), w, c);
      }
    },
    text(s, x, y, size, c) { ctx.fillStyle = c; ctx.font = `bold ${8 * size}px ui-monospace, Menlo, monospace`; ctx.textAlign = "center"; ctx.textBaseline = "middle"; ctx.fillText(s, x, y); },
  };
  return g;
}

function heart(g, x, y, s, c) {
  g.circle(x - s * 0.45, y - s * 0.2, s * 0.5, c); g.circle(x + s * 0.45, y - s * 0.2, s * 0.5, c);
  g.tri(x - s * 0.93, y, x + s * 0.93, y, x, y + s * 0.95, c);
}
function star(g, x, y, r, c) {
  const p = [];
  for (let i = 0; i < 10; i++) { const a = (-90 + i * 36) * Math.PI / 180, rr = i % 2 ? r * 0.45 : r; p.push([x + rr * Math.cos(a), y + rr * Math.sin(a)]); }
  for (let i = 0; i < 10; i++) { const a = p[i], b = p[(i + 1) % 10]; g.tri(x, y, a[0], a[1], b[0], b[1], c); }
}
function drop(g, x, y, r, c) { g.circle(x, y, r, c); g.tri(x - r * 0.9, y - r * 0.3, x + r * 0.9, y - r * 0.3, x, y - r * 2.2, c); }

function drawEye(g, s, x, y, side, lx, ly, bg) {
  const ex = x + lx, ey = y + ly;
  switch (s) {
    case 0: g.ellipse(ex, ey, 14, 19, INK); g.circle(ex - 5, ey - 7, 5, WHITE); g.circle(ex + 4, ey + 6, 2, WHITE); break;
    case 1: g.arc(x, y + 8, 15, 15, 180, 360, 6, INK); break;
    case 2: g.line(x - 14, y, x + 14, y, 6, INK); break;
    case 3: g.circle(x, y, 21, INK); g.circle(x, y, 18, WHITE); g.circle(x + lx * 1.4, y + ly * 1.4, 9, INK); g.circle(x + lx * 1.4 - 3, y + ly * 1.4 - 3, 3, WHITE); break;
    case 4: g.ellipse(ex, ey, 14, 19, INK); g.rrect(x - 24, y - 28, 48, 27, 0, bg); g.line(x - 16, y - 1, x + 16, y - 1, 5, INK); break;
    case 5: heart(g, x, y, 20, PINK); g.circle(x - 8, y - 8, 3, WHITE); break;
    case 6: star(g, x, y, 22, YEL); g.circle(x, y, 4, WHITE); break;
    case 7: {
      let px = x, py = y;
      for (let i = 1; i <= 36; i++) { const a = i * 0.45, r = 1 + i * 0.5, nx = x + r * Math.cos(a * side), ny = y + r * Math.sin(a); g.line(px, py, nx, ny, 4, INK); px = nx; py = ny; }
      break;
    }
    case 8: g.line(x - 12, y - 12, x + 12, y + 12, 6, INK); g.line(x - 12, y + 12, x + 12, y - 12, 6, INK); break;
    case 9: g.ellipse(ex, ey, 15, 20, INK); g.circle(ex - 4, ey - 6, 7, WHITE); g.circle(ex + 5, ey + 7, 3, WHITE); g.arc(x, y + 4, 17, 20, 25, 155, 4, BLUE); break;
    case 11: g.ellipse(ex, ey, 14, 19, INK); g.circle(ex - 3 * side, ey + 2, 4, WHITE);
      g.tri(x - side * 24, y - 26, x + side * 24, y - 26, x + side * 24, y - 2, bg); g.line(x - side * 16, y - 20, x + side * 16, y - 6, 5, INK); break;
    case 10: g.ellipse(ex, ey, 14, 19, INK); g.circle(ex + 3 * side, ey - 2, 4, WHITE);
      g.tri(x + side * 24, y - 26, x - side * 24, y - 26, x - side * 24, y - 2, bg); g.line(x + side * 16, y - 20, x - side * 16, y - 6, 5, INK); break;
    case 12: if (side < 0) drawEye(g, 0, x, y, side, lx, ly, bg); else g.arc(x, y + 8, 15, 15, 180, 360, 6, INK); break;
    case 13: { const sx = x + (lx >= 0 ? 7 : -7); g.ellipse(sx, y + 3, 13, 16, INK); g.rrect(x - 24, y - 26, 48, 22, 0, bg); g.line(x - 17, y - 4, x + 17, y - 4, 5, INK); break; }
    case 14: g.ellipse(ex, ey, 16, 21, INK); g.circle(ex - 6, ey - 7, 6, WHITE); g.circle(ex + 6, ey + 7, 3, WHITE); star(g, ex + 5, ey - 9, 5, WHITE); break;
    case 15: g.circle(ex, ey, 7, INK); break;
  }
}

function drawMouth(g, m, bg) {
  const x = 86, y = 205;
  switch (m) {
    case 0: g.arc(x, y - 10, 18, 14, 20, 160, 6, INK); break;
    case 1: g.ellipse(x, y - 8, 26, 24, INK); g.rrect(x - 30, y - 34, 60, 26, 0, bg); g.ellipse(x, y + 8, 12, 6, PINK); break;
    case 2: g.ellipse(x, y, 9, 11, INK); g.ellipse(x, y + 4, 5, 4, PINK); break;
    case 3: g.line(x - 16, y, x + 16, y, 6, INK); break;
    case 4: g.arc(x, y + 10, 17, 13, 200, 340, 6, INK); break;
    case 5: { let px = x - 22, py = y; for (let i = 1; i <= 16; i++) { const nx = x - 22 + i * 44 / 16, ny = y + 4 * Math.sin(i * Math.PI / 4); g.line(px, py, nx, ny, 5, INK); px = nx; py = ny; } break; }
    case 6: g.arc(x - 8, y - 5, 8, 8, 0, 180, 5, INK); g.arc(x + 8, y - 5, 8, 8, 0, 180, 5, INK); break;
    case 7: g.rrect(x - 8, y - 2, 16, 16, 8, PINK); g.line(x, y + 2, x, y + 9, 2, mix(PINK, INK, 0.35)); g.arc(x, y - 10, 18, 12, 20, 160, 6, INK); break;
    case 8: g.rrect(x - 26, y - 13, 52, 26, 9, INK); g.rrect(x - 23, y - 10, 46, 20, 6, WHITE);
      g.line(x - 22, y, x + 22, y, 2, INK); for (const d of [-11, 0, 11]) g.line(x + d, y - 9, x + d, y + 9, 2, INK); break;
    case 9: g.ellipse(x, y + 2, 17, 21, INK); g.ellipse(x, y + 14, 10, 6, PINK); break;
    case 11: g.line(x - 15, y + 4, x + 13, y - 4, 6, INK); g.arc(x + 15, y - 10, 5, 6, 30, 120, 4, INK); break;
    case 10: g.arc(x - 3, y - 6, 6, 5, -90, 90, 4, INK); g.arc(x - 3, y + 4, 6, 5, -90, 90, 4, INK); break;
  }
}

function drawExtra(g, e, bg) {
  if (e === 1) { const c = mix(bg, PINK, 0.65); g.ellipse(36, 178, 14, 8, c); g.ellipse(136, 178, 14, 8, c); }
  if (e === 2) drop(g, 146, 98, 8, BLUE);
  if (e === 3) { drop(g, 46, 172, 6, BLUE); drop(g, 126, 172, 6, BLUE); drop(g, 44, 196, 4, BLUE); }
}

function drawDeco(g, d, t) {
  const bob = Math.sin(t * 3) * 4;
  if (d === 1) { g.text("z", 132, 70 + bob, 2, INK); g.text("Z", 148, 50 - bob, 3, INK); }
  if (d === 2) { for (const [x, y, k] of [[26, 70, 0], [146, 60, 1]]) { const yy = y + Math.sin(t * 4 + k * 2) * 6; g.circle(x, yy + 10, 5, INK); g.line(x + 4, yy + 10, x + 4, yy - 8, 3, INK); g.line(x + 4, yy - 8, x + 12, yy - 4, 3, INK); } }
  if (d === 3) heart(g, 146, 64 - (t * 20 % 30), 10, PINK);
  if (d === 4) g.text("?", 148, 60 + bob, 4, INK);
  if (d === 5) g.text("!", 148, 60 + bob, 4, "#e0453a");
  if (d === 6) { star(g, 26, 66 + bob, 7, YEL); star(g, 150, 54 - bob, 9, YEL); star(g, 140, 236 + bob, 5, YEL); }
}

// Draws one frame. st: {id, bg, deco, caption, lx, ly, anim}; t = seconds since start.
function drawFace(g, st, t) {
  const p = faceParts(st.id), bg = st.bg;
  g.fill(bg);
  let dy = Math.sin(t * 2) * 2, dx = 0;                     // breathing
  if (st.anim === 1) dy = -Math.abs(Math.sin(t * 6)) * 10;  // bounce
  if (st.anim === 2) dx = Math.sin(t * 18) * 3;             // shiver
  const blink = OPEN_EYES.has(p.eye) && (t % 4.3) < 0.13;
  const sg = Object.create(g);  // the face moves as one; caption stays put
  for (const k of ["circle", "ellipse"]) sg[k] = (x, y, ...r) => g[k](x + dx, y + dy, ...r);
  sg.rrect = (x, y, ...r) => g.rrect(x + dx, y + dy, ...r);
  sg.tri = (a, b, c, d, e, f, col) => g.tri(a + dx, b + dy, c + dx, d + dy, e + dx, f + dy, col);
  sg.line = (a, b, c, d, w, col) => g.line(a + dx, b + dy, c + dx, d + dy, w, col);
  sg.arc = (cx, cy, ...r) => g.arc(cx + dx, cy + dy, ...r);
  sg.text = (s, x, y, ...r) => g.text(s, x + dx, y + dy, ...r);
  for (const [x, side] of [[52, -1], [120, 1]]) drawEye(sg, blink ? 2 : p.eye, x, 140, side, st.lx || 0, st.ly || 0, bg);
  drawMouth(sg, p.mouth, bg);
  drawExtra(sg, p.extra, bg);
  drawDeco(g, st.deco || 0, t);
  if (st.caption) {
    const s = st.caption.slice(0, 13), w = s.length * 12 + 16;
    g.rrect(86 - w / 2, 276, w, 26, 13, mix(bg, INK, 0.82));
    g.text(s, 86, 290, 2, WHITE);
  }
}

if (typeof module !== "undefined") module.exports = { faceParts, faceId, faceName, FACE_COUNT };
