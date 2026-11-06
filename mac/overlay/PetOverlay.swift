// PetOverlay: the ring pet's own pointer and pen, drawn on top of the screen.
// A see-through window that never takes clicks. It polls brain.py (http://127.0.0.1:4330/overlay) and, when the AI
// has looked at the screen, flies its pointer to each thing, draws circles / arrows / underlines / boxes / notes
// stroke by stroke, and shows what it said in a speech bubble. Everything fades after a while.
//   build:  swiftc -O PetOverlay.swift -o PetOverlay      run: ./PetOverlay
import AppKit
import AVFoundation

struct Mark { var kind: String; var x = 0.0, y = 0.0, x2 = 0.0, y2 = 0.0, r = 0.0, w = 0.0, h = 0.0; var text = ""
  var crayon = NSColor.systemPink; var seed = Double.random(in: 0...100) }
// a child's crayon box
let CRAYONS: [NSColor] = [.systemPink, .systemYellow, .systemGreen, .systemOrange, .systemPurple, .systemTeal, .systemRed]
// and a toddler's whiteboard markers
let MARKERS: [NSColor] = [NSColor(calibratedRed: 0.1, green: 0.3, blue: 1, alpha: 1), NSColor(calibratedRed: 0.95, green: 0.1, blue: 0.15, alpha: 1),
                          NSColor(calibratedRed: 0.05, green: 0.65, blue: 0.2, alpha: 1), NSColor(calibratedWhite: 0.1, alpha: 1)]

final class PetView: NSView {
  var pet = CGPoint(x: -100, y: -100), petTarget = CGPoint.zero, home = CGPoint.zero
  var colour = NSColor(calibratedRed: 0.1, green: 1, blue: 0.4, alpha: 1)
  var marks: [Mark] = [], drawn: [(Mark, Double)] = []  // (mark, how much of it is drawn 0..1)
  var marker = false, listening = false, thinking = false, status = "", thinkingSince = Date()
  var mood = "happy", eventName = "", eventAt = 0.0, lastEventAt = 0.0, music: (bpm: Double, since: Double)? = nil
  var particles: [(x: Double, y: Double, vx: Double, vy: Double, life: Double, kind: Int, hue: Double)] = []
  var scale = 0.0, blink = 0.0, squash = 0.0
  struct MoodLook { let colour: NSColor; let eye: Int }  // eye: 0 normal 1 wide 2 narrow 3 happy-closed 4 worried 5 heart 6 spiral
  let looks: [String: MoodLook] = [
    "happy": .init(colour: NSColor(calibratedRed: 0.1, green: 1, blue: 0.4, alpha: 1), eye: 0),
    "excited": .init(colour: NSColor(calibratedRed: 0, green: 0.85, blue: 1, alpha: 1), eye: 1),
    "love": .init(colour: NSColor(calibratedRed: 1, green: 0.3, blue: 0.55, alpha: 1), eye: 5),
    "celebrate": .init(colour: NSColor(calibratedRed: 1, green: 0.8, blue: 0, alpha: 1), eye: 3),
    "proud": .init(colour: NSColor(calibratedRed: 0.4, green: 0.9, blue: 1, alpha: 1), eye: 3),
    "laughing": .init(colour: NSColor(calibratedRed: 1, green: 0.85, blue: 0.2, alpha: 1), eye: 3),
    "focused": .init(colour: NSColor(calibratedRed: 0.45, green: 0.6, blue: 1, alpha: 1), eye: 2),
    "thinking": .init(colour: NSColor(calibratedRed: 0.6, green: 0.5, blue: 1, alpha: 1), eye: 2),
    "curious": .init(colour: NSColor(calibratedRed: 0.6, green: 0.25, blue: 1, alpha: 1), eye: 1),
    "reading": .init(colour: NSColor(calibratedRed: 0.8, green: 0.7, blue: 0.5, alpha: 1), eye: 2),
    "music": .init(colour: NSColor(calibratedRed: 1, green: 0.2, blue: 0.8, alpha: 1), eye: 3),
    "gaming": .init(colour: NSColor(calibratedRed: 1, green: 0.5, blue: 0.2, alpha: 1), eye: 1),
    "worried": .init(colour: NSColor(calibratedRed: 1, green: 0.4, blue: 0.1, alpha: 1), eye: 4),
    "dizzy": .init(colour: NSColor(calibratedRed: 1, green: 1, blue: 0.2, alpha: 1), eye: 6),
    "bored": .init(colour: NSColor(calibratedRed: 0.6, green: 0.3, blue: 1, alpha: 1), eye: 2),
    "sleepy": .init(colour: NSColor(calibratedRed: 0.2, green: 0.3, blue: 0.8, alpha: 1), eye: 2),
    "asleep": .init(colour: NSColor(calibratedRed: 0.1, green: 0.15, blue: 0.5, alpha: 1), eye: 3),
    "lowbattery": .init(colour: NSColor(calibratedRed: 1, green: 0.2, blue: 0.2, alpha: 1), eye: 4),
    "charging": .init(colour: NSColor(calibratedRed: 0.2, green: 1, blue: 0.3, alpha: 1), eye: 1),
    "shy": .init(colour: NSColor(calibratedRed: 1, green: 0.6, blue: 0.7, alpha: 1), eye: 2),
  ]
  func look() -> MoodLook { looks[mood] ?? looks["happy"]! }

