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
  var marker = false, listening = false, thinking = false, status = ""
  var current = -1, progress = 0.0, bubble = "", bubbleAt = 0.0, lastVersion = -1, fadeAt = 0.0, alphaAll = 1.0
  var t = 0.0, blinkAt = 0.0

  override var isFlipped: Bool { true }

  func P(_ x: Double, _ y: Double) -> CGPoint { CGPoint(x: x * bounds.width, y: y * bounds.height) }

  func start(_ j: [String: Any]) {
    marks = ((j["draw"] as? [[String: Any]]) ?? []).compactMap { d in
      guard let k = d["type"] as? String else { return nil }
      func n(_ s: String) -> Double { (d[s] as? NSNumber)?.doubleValue ?? 0 }
      return Mark(kind: k, x: n("x"), y: n("y"), x2: n("x2"), y2: n("y2"), r: n("r"), w: n("w"), h: n("h"), text: d["text"] as? String ?? "",
                  crayon: ((j["marker"] as? Bool) == true ? MARKERS : CRAYONS).randomElement()!)
    }
    marker = (j["marker"] as? Bool) == true; thinking = false
    drawn = []; current = marks.isEmpty ? -1 : 0; progress = 0; alphaAll = 1
    bubble = j["say"] as? String ?? ""; bubbleAt = t
    if let c = j["colour"] as? [Double], c.count == 3 { colour = NSColor(calibratedRed: c[0], green: c[1], blue: c[2], alpha: 1) }
    if let p = j["point"] as? [Double], p.count == 2 { petTarget = P(p[0], p[1]) }
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
        let a = Double(i) / 60 * 2.15 * .pi, wob = 1 + 0.04 * sin(a * 3)
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
    if home == .zero { home = CGPoint(x: bounds.width - 90, y: bounds.height - 120); pet = home; petTarget = home }
    if current >= 0 && current < marks.count {
      let m = marks[current]
      let d = hypot(pet.x - startPoint(m).x, pet.y - startPoint(m).y)
      if progress == 0 && d > 6 { petTarget = startPoint(m) }  // fly there first
      else {
        progress = min(1, progress + (m.kind == "text" ? 0.05 : marker ? 0.04 : 0.025) * Double.random(in: 0.6...1.4))
        if m.kind != "text" { petTarget = path(m, progress).1 } else { petTarget = P(m.x, m.y) }
        if progress >= 1 { drawn.append((m, 1)); current += 1; progress = 0; if current >= marks.count { current = -1 } }
      }
    } else if t > fadeAt {
      if Int(t * 60) % 240 == 0 { home = CGPoint(x: bounds.width - 90 - Double.random(in: 0...160), y: bounds.height - 110 - Double.random(in: 0...90)) }
      petTarget = home
      alphaAll = max(0, alphaAll - 0.02)
      if alphaAll == 0 { drawn = []; bubble = "" }
    }
    if listening || thinking {
      let mouse = NSEvent.mouseLocation, f = win.frame
      petTarget = CGPoint(x: mouse.x - f.minX + 34, y: f.height - (mouse.y - f.minY) + 30)
      bubble = listening ? "I'm listening!" : (status.isEmpty ? "hmm, let me see..." : status); bubbleAt = t - 10; alphaAll = 1; fadeAt = t + 60
    }
    // fly: quick but smooth, with a little bob when it's resting
    let k = current >= 0 && progress > 0 ? 0.6 : 0.12
    pet.x += (petTarget.x - pet.x) * k; pet.y += (petTarget.y - pet.y) * k
    needsDisplay = true
  }

  override func draw(_ dirty: NSRect) {
    NSColor.clear.set(); dirty.fill()
    func stroke(_ p: NSBezierPath, _ col: NSColor) {
      if marker {
        p.lineWidth = 11; p.lineCapStyle = .round; p.lineJoinStyle = .round
        col.withAlphaComponent(0.72 * alphaAll).set(); p.stroke(); return
      }
      p.lineWidth = 6; p.lineCapStyle = .round; p.lineJoinStyle = .round
      NSColor.black.withAlphaComponent(0.3 * alphaAll).set(); let s = p.copy() as! NSBezierPath; s.lineWidth = 9; s.stroke()
      col.withAlphaComponent(0.92 * alphaAll).set(); p.stroke()
      let wax = p.copy() as! NSBezierPath; wax.lineWidth = 2  // crayon texture: a lighter streak down the middle
      NSColor.white.withAlphaComponent(0.3 * alphaAll).set(); wax.stroke()
    }
    func note(_ m: Mark, _ f: Double) {
      let text = String(m.text.prefix(Int(Double(m.text.count) * f)))
      let font = NSFont(name: "Chalkboard SE Bold", size: 26) ?? NSFont.systemFont(ofSize: 24, weight: .heavy)
      let attrs: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: m.crayon.withAlphaComponent(alphaAll),
        .strokeColor: NSColor.black.withAlphaComponent(0.6 * alphaAll), .strokeWidth: -3]
      (text as NSString).draw(at: P(m.x, m.y), withAttributes: attrs)
    }
    for (m, _) in drawn { m.kind == "text" ? note(m, 1) : stroke(path(m, 1).0, m.crayon) }
    if current >= 0 && current < marks.count && progress > 0 {
      let m = marks[current]; m.kind == "text" ? note(m, progress) : stroke(path(m, progress).0, m.crayon)
    }

    // the pet pointer: a little glowing ring with eyes, like the real one
    let bob = current < 0 ? sin(t * 2.5) * 3 : 0
    let c = CGPoint(x: pet.x, y: pet.y + bob), R = 17.0
    colour.withAlphaComponent(0.25).set(); NSBezierPath(ovalIn: NSRect(x: c.x - R - 8, y: c.y - R - 8, width: 2 * R + 16, height: 2 * R + 16)).fill()
    NSColor(white: 0.08, alpha: 0.92).set(); NSBezierPath(ovalIn: NSRect(x: c.x - R, y: c.y - R, width: 2 * R, height: 2 * R)).fill()
    let rim = NSBezierPath(ovalIn: NSRect(x: c.x - R, y: c.y - R, width: 2 * R, height: 2 * R)); rim.lineWidth = 3; colour.set(); rim.stroke()
    if t > blinkAt + 4 + Double.random(in: 0...3) { blinkAt = t }
    let blink = t - blinkAt < 0.12
    let look = CGPoint(x: (petTarget.x - pet.x) / 60, y: (petTarget.y - pet.y) / 60)
    let lx = max(-3, min(3, look.x)), ly = max(-3, min(3, look.y))
    NSColor.white.set()
    for dx in [-6.0, 6.0] {
      let e = NSRect(x: c.x + dx - 3.5 + lx, y: c.y - 4 + ly - (blink ? -3 : 0), width: 7, height: blink ? 1.5 : 8)
      NSBezierPath(roundedRect: e, xRadius: 3, yRadius: 3).fill()
    }
    // red dot = the mic is on (only ever while fn is held)
    if listening { NSColor.systemRed.set(); NSBezierPath(ovalIn: NSRect(x: c.x + 10, y: c.y - 22, width: 10, height: 10)).fill() }
    // the pen nib when it's drawing
    if current >= 0 && current < marks.count && progress > 0 { marks[current].crayon.set(); NSBezierPath(ovalIn: NSRect(x: c.x + 12, y: c.y + 12, width: 7, height: 7)).fill() }

    // speech bubble
    if !bubble.isEmpty && alphaAll > 0 {
      let shown = String(bubble.prefix(Int((t - bubbleAt) * 40)))
      let attrs: [NSAttributedString.Key: Any] = [.font: NSFont(name: "Chalkboard SE", size: 17) ?? NSFont.systemFont(ofSize: 16), .foregroundColor: NSColor.white.withAlphaComponent(alphaAll)]
      let box = (shown as NSString).boundingRect(with: NSSize(width: 300, height: 400), options: [.usesLineFragmentOrigin], attributes: attrs)
      var o = CGPoint(x: c.x + 28, y: c.y - box.height - 30)
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
      let s = j["status"] as? String ?? ""
      if !s.isEmpty { view.status = s; view.thinking = true } else if !view.status.isEmpty { view.status = "" }
      if view.lastVersion == -1 { view.lastVersion = v; return }  // don't replay an old one at start-up
      if v != view.lastVersion { view.lastVersion = v; view.start(j) }
    }
  }.resume()
}
// ---- hold fn to talk to Pip ----
// Records the mic only while fn is held, then sends the clip (and brain.py takes one screenshot) to Gemini.
var recorder: AVAudioRecorder?
let clip = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("pip-talk.wav")
var fnDown = false, fnAt = Date()
func fnChanged(_ e: NSEvent) {
  let down = e.modifierFlags.contains(.function) && e.keyCode == 63
  if down == fnDown { return }
  fnDown = down
  if down {
    fnAt = Date()
    let s: [String: Any] = [AVFormatIDKey: kAudioFormatLinearPCM, AVSampleRateKey: 16000, AVNumberOfChannelsKey: 1,
                            AVLinearPCMBitDepthKey: 16, AVLinearPCMIsFloatKey: false]
    recorder = try? AVAudioRecorder(url: clip, settings: s)
    recorder?.record(forDuration: 60)  // never more than a minute, even if fn gets stuck
    view.listening = true
  } else {
    recorder?.stop(); recorder = nil; view.listening = false
    if Date().timeIntervalSince(fnAt) < 0.4 { return }  // a quick tap is just fn, not talking
    guard let data = try? Data(contentsOf: clip) else { return }
    try? FileManager.default.removeItem(at: clip)
    view.thinking = true
    var req = URLRequest(url: URL(string: "http://127.0.0.1:4330/talk")!); req.httpMethod = "POST"; req.httpBody = data
    URLSession.shared.dataTask(with: req) { _, _, _ in }.resume()
  }
}
AVCaptureDevice.requestAccess(for: .audio) { _ in }
NSEvent.addGlobalMonitorForEvents(matching: .flagsChanged, handler: fnChanged)
NSEvent.addLocalMonitorForEvents(matching: .flagsChanged) { fnChanged($0); return $0 }
app.run()
