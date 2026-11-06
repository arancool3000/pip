// ocr: prints every line of text in an image with its position, so Pip's drawings snap onto the real words.
//   ocr <image>   ->   one JSON object per line: {"text": "...", "x": 0..1, "y": 0..1, "w": 0..1, "h": 0..1}  (top-left origin)
import Foundation
import Vision
import AppKit

guard CommandLine.arguments.count > 1, let img = NSImage(contentsOfFile: CommandLine.arguments[1]),
      let cg = img.cgImage(forProposedRect: nil, context: nil, hints: nil) else { exit(1) }
let req = VNRecognizeTextRequest()
req.recognitionLevel = .fast
req.usesLanguageCorrection = false
try? VNImageRequestHandler(cgImage: cg, options: [:]).perform([req])
for o in req.results ?? [] {
  guard let top = o.topCandidates(1).first else { continue }
  let b = o.boundingBox  // normalised, origin bottom-left
  let d: [String: Any] = ["text": top.string, "x": b.minX, "y": 1 - b.maxY, "w": b.width, "h": b.height]
  if let j = try? JSONSerialization.data(withJSONObject: d), let s = String(data: j, encoding: .utf8) { print(s) }
}