  func spawn(_ kind: Int, _ n: Int, at c: CGPoint) {  // 0 heart 1 confetti 2 note 3 zzz 4 sweat 5 sparkle 6 question
    for _ in 0..<n {
      particles.append((c.x + Double.random(in: -8...8), c.y, Double.random(in: -40...40), Double.random(in: -90 ... -30),
                        1.0, kind, Double.random(in: 0...1)))
    }
  }
  func react(_ e: String) {
    eventAt = t
    let c = CGPoint(x: pet.x, y: pet.y - 20)
    switch e {
    case "love", "song_start": spawn(e == "love" ? 0 : 2, 6, at: c)
    case "celebrate", "success", "clap": spawn(1, 18, at: c); squash = 1
    case "laugh": squash = 1
    case "worried", "error", "low_batt": spawn(4, 1, at: c)
    case "away": spawn(3, 3, at: c)
    case "back", "hello", "charging": squash = 1; spawn(5, 6, at: c)
    case "app_switch", "curious": spawn(6, 1, at: c)
    case "screenshot": alphaAll = 1
    default: break
    }
  }
  var current = -1, progress = 0.0, bubble = "", bubbleAt = 0.0, lastVersion = -1, fadeAt = 0.0, alphaAll = 1.0
  var t = 0.0, blinkAt = 0.0

  override var isFlipped: Bool { true }

  func P(_ x: Double, _ y: Double) -> CGPoint { CGPoint(x: x * bounds.width, y: y * bounds.height) }

  func start(_ j: [String: Any]) {
    marks = ((j["draw"] as? [[String: Any]]) ?? []).compactMap { d in
      guard let k = d["type"] as? String else { return nil }
      func n(_ s: String) -> Double { (d[s] as? NSNumber)?.doubleValue ?? 0 }
      return Mark(kind: k, x: n("x"), y: n("y"), x2: n("x2"), y2: n("y2"), r: n("r"), w: n("w"), h: n("h"), text: d["text"] as? String ?? "",
                  crayon: colour)
    }
    marker = (j["marker"] as? Bool) == true; thinking = false
    drawn = []; current = marks.isEmpty ? -1 : 0; progress = 0; alphaAll = 1
    bubble = j["say"] as? String ?? ""; bubbleAt = t
    if let c = j["colour"] as? [Double], c.count == 3 { colour = NSColor(calibratedRed: c[0], green: c[1], blue: c[2], alpha: 1) }
    if let p = j["point"] as? [Double], p.count == 2 { petTarget = P(p[0], p[1]) }
    for i in marks.indices { marks[i].crayon = colour }
    if current >= 0 { petTarget = startPoint(marks[0]) }
    fadeAt = t + 14 + Double(marks.count) * 2
  }

  func startPoint(_ m: Mark) -> CGPoint {
    switch m.kind {
    case "circle": return P(m.x + m.r, m.y)
    case "box": return P(m.x, m.y)
    case "star", "heart", "smiley", "spiral", "flower", "scribble", "mess", "zigzag", "loops", "sun", "person", "dots", "wiggle": return doodle(m)[0]
    default: return P(m.x, m.y)
    }
  }

  // the path of a mark, cut off at fraction f, plus where the pen tip is
  func path(_ m: Mark, _ f: Double) -> (NSBezierPath, CGPoint) {
    let p = NSBezierPath(); var tip = P(m.x, m.y)
    switch m.kind {
    case "circle":  // a hand-drawn loop, a bit wobbly and overshooting like a real pen
      let c = P(m.x, m.y), rx = m.r * bounds.width, ry = m.h > 0 ? m.h * bounds.height : m.r * bounds.width * 0.8
      let steps = max(2, Int(60 * f))
      for i in 0...steps {
        let a = Double(i) / 60 * 2.08 * .pi, wob = 1 + 0.012 * sin(a * 3)
        let q = CGPoint(x: c.x + cos(a) * rx * wob, y: c.y - sin(a) * ry * wob)
        i == 0 ? p.move(to: q) : p.line(to: q); tip = q
      }
    case "box":
      let a = P(m.x, m.y), b = P(m.x + m.w, m.y + m.h)
      let pts = [a, CGPoint(x: b.x, y: a.y), b, CGPoint(x: a.x, y: b.y), a]
      p.move(to: a); let segs = f * 4
      for i in 0..<4 where Double(i) < segs {
        let k = min(1, segs - Double(i)), s = pts[i], e = pts[i + 1]
        tip = CGPoint(x: s.x + (e.x - s.x) * k, y: s.y + (e.y - s.y) * k); p.line(to: tip)
      }
    case "arrow", "underline":
      let a = P(m.x, m.y), b = m.kind == "underline" ? P(m.x2, m.y) : P(m.x2, m.y2)
      tip = CGPoint(x: a.x + (b.x - a.x) * f, y: a.y + (b.y - a.y) * f)
      p.move(to: a); p.line(to: tip)
      if m.kind == "arrow" && f > 0.95 {
        let ang = atan2(b.y - a.y, b.x - a.x)
        for s in [-0.5, 0.5] { p.move(to: b); p.line(to: CGPoint(x: b.x - 22 * cos(ang + s), y: b.y - 22 * sin(ang + s))) }
      }
    default:
      let pts = doodle(m)
      let n = max(1, Int(Double(pts.count - 1) * f))
      p.move(to: pts[0]); tip = pts[0]
      for i in 1...n where i < pts.count { p.line(to: pts[i]); tip = pts[i] }
    }
    return (p, tip)
  }

