# Pip

A tiny AI pet that lives in an 8-LED ring on your desk, and comes out onto your Mac's screen with its own pointer and
a box of crayons.

- **On the desk:** a face in a colour ring that blinks, looks around, gets hungry and bored, sleeps at night, purrs
  when you stroke it (turn the wheel) and munches when you feed it (tap the wheel). It talks and tells the time
  through a little speaker.
- **On the laptop:** it watches which app and page you're on and changes mood: concentrating while you code,
  bopping to music, watching videos, dizzy when you flick between apps, asleep when you walk away.
- **AI:** every few minutes (or when you hold the wheel 3 s) it takes one look at your screen with Gemini, flies its
  own pointer over and draws on it: crayon stars, hearts and smileys, sometimes wild toddler marker scribbles.
- **Helpful when you're working:** it does the sums on your screen, fills in spreadsheet gaps, fixes slips in docs
  and tells you how to make slides look better, and puts the answers on your clipboard for Command-V.
- **No laptop needed:** with Wi-Fi it shows the weather on its face (sunny, rainy, snowy, stormy, cold) and, with a
  Gemini key, says something out loud in a real voice at 8:00, 12:30, 17:00 and 19:30, or when you hold the wheel 3 s.
- **Hold fn and talk to it.** It answers out loud, and can use the Ember desktop app's tools (if you have it) to
  actually do things (click, type, open, look up, calculate). It never sends, buys, deletes or touches passwords.

## Privacy

- The **microphone is only on while you hold fn** (a red dot shows on Pip), and never for more than a minute.
- Screenshots go to Gemini one at a time and are deleted straight away. Automatic looks only happen 7am-10pm while
  you're using the Mac; start with `--no-auto` to turn them off.
- Everything else (app names, window titles) stays on your Mac. The brain only listens on 127.0.0.1.

## Hardware

| Part | Pins |
|---|---|
| ESP32-C3 Super Mini | the brain of the ring pet |
| Freenove 8 RGB LED ring (IN side) | V → 5V, G → GND, S → GPIO 10 |
| Rotary encoder ("clicky wheel") | + → **3V3**, GND, CLK → 3, DT → 4, SW → 5 |
| Small speaker | GPIO 20 → 220 Ω → speaker → GPIO 21 (no amplifier needed) |

Optional: `s3/` is the same pet's face for an ESP32-S3 with a 1.47" screen (Waveshare ESP32-S3-LCD-1.47).

## Set up

```sh
# ring firmware (Arduino CLI, esp32 core 3.x, Adafruit NeoPixel)
cd ring
cp secrets.example.h ring_pet/secrets.h        # optional Wi-Fi for the clock
CLIP_RATE=16000 CLIP_LOUD=1 python3 gen_clips.py
python3 gen_wiring.py
arduino-cli compile --upload -p /dev/cu.usbmodem* \
  --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app ring_pet
python3 settime.py                              # if it has no Wi-Fi

# Mac side
mkdir -p ~/.config/pip
echo "YOUR_GEMINI_KEY" > ~/.config/pip/gemini_key
echo "Sam" > ~/.config/pip/name                 # what Pip calls you
sh mac/run.sh
```

Then: allow the microphone when asked, set **System Settings → Keyboard → Press 🌐 key to: Do Nothing**, and if
fn does nothing, switch Pip on under **Privacy & Security → Input Monitoring**.

## Controls

| Do this | Pip does |
|---|---|
| Turn the wheel | gets stroked, purrs |
| Tap the wheel | gets fed |
| Hold the wheel 1 s | tells you the time |
| Hold the wheel 3 s | looks at your screen and draws on it |
| Hold the wheel + turn | turns its face round the ring |
| Hold fn and talk | does what you ask |
| Hold BOOT 3 s | reads out the wiring |

MIT licence.