  // star / heart / smiley / spiral / scribble / flower, a bit wonky like a kid drew them
  func doodle(_ m: Mark) -> [CGPoint] {
    let c = P(m.x, m.y), R = max(0.012, m.r) * bounds.width
    var out: [CGPoint] = []
    func wob(_ i: Int) -> Double { 1 + 0.07 * sin(Double(i) * 1.7 + m.seed) }
    switch m.kind {
    case "star":
      for i in 0...10 { let a = Double(i) * .pi / 5 - .pi / 2, rr = (i % 2 == 0 ? R : R * 0.45) * wob(i)
        out.append(CGPoint(x: c.x + cos(a) * rr, y: c.y + sin(a) * rr)) }
    case "heart":
      for i in 0...60 { let t = Double(i) / 60 * 2 * .pi
        let x = 16 * pow(sin(t), 3), y = -(13 * cos(t) - 5 * cos(2 * t) - 2 * cos(3 * t) - cos(4 * t))
        out.append(CGPoint(x: c.x + x / 17 * R * wob(i), y: c.y + y / 17 * R * wob(i))) }
    case "smiley":
      for i in 0...40 { let a = Double(i) / 40 * 2 * .pi; out.append(CGPoint(x: c.x + cos(a) * R * wob(i), y: c.y + sin(a) * R * wob(i))) }
      out.append(CGPoint(x: c.x - R * 0.35, y: c.y - R * 0.3)); out.append(CGPoint(x: c.x - R * 0.35, y: c.y - R * 0.15))
      out.append(CGPoint(x: c.x + R * 0.35, y: c.y - R * 0.3)); out.append(CGPoint(x: c.x + R * 0.35, y: c.y - R * 0.15))
      for i in 0...12 { let a = .pi * 0.15 + Double(i) / 12 * .pi * 0.7; out.append(CGPoint(x: c.x + cos(a) * R * 0.55, y: c.y + sin(a) * R * 0.5)) }
    case "spiral":
      for i in 0...70 { let a = Double(i) / 70 * 5 * .pi, rr = R * Double(i) / 70
        out.append(CGPoint(x: c.x + cos(a) * rr, y: c.y + sin(a) * rr)) }
    case "flower":
      for i in 0...100 { let a = Double(i) / 100 * 2 * .pi, rr = R * (0.55 + 0.45 * abs(sin(a * 2.5))) * wob(i)
        out.append(CGPoint(x: c.x + cos(a) * rr, y: c.y + sin(a) * rr)) }
    case "mess":  // a toddler's random scribble: a wandering line with momentum, never the same twice
      var g = SystemRandomNumberGenerator(); var q = c, ang = Double.random(in: 0...6.28, using: &g), sp = R * 0.12
      for _ in 0...Int.random(in: 60...140) {
        ang += Double.random(in: -0.9...0.9); sp = max(R * 0.05, min(R * 0.25, sp + Double.random(in: -0.03...0.03) * R))
        q.x += cos(ang) * sp; q.y += sin(ang) * sp
        if hypot(q.x - c.x, q.y - c.y) > R * 1.3 { ang = atan2(c.y - q.y, c.x - q.x) + Double.random(in: -0.6...0.6) }
        out.append(q)
      }
    case "zigzag":
      let n = Int.random(in: 6...14), tilt = Double.random(in: -0.5...0.5)
      for i in 0...n { let f = Double(i) / Double(n) - 0.5, up = (i % 2 == 0 ? -1.0 : 1.0) * R * Double.random(in: 0.25...0.6)
        out.append(CGPoint(x: c.x + f * 2 * R + up * tilt, y: c.y + up + f * 2 * R * tilt)) }
    case "loops":
      let n = Int.random(in: 4...9), rr = R / Double(n) * Double.random(in: 1.2...2.2)
      for i in 0...(n * 20) { let f = Double(i) / Double(n * 20), a = f * Double(n) * 2 * .pi
        out.append(CGPoint(x: c.x - R + f * 2 * R + cos(a) * rr, y: c.y + sin(a) * rr * 1.3 + sin(f * 6) * R * 0.2)) }
    case "dots":  // stabbing the board with the pen
      for _ in 0..<Int.random(in: 6...16) { let q = CGPoint(x: c.x + Double.random(in: -R...R), y: c.y + Double.random(in: -R...R))
        out.append(q); out.append(CGPoint(x: q.x + 2, y: q.y + 2)); out.append(q) }
    case "wiggle":
      let n = Int.random(in: 3...7), amp = R * Double.random(in: 0.15...0.4)
      for i in 0...80 { let f = Double(i) / 80
        out.append(CGPoint(x: c.x - R + f * 2 * R, y: c.y + sin(f * Double(n) * 2 * .pi) * amp * (1 + Double.random(in: -0.15...0.15)))) }
    case "sun":
      for i in 0...30 { let a = Double(i) / 30 * 2 * .pi; out.append(CGPoint(x: c.x + cos(a) * R * 0.45 * wob(i), y: c.y + sin(a) * R * 0.45 * wob(i))) }
      for i in 0..<Int.random(in: 6...11) { let a = Double(i) / 9 * 2 * .pi + Double.random(in: -0.2...0.2)
        out.append(CGPoint(x: c.x + cos(a) * R * 0.55, y: c.y + sin(a) * R * 0.55))
        out.append(CGPoint(x: c.x + cos(a) * R * Double.random(in: 0.85...1.2), y: c.y + sin(a) * R * Double.random(in: 0.85...1.2)))
        out.append(CGPoint(x: c.x + cos(a) * R * 0.55, y: c.y + sin(a) * R * 0.55)) }
    case "person":  // a potato with sticks
      for i in 0...24 { let a = Double(i) / 24 * 2 * .pi; out.append(CGPoint(x: c.x + cos(a) * R * 0.35 * wob(i), y: c.y - R * 0.45 + sin(a) * R * 0.35 * wob(i))) }
      let neck = CGPoint(x: c.x, y: c.y - R * 0.1), hip = CGPoint(x: c.x + Double.random(in: -5...5), y: c.y + R * 0.5)
      out += [neck, CGPoint(x: c.x - R * 0.6, y: c.y + Double.random(in: -R * 0.4...0)), neck, CGPoint(x: c.x + R * 0.6, y: c.y + Double.random(in: -R * 0.4...0)),
              neck, hip, CGPoint(x: c.x - R * 0.4, y: c.y + R), hip, CGPoint(x: c.x + R * 0.4, y: c.y + R)]
    default:  // scribble
      var q = c
      for i in 0...40 { q.x += cos(Double(i) * 1.3 + m.seed) * R * 0.35; q.y += sin(Double(i) * 0.9 + m.seed * 2) * R * 0.35
        out.append(q) }
    }
    return out
  }

  func tick() {
    t += 1.0 / 60
    // pop in when there's something to show, shrink away when not
    let wanted = (current >= 0 || t < fadeAt || listening || thinking || t - eventAt < 4 || music != nil) ? 1.0 : 0.0
    scale += (wanted - scale) * (wanted > scale ? 0.18 : 0.08)
    squash = max(0, squash - 0.04)
    if t > blinkAt + 3 + Double.random(in: 0...3) { blinkAt = t }
    for i in particles.indices {
      particles[i].x += particles[i].vx / 60; particles[i].y += particles[i].vy / 60
      particles[i].vy += (particles[i].kind == 1 ? 120 : -10) / 60; particles[i].life -= 1.0 / 90
    }
    particles.removeAll { $0.life <= 0 }
    if home == .zero { home = CGPoint(x: bounds.width - 90, y: bounds.height - 120); pet = home; petTarget = home }
    if current >= 0 && current < marks.count {
      let m = marks[current]
      let d = hypot(pet.x - startPoint(m).x, pet.y - startPoint(m).y)
      if progress == 0 && d > 6 { petTarget = startPoint(m) }  // fly there first
      else {
        progress = min(1, progress + (m.kind == "text" ? 0.08 : 0.035))
        if m.kind != "text" { petTarget = path(m, progress).1 } else { petTarget = P(m.x, m.y) }
        if progress >= 1 { drawn.append((m, 1)); current += 1; progress = 0; if current >= marks.count { current = -1 } }
      }
    } else if t > fadeAt {
      petTarget = home
      alphaAll = max(0, alphaAll - 0.02)
      if alphaAll == 0 { drawn = []; bubble = "" }
    }
    if listening || thinking {
      let mouse = NSEvent.mouseLocation, f = win.frame
      petTarget = CGPoint(x: mouse.x - f.minX + 34, y: f.height - (mouse.y - f.minY) + 30)
      bubble = listening ? "I'm listening!" : (status.isEmpty ? "hmm, let me see..." : status); bubbleAt = t - 10; alphaAll = 1; fadeAt = t + 60
    }
    if let m = music, current < 0 {  // dancing: bounce on the beat next to its home spot
      let beat = ((t - m.since) * m.bpm / 60).truncatingRemainder(dividingBy: 1)
      petTarget = CGPoint(x: home.x + sin((t - m.since) * m.bpm / 60 * .pi) * 14, y: home.y - abs(sin(beat * .pi)) * 18)
      if Int((t - m.since) * m.bpm / 60) % 4 == 0 && beat < 0.03 { spawn(2, 1, at: CGPoint(x: pet.x, y: pet.y - 20)) }
    }
    // fly: quick but smooth, with a little bob when it's resting
    let k = current >= 0 && progress > 0 ? 0.6 : 0.12
    pet.x += (petTarget.x - pet.x) * k; pet.y += (petTarget.y - pet.y) * k
    needsDisplay = true
  }

  func drawParticles() {
    for p in particles {
      let a = CGFloat(min(1, p.life * 1.5)) * CGFloat(alphaAll)
      switch p.kind {
      case 0: NSColor(calibratedRed: 1, green: 0.35, blue: 0.55, alpha: a).set(); heart(at: CGPoint(x: p.x, y: p.y), size: 6).fill()
      case 1: NSColor(calibratedHue: CGFloat(p.hue), saturation: 0.9, brightness: 1, alpha: a).set(); NSRect(x: p.x, y: p.y, width: 5, height: 3).fill()
      case 2: NSColor(calibratedRed: 1, green: 0.3, blue: 0.85, alpha: a).set()
        ("♪" as NSString).draw(at: CGPoint(x: p.x, y: p.y), withAttributes: [.font: NSFont.systemFont(ofSize: 14, weight: .bold), .foregroundColor: NSColor(calibratedRed: 1, green: 0.3, blue: 0.85, alpha: a)])
      case 3: ("z" as NSString).draw(at: CGPoint(x: p.x, y: p.y), withAttributes: [.font: NSFont.systemFont(ofSize: 12, weight: .bold), .foregroundColor: NSColor(calibratedRed: 0.5, green: 0.6, blue: 1, alpha: a)])
      case 4: NSColor(calibratedRed: 0.4, green: 0.7, blue: 1, alpha: a).set(); NSBezierPath(ovalIn: NSRect(x: p.x, y: p.y, width: 5, height: 8)).fill()
      case 5: NSColor(calibratedRed: 1, green: 0.95, blue: 0.6, alpha: a).set()
        let s = NSBezierPath(); for i in 0..<8 { let r = i % 2 == 0 ? 4.0 : 1.5, ang = Double(i) * .pi / 4; let q = CGPoint(x: p.x + cos(ang) * r, y: p.y + sin(ang) * r); i == 0 ? s.move(to: q) : s.line(to: q) }; s.close(); s.fill()
      default: ("?" as NSString).draw(at: CGPoint(x: p.x, y: p.y), withAttributes: [.font: NSFont.systemFont(ofSize: 13, weight: .bold), .foregroundColor: NSColor(white: 1, alpha: a)])
      }
    }
  }
  func heart(at c: CGPoint, size s: Double) -> NSBezierPath {
    let p = NSBezierPath()
    p.move(to: CGPoint(x: c.x, y: c.y + s)); p.curve(to: CGPoint(x: c.x - s, y: c.y), controlPoint1: CGPoint(x: c.x - s / 2, y: c.y + s * 0.6), controlPoint2: CGPoint(x: c.x - s, y: c.y + s * 0.4))
    p.curve(to: CGPoint(x: c.x, y: c.y + s * 0.2), controlPoint1: CGPoint(x: c.x - s, y: c.y - s * 0.4), controlPoint2: CGPoint(x: c.x, y: c.y - s * 0.4))
    p.curve(to: CGPoint(x: c.x + s, y: c.y), controlPoint1: CGPoint(x: c.x, y: c.y - s * 0.4), controlPoint2: CGPoint(x: c.x + s, y: c.y - s * 0.4))
    p.curve(to: CGPoint(x: c.x, y: c.y + s), controlPoint1: CGPoint(x: c.x + s, y: c.y + s * 0.4), controlPoint2: CGPoint(x: c.x + s / 2, y: c.y + s * 0.6))
    return p
  }

  override func draw(_ dirty: NSRect) {
    NSColor.clear.set(); dirty.fill()
    func stroke(_ p: NSBezierPath, _ col: NSColor) {
      if marker {
        p.lineWidth = 11; p.lineCapStyle = .round; p.lineJoinStyle = .round
        col.withAlphaComponent(0.72 * alphaAll).set(); p.stroke(); return
      }
      p.lineWidth = 3.5; p.lineCapStyle = .round; p.lineJoinStyle = .round
      NSColor.black.withAlphaComponent(0.25 * alphaAll).set(); let s = p.copy() as! NSBezierPath; s.lineWidth = 6; s.stroke()
      col.withAlphaComponent(0.95 * alphaAll).set(); p.stroke()
    }
    func note(_ m: Mark, _ f: Double) {
      let text = String(m.text.prefix(Int(Double(m.text.count) * f)))
      if text.isEmpty { return }
      let font = NSFont.systemFont(ofSize: 15, weight: .semibold)
      let attrs: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: NSColor.white.withAlphaComponent(alphaAll)]
      let size = (text as NSString).size(withAttributes: attrs)
      let o = P(m.x, m.y), r = NSRect(x: o.x, y: o.y, width: size.width + 18, height: size.height + 8)
      NSColor(white: 0.1, alpha: 0.88 * alphaAll).set(); NSBezierPath(roundedRect: r, xRadius: 7, yRadius: 7).fill()
      let edge = NSBezierPath(roundedRect: r, xRadius: 7, yRadius: 7); edge.lineWidth = 1.5
      m.crayon.withAlphaComponent(0.9 * alphaAll).set(); edge.stroke()
      (text as NSString).draw(at: CGPoint(x: o.x + 9, y: o.y + 4), withAttributes: attrs)
    }
    for (m, _) in drawn { m.kind == "text" ? note(m, 1) : stroke(path(m, 1).0, m.crayon) }
    if current >= 0 && current < marks.count && progress > 0 {
      let m = marks[current]; m.kind == "text" ? note(m, progress) : stroke(path(m, progress).0, m.crayon)
    }

    // the pet pointer: a little glowing ring with eyes, like the real one
    if scale < 0.02 { drawParticles(); return }
    let bob = current < 0 ? sin(t * 1.5) * 1.5 : 0
    let c = CGPoint(x: pet.x, y: pet.y + bob), R = 14.0 * scale * (1 + squash * 0.25 * sin(t * 30))
    let mc = current >= 0 || t < fadeAt ? colour : look().colour
    mc.withAlphaComponent(0.25).set(); NSBezierPath(ovalIn: NSRect(x: c.x - R - 8, y: c.y - R - 8, width: 2 * R + 16, height: 2 * R + 16)).fill()
    NSColor(white: 0.08, alpha: 0.92).set(); NSBezierPath(ovalIn: NSRect(x: c.x - R, y: c.y - R, width: 2 * R, height: 2 * R)).fill()
    let rim = NSBezierPath(ovalIn: NSRect(x: c.x - R, y: c.y - R, width: 2 * R, height: 2 * R)); rim.lineWidth = 3 * scale; mc.set(); rim.stroke()
    let blinking = t - blinkAt < 0.12
    // the eyes look where it's going, or at your mouse when it's resting
    let mouse = NSEvent.mouseLocation, f = win.frame
    let target = current >= 0 ? petTarget : CGPoint(x: mouse.x - f.minX, y: f.height - (mouse.y - f.minY))
    let lx = max(-3, min(3, (target.x - pet.x) / 60)), ly = max(-3, min(3, (target.y - pet.y) / 60))
    let eye = listening ? 1 : look().eye
    NSColor.white.set()
    for dx in [-6.0, 6.0] {
      let ex = c.x + dx * scale - 3.5 + lx, ey = c.y - 4 + ly
      switch eye {
      case 3:  // happy: closed arcs
        let p = NSBezierPath(); p.move(to: CGPoint(x: ex, y: ey + 4)); p.curve(to: CGPoint(x: ex + 7, y: ey + 4), controlPoint1: CGPoint(x: ex + 2, y: ey - 1), controlPoint2: CGPoint(x: ex + 5, y: ey - 1))
        p.lineWidth = 2; p.stroke()
      case 5:  // love: little hearts
        let p = NSBezierPath(); let hx = ex + 3.5, hy = ey + 2
        p.move(to: CGPoint(x: hx, y: hy + 5)); p.curve(to: CGPoint(x: hx - 4, y: hy), controlPoint1: CGPoint(x: hx - 2, y: hy + 3), controlPoint2: CGPoint(x: hx - 4, y: hy + 2))
        p.curve(to: CGPoint(x: hx, y: hy + 1), controlPoint1: CGPoint(x: hx - 4, y: hy - 2), controlPoint2: CGPoint(x: hx, y: hy - 2))
        p.curve(to: CGPoint(x: hx + 4, y: hy), controlPoint1: CGPoint(x: hx, y: hy - 2), controlPoint2: CGPoint(x: hx + 4, y: hy - 2))
        p.curve(to: CGPoint(x: hx, y: hy + 5), controlPoint1: CGPoint(x: hx + 4, y: hy + 2), controlPoint2: CGPoint(x: hx + 2, y: hy + 3))
        NSColor(calibratedRed: 1, green: 0.4, blue: 0.6, alpha: 1).set(); p.fill(); NSColor.white.set()
      case 6:  // dizzy: spirals
        let p = NSBezierPath(); for i in 0...40 { let a = Double(i) / 6 + t * 6, rr = Double(i) / 10; let q = CGPoint(x: ex + 3.5 + cos(a) * rr, y: ey + 4 + sin(a) * rr); i == 0 ? p.move(to: q) : p.line(to: q) }
        p.lineWidth = 1.2; p.stroke()
      default:
        let h: Double = blinking ? 1.5 : eye == 1 ? 10 : eye == 2 ? 4 : 8, w: Double = eye == 1 ? 8 : 7
        let e = NSRect(x: ex, y: ey + (8 - h) / 2 + (eye == 4 ? 1 : 0), width: w, height: h)
        NSBezierPath(roundedRect: e, xRadius: 3, yRadius: 3).fill()
        if eye == 4 {  // worried brows
          let p = NSBezierPath(); p.move(to: CGPoint(x: ex + (dx < 0 ? 0 : 7), y: ey - 3)); p.line(to: CGPoint(x: ex + (dx < 0 ? 7 : 0), y: ey - 1)); p.lineWidth = 1.5; p.stroke()
        }
      }
    }
    drawParticles()
    // red dot = the mic is on (only ever while fn is held)
    if listening { NSColor.systemRed.set(); NSBezierPath(ovalIn: NSRect(x: c.x + 10, y: c.y - 22, width: 10, height: 10)).fill() }
    // the pen nib when it's drawing
    if current >= 0 && current < marks.count && progress > 0 { marks[current].crayon.set(); NSBezierPath(ovalIn: NSRect(x: c.x + 12, y: c.y + 12, width: 7, height: 7)).fill() }

    // speech bubble
    if !bubble.isEmpty && alphaAll > 0 {
      let shown = String(bubble.prefix(Int((t - bubbleAt) * 40)))
      let attrs: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: 14, weight: .medium), .foregroundColor: NSColor.white.withAlphaComponent(alphaAll)]
      let box = (shown as NSString).boundingRect(with: NSSize(width: 300, height: 400), options: [.usesLineFragmentOrigin], attributes: attrs)
      // the bubble sits on whichever side of the pointer is clear of the drawings, never on top of them
      var o = CGPoint(x: c.x + 28, y: c.y - box.height - 30)
      let all = drawn.map { $0.0 } + (current >= 0 && current < marks.count ? [marks[current]] : [])
      var marksRect = NSRect.null
      for m in all { let (p, _) = path(m, 1); marksRect = marksRect.union(m.kind == "text" ? NSRect(x: P(m.x, m.y).x, y: P(m.x, m.y).y, width: 200, height: 30) : p.bounds) }
      let candidate = NSRect(x: o.x, y: o.y, width: box.width + 24, height: box.height + 16)
      if !marksRect.isNull && candidate.insetBy(dx: -10, dy: -10).intersects(marksRect) {
        o.y = marksRect.maxY + 24  // below the drawings
        if o.y + box.height + 16 > bounds.height - 8 { o.y = marksRect.minY - box.height - 40 }  // or above them
      }
      if o.y + box.height + 16 > bounds.height - 8 { o.y = bounds.height - box.height - 24 }
      if o.x + box.width + 24 > bounds.width { o.x = c.x - box.width - 52 }
      if o.y < 8 { o.y = c.y + 30 }
      let r = NSRect(x: o.x, y: o.y, width: box.width + 24, height: box.height + 16)
      NSColor(white: 0.08, alpha: 0.9 * alphaAll).set(); NSBezierPath(roundedRect: r, xRadius: 12, yRadius: 12).fill()
      let edge = NSBezierPath(roundedRect: r, xRadius: 12, yRadius: 12); edge.lineWidth = 2; colour.withAlphaComponent(alphaAll).set(); edge.stroke()
      (shown as NSString).draw(with: NSRect(x: o.x + 12, y: o.y + 8, width: box.width, height: box.height), options: [.usesLineFragmentOrigin], attributes: attrs)
    }
  }
}

let app = NSApplication.shared
app.setActivationPolicy(.accessory)  // no Dock icon, never steals focus
let screen = NSScreen.screens[0]  // the menu-bar display, the same one `screencapture -m` takes
let win = NSWindow(contentRect: screen.frame, styleMask: .borderless, backing: .buffered, defer: false)
win.isOpaque = false; win.backgroundColor = .clear; win.hasShadow = false
win.ignoresMouseEvents = true
win.level = .screenSaver
win.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .stationary]
let view = PetView(frame: NSRect(origin: .zero, size: screen.frame.size))
win.contentView = view
win.orderFrontRegardless()

Timer.scheduledTimer(withTimeInterval: 1.0 / 60, repeats: true) { _ in view.tick() }
Timer.scheduledTimer(withTimeInterval: 0.4, repeats: true) { _ in
  URLSession.shared.dataTask(with: URL(string: "http://127.0.0.1:4330/overlay")!) { data, _, _ in
    guard let data, let j = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
          let v = (j["version"] as? NSNumber)?.intValue else { return }
    DispatchQueue.main.async {
      if let st = j["settings"] as? [String: Bool] {
        menu.update(st)
        if st["on_screen"] == false { if win.isVisible { win.orderOut(nil) } } else if !win.isVisible { win.orderFrontRegardless() }
      }
      if let m = j["mood"] as? String { view.mood = m }
      if let ev = j["event"] as? [String: Any], let name = ev["last"] as? String, let at = (ev["at"] as? NSNumber)?.doubleValue {
        if at != view.lastEventAt { view.lastEventAt = at; view.react(name) }
        if let mu = ev["music"] as? [String: Any], let bpm = (mu["bpm"] as? NSNumber)?.doubleValue {
          if view.music == nil { view.music = (bpm, view.t) }
        } else { view.music = nil }
      }
      let s = j["status"] as? String ?? ""
      if !s.isEmpty { view.status = s; view.thinking = true }
      else if view.thinking && (!view.status.isEmpty || Date().timeIntervalSince(view.thinkingSince) > 5) {
        view.thinking = false; view.status = ""  // the brain finished, failed or never got it: stop waiting
        if view.lastVersion == (j["version"] as? NSNumber)?.intValue { view.bubble = "" }
      }
      if view.lastVersion == -1 { view.lastVersion = v; return }  // don't replay an old one at start-up
      if v != view.lastVersion { view.lastVersion = v; view.start(j) }
    }
  }.resume()
}
// ---- the menu bar icon: switch Pip's bits on and off ----
final class PipMenu: NSObject {
  let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
  let menu = NSMenu()
  var current: [String: Bool] = [:]
  let switches: [(String, String)] = [("sound", "Sound"), ("on_screen", "Pet on screen"),
                                      ("auto_look", "Looks at my screen by itself"), ("fn_talk", "Hold fn to talk"),
                                      ("ring_talk", "Ring chats by itself")]
  override init() {
    super.init()
    item.button?.image = NSImage(systemSymbolName: "pawprint.fill", accessibilityDescription: "Pip")
    for (key, title) in switches {
      let m = NSMenuItem(title: title, action: #selector(toggle(_:)), keyEquivalent: "")
      m.target = self; m.representedObject = key; menu.addItem(m)
    }
    menu.addItem(.separator())
    let off = NSMenuItem(title: "Everything off", action: #selector(allOff), keyEquivalent: ""); off.target = self; menu.addItem(off)
    let on = NSMenuItem(title: "Everything on", action: #selector(allOn), keyEquivalent: ""); on.target = self; menu.addItem(on)
    let look = NSMenuItem(title: "Look at my screen now", action: #selector(lookNow), keyEquivalent: ""); look.target = self; menu.addItem(look)
    menu.addItem(.separator())
    let quit = NSMenuItem(title: "Quit Pip", action: #selector(quit), keyEquivalent: "q"); quit.target = self; menu.addItem(quit)
    item.menu = menu
  }
  func update(_ s: [String: Bool]) {
    current = s
    for m in menu.items { if let k = m.representedObject as? String { m.state = s[k] == true ? .on : .off } }
    item.button?.appearsDisabled = !s.values.contains(true)
  }
  func send(_ body: [String: Bool]) {
    var req = URLRequest(url: URL(string: "http://127.0.0.1:4330/settings")!); req.httpMethod = "POST"
    req.httpBody = try? JSONSerialization.data(withJSONObject: body)
    URLSession.shared.dataTask(with: req) { data, _, _ in
      if let data, let s = try? JSONSerialization.jsonObject(with: data) as? [String: Bool] { DispatchQueue.main.async { self.update(s) } }
    }.resume()
  }
  @objc func toggle(_ m: NSMenuItem) { if let k = m.representedObject as? String { send([k: m.state != .on]) } }
  @objc func allOff() { send(Dictionary(uniqueKeysWithValues: switches.map { ($0.0, false) })) }
  @objc func allOn() { send(Dictionary(uniqueKeysWithValues: switches.map { ($0.0, true) })) }
  @objc func lookNow() {
    var req = URLRequest(url: URL(string: "http://127.0.0.1:4330/ask")!); req.httpMethod = "POST"
    URLSession.shared.dataTask(with: req) { _, _, _ in }.resume()
  }
  @objc func quit() { NSApp.terminate(nil) }
}
let menu = PipMenu()

// ---- hold fn to talk to Pip ----
// Records the mic only while fn is held, then sends the clip (and brain.py takes one screenshot) to Gemini.
var recorder: AVAudioRecorder?
let clip = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("pip-talk.wav")
var fnDown = false, fnAt = Date()
func fnChanged(_ e: NSEvent) {
  if menu.current["fn_talk"] == false { return }
  let down = e.modifierFlags.contains(.function) && e.keyCode == 63
  if down == fnDown { return }
  fnDown = down
  if down {
    fnAt = Date()
    let s: [String: Any] = [AVFormatIDKey: kAudioFormatLinearPCM, AVSampleRateKey: 16000, AVNumberOfChannelsKey: 1,
                            AVLinearPCMBitDepthKey: 16, AVLinearPCMIsFloatKey: false]
    recorder = try? AVAudioRecorder(url: clip, settings: s)
    recorder?.record(forDuration: 60)  // never more than a minute, even if fn gets stuck
    view.listening = true; tellBrain("listen")
  } else {
    recorder?.stop(); recorder = nil; view.listening = false
    if Date().timeIntervalSince(fnAt) < 0.4 { tellBrain("quiet"); return }  // a quick tap is just fn, not talking
    guard let data = try? Data(contentsOf: clip) else { return }
    try? FileManager.default.removeItem(at: clip)
    view.thinking = true; view.thinkingSince = Date()
    var req = URLRequest(url: URL(string: "http://127.0.0.1:4330/talk")!); req.httpMethod = "POST"; req.httpBody = data
    URLSession.shared.dataTask(with: req) { _, _, _ in }.resume()
  }
}
NSEvent.addGlobalMonitorForEvents(matching: .keyDown) { e in
  if e.keyCode == 53 { view.drawn = []; view.marks = []; view.current = -1; view.bubble = ""; view.alphaAll = 0; view.fadeAt = 0 }  // Esc: clear everything
  else if view.current < 0 && view.t < view.fadeAt { view.fadeAt = min(view.fadeAt, view.t + 2) }  // you're typing: get out of the way sooner
}
func tellBrain(_ status: String) {
  var req = URLRequest(url: URL(string: "http://127.0.0.1:4330/status")!); req.httpMethod = "POST"; req.httpBody = status.data(using: .utf8)
  URLSession.shared.dataTask(with: req) { _, _, _ in }.resume()
}
AVCaptureDevice.requestAccess(for: .audio) { _ in }
NSEvent.addGlobalMonitorForEvents(matching: .flagsChanged, handler: fnChanged)
NSEvent.addLocalMonitorForEvents(matching: .flagsChanged) { fnChanged($0); return $0 }
app.run()
