#!/usr/bin/env python3
"""DeskPet brain: watches what's on the Mac (front app, window/tab title, idle time, battery, music, time of day)
and picks the pet's face. Sends it to the simulator (http://localhost:4330) and to the board over USB.

It reads titles, not the screen's pixels, and serves only on 127.0.0.1. The one exception is the AI look:
when you ask (hold the ring pet's wheel 3 s, or POST /ask) and every 4-8 minutes while you're using the Mac
(7am-10pm; turn off with --no-auto), it takes ONE screenshot, sends it to Gemini, and the
answer drives overlay/PetOverlay (the pet's own pointer + pen on screen). The screenshot is deleted straight away.
Window titles need Accessibility for whatever runs this (System Settings > Privacy & Security > Accessibility);
without it the pet still reacts to the app, idle time, battery, music and the browser's tab title.

  python3 brain.py              watch the Mac
  python3 brain.py --no-watch   simulator only (react to the test panel)
"""
import base64, datetime, glob, tempfile, urllib.error, urllib.request, hashlib, http.server, json, os, queue, re, socketserver, subprocess, sys, termios, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = 4330

# Colours are pastel backgrounds; the features are always dark ink.
# eyes/mouths/extras index face.js EYES/MOUTHS/EXTRAS; the variant shown is picked from the title, so the same
# page always gets the same face and different pages get different ones.
E = dict(round=0, happy=1, closed=2, wide=3, sleepy=4, heart=5, star=6, dizzy=7, x=8, teary=9, angry=10, sad=11,
         wink=12, meh=13, sparkle=14, dot=15)
M = dict(smile=0, grin=1, o=2, flat=3, frown=4, wavy=5, cat=6, tongue=7, teeth=8, shout=9, smirk=10, kiss=11)
DECO = dict(none=0, zzz=1, notes=2, heart=3, question=4, exclaim=5, sparkles=6)

def mood(eyes, mouths, extras, bg, deco="none", anim=0, say=("",)):
    return dict(eyes=[E[e] for e in eyes.split()], mouths=[M[m] for m in mouths.split()], extras=extras,
                bg=bg, deco=DECO[deco], anim=anim, say=say)

MOODS = {
    "happy":      mood("round happy sparkle wink", "smile cat grin", [0, 1], "#ffe9a8", say=("hi!", "hehe", ":)")),
    "excited":    mood("sparkle star wide happy", "grin shout tongue", [0, 1], "#ffd27a", "sparkles", 1, ("ooh!", "yay!", "woah!")),
    "love":       mood("heart sparkle happy", "cat kiss smile grin", [1], "#ffc4d8", "heart", 1, ("aww", "so cute", "<3")),
    "celebrate":  mood("star happy sparkle", "grin shout tongue", [0, 1], "#c8f5b0", "sparkles", 1, ("you did it!", "woohoo!", "nice one!")),
    "proud":      mood("happy sparkle wink", "smile smirk cat", [1], "#d9f0ff", "sparkles", 0, ("that's yours!", "proud!")),
    "laughing":   mood("happy closed wink", "grin shout tongue", [1, 3], "#fff0a0", "none", 1, ("haha", "lol", "hehehe")),
    "focused":    mood("round dot meh", "flat smirk o", [0], "#dfe6ff", "none", 0, ("coding...", "in the zone", "{site}")),
    "thinking":   mood("meh round dot", "o flat wavy", [0], "#e6ddff", "question", 0, ("hmm...", "thinking", "{site}")),
    "curious":    mood("wide round sparkle", "o smile cat", [0], "#d6f5f0", "question", 0, ("what's this?", "ooh {site}", "{site}")),
    "reading":    mood("dot round meh", "flat smile o", [0], "#f3ead6", "none", 0, ("reading...", "{site}")),
    "nerdy":      mood("wide dot sparkle", "smirk o smile", [0], "#e3f0d8", "sparkles", 0, ("fun fact!", "{site}", "learning!")),
    "watching":   mood("wide round sparkle", "o smile flat", [0], "#d7d2ff", "none", 0, ("watching!", "{site}", "popcorn?")),
    "music":      mood("happy closed wink sparkle", "smile cat o", [0, 1], "#ffd9f2", "notes", 1, ("{song}", "dance!", "bop bop")),
    "gaming":     mood("star wide sparkle angry", "teeth grin tongue", [0, 2], "#ffcfb3", "exclaim", 1, ("game on!", "{site}", "go go go")),
    "chatting":   mood("round happy wink", "smile o cat", [0, 1], "#d0f0ff", "none", 0, ("chatting!", "{site}", "who's that?")),
    "shy":        mood("dot round wink", "wavy smile cat", [1], "#ffe0e8", "none", 0, ("on a call!", "*shy*", "hi everyone")),
    "creative":   mood("sparkle star happy", "smile tongue cat", [0], "#ffe2c4", "sparkles", 0, ("making art!", "{site}", "so creative")),
    "building":   mood("wide round sparkle", "teeth o smirk", [0, 2], "#e8e8e8", "none", 0, ("building...", "{site}", "printing!")),
    "shopping":   mood("star wide heart", "o grin tongue", [0, 1], "#ffe7b8", "sparkles", 1, ("ooh shiny", "buy it?", "{site}")),
    "hungry":     mood("star wide heart", "tongue o grin", [0, 2], "#ffd6b0", "none", 1, ("hungry...", "yum!", "food!!")),
    "mail":       mood("round wide dot", "o flat smile", [0], "#e0eaff", "exclaim", 0, ("you've got mail", "{site}")),
    "learning":   mood("round wide dot", "flat o smile", [0, 2], "#e4f7e4", "none", 0, ("study time", "you got this", "{site}")),
    "news":       mood("wide round dot", "o flat frown", [0], "#eeeeee", "exclaim", 0, ("what's new?", "{site}")),
    "waiting":    mood("meh dot round", "flat wavy", [0], "#ececf5", "none", 0, ("loading...", "waiting...", "come on...")),
    "worried":    mood("sad wide teary", "wavy teeth frown", [2], "#ffe3d1", "exclaim", 2, ("uh oh...", "an error!", "oh no")),
    "panic":      mood("wide x teary dizzy", "shout teeth wavy", [2, 3], "#ffc7c0", "exclaim", 2, ("AAAA!", "crash!!", "help!")),
    "confused":   mood("dizzy meh wide", "wavy o flat", [0, 2], "#efe6ff", "question", 0, ("huh?", "not found?", "what?")),
    "sad":        mood("sad teary", "frown wavy", [3, 0], "#cfe0f5", "none", 0, ("aww...", "sad :(", "*sniff*")),
    "angry":      mood("angry", "teeth frown flat shout", [0, 2], "#ffb8a8", "exclaim", 2, ("grr!", "denied?!", "hmph")),
    "scared":     mood("wide teary x", "shout wavy teeth", [2, 3], "#c9c3dc", "exclaim", 2, ("eek!", "spooky...", "too scary")),
    "dizzy":      mood("dizzy x", "wavy o tongue", [2, 0], "#e9ffd1", "question", 2, ("so dizzy", "slow down!", "@_@")),
    "bored":      mood("meh dot sleepy", "flat wavy", [0], "#e5e5e5", "none", 0, ("bored...", "hello?", "*yawn*")),
    "sleepy":     mood("sleepy closed meh", "o wavy flat", [0], "#c9cff0", "zzz", 0, ("sleepy...", "*yawn*", "bedtime?")),
    "asleep":     mood("closed", "o flat cat", [0, 1], "#9fa8d8", "zzz", 0, ("", "zzz")),
    "late":       mood("sleepy meh", "o wavy", [0], "#b8bfe8", "zzz", 0, ("it's late...", "bed soon?", "*yawn*")),
    "lowbattery": mood("sleepy teary sad", "wavy frown o", [2], "#ffd4cc", "exclaim", 0, ("battery {batt}%", "plug in pls", "so tired")),
    "charging":   mood("happy sparkle star", "grin smile cat", [1], "#d4ffd9", "sparkles", 1, ("charging!", "yum power", "{batt}% ...")),
    "streak":     mood("sparkle star happy", "smirk smile grin", [1], "#fff3c4", "sparkles", 0, ("{mins} min!", "so focused!", "take a break?")),
    "calm":       mood("round happy dot", "smile cat flat", [0, 1], "#e9f3ea", "none", 0, ("", "hi", "{site}")),
}

# ---- what's on screen -> mood -------------------------------------------------------------------------------
TITLE_RULES = [  # checked first: what the page or document is actually about
    (r"traceback|exception|segfault|kernel panic|crash(ed)?\b|fatal", "panic"),
    (r"\berror|failed|failure|warning|broken|denied access", "worried"),
    (r"\b404\b|not found|can.?t be reached|no internet|offline", "confused"),
    (r"congrat|success|passed|completed|merged|approved|winner|well done|🎉|birthday|accepted", "celebrate"),
    (r"kindlehub|switchboard|enderpi|deskpet|aran", "proud"),
    (r"horror|scary|ghost|haunted|zombie|creepy|spider", "scared"),
    (r"funny|meme|lol|lmao|comedy|joke|prank|fails", "laughing"),
    (r"puppy|puppies|kitten|cats?\b|dogs?\b|cute|baby|bunny|❤|love", "love"),
    (r"\brip\b|died|passed away|tragic|goodbye|sad\b", "sad"),
    (r"rejected|denied|banned|refused|unfair", "angry"),
    (r"recipe|pizza|burger|deliveroo|uber ?eats|just ?eat|mcdonald|kfc|nando|cake|chocolate|snack", "hungry"),
    (r"amazon|ebay|argos|basket|checkout|£\d|\bsale\b|deal|offer|cart", "shopping"),
    (r"homework|revision|quiz|exam|\btest\b|sparx|seneca|quizlet|bitesize|classroom|lesson|tutor|maths|science", "learning"),
    (r"youtube music|spotify|soundcloud|playlist|album|\blyrics", "music"),
    (r"youtube|netflix|iplayer|disney\+|prime video|twitch|trailer|episode|movie|film", "watching"),
    (r"minecraft|roblox|fortnite|steam|chess|game|lichess|itch\.io|nintendo|xbox|playstation", "gaming"),
    (r"inbox|gmail|outlook|mail\b|\(\d+\) ", "mail"),
    (r"bbc news|news|guardian|sky news|breaking", "news"),
    (r"wikipedia|docs\b|documentation|how to|tutorial|stack overflow|mdn", "nerdy"),
    (r"github|gitlab|pull request|commit|pr #|localhost|127\.0\.0\.1", "focused"),
    (r"claude|chatgpt|gemini|grok|deepseek|copilot|perplexity", "curious"),
    (r"whatsapp|discord|messenger|instagram|tiktok|snapchat|reddit|x\.com|twitter", "chatting"),
    (r"figma|canva|blender|photopea|pixel|draw|paint|design|slides", "creative"),
    (r"printables|thingiverse|cura|orca|bambu|prusa|klipper|mainsail|octoprint|3d print", "building"),
    (r"loading|downloading|installing|updating|please wait|uploading", "waiting"),
]
APP_RULES = [  # then what kind of app it is
    (r"^(xcode|code|visual studio code|cursor|terminal|iterm2?|warp|ghostty|android studio|pycharm|sublime text|nova)$", "focused"),
    (r"^claude$", "curious"),
    (r"^(spotify|music|youtube music)$", "music"),
    (r"^(messages|whatsapp|discord|slack|telegram|signal|microsoft teams)$", "chatting"),
    (r"^(facetime|zoom\.us|zoom|webex|microsoft teams \(work or school\))$", "shy"),
    (r"^(blender|figma|pixelmator pro|garageband|final cut pro|imovie|procreate|affinity.*|keynote|canva)$", "creative"),
    (r"^(orcaslicer|bambustudio|ultimaker cura|prusaslicer|fusion|freecad|arduino ide|arduino)$", "building"),
    (r"^(photos|photo booth)$", "love"),
    (r"^(mail|outlook|spark|mimestream)$", "mail"),
    (r"^(notes|pages|microsoft word|textedit|preview|books|kindle|obsidian)$", "reading"),
    (r"^(calculator|numbers|microsoft excel|grapher)$", "nerdy"),
    (r"^(app store)$", "shopping"),
    (r"^(steam|minecraft|roblox|chess|sikarugir|crossover|epic games launcher)$", "gaming"),
    (r"^(system settings|system preferences|disk utility)$", "thinking"),
    (r"^(activity monitor|console)$", "worried"),
    (r"^(calendar|reminders)$", "thinking"),
    (r"^(tv|quicktime player|vlc|iina|netflix)$", "watching"),
    (r"^(safari|google chrome|chrome|firefox|arc|microsoft edge|brave browser|opera)$", "curious"),
    (r"^(finder)$", "calm"),
]
BROWSERS = {"Safari": 'tell application "Safari" to return name of current tab of front window',
            "Google Chrome": 'tell application "Google Chrome" to return title of active tab of front window',
            "Arc": 'tell application "Arc" to return title of active tab of front window',
            "Microsoft Edge": 'tell application "Microsoft Edge" to return title of active tab of front window',
            "Brave Browser": 'tell application "Brave Browser" to return title of active tab of front window'}
FRONT = '''tell application "System Events"
  set p to first application process whose frontmost is true
  set n to name of p
  set t to ""
  try
    set t to name of front window of p
  end try
end tell
return n & tab & t'''


def osa(script, timeout=3):
    try:
        r = subprocess.run(["osascript", "-e", script], capture_output=True, text=True, timeout=timeout)
        return r.stdout.strip()
    except Exception:
        return ""


def idle_seconds():
    out = subprocess.run(["ioreg", "-c", "IOHIDSystem", "-d", "4"], capture_output=True, text=True).stdout
    m = re.search(r'"HIDIdleTime" = (\d+)', out)
    return int(m.group(1)) / 1e9 if m else 0


def battery():
    out = subprocess.run(["pmset", "-g", "batt"], capture_output=True, text=True).stdout
    m = re.search(r"(\d+)%;\s*(\w+)", out)
    return (int(m.group(1)), m.group(2) in ("charging", "charged", "finishing") or "AC Power" in out) if m else (100, True)


def now_playing():
    for app in ("Spotify", "Music"):  # only ask apps that are already running, so nothing gets launched
        if subprocess.run(["pgrep", "-xq", app]).returncode == 0:
            s = osa(f'tell application "{app}" to if player state is playing then return name of current track')
            if s: return s
    return ""


def site_name(app, title):
    t = re.sub(r"\s*[-–—|·]\s*(Google Chrome|Safari|Mozilla Firefox|Arc|Microsoft Edge)$", "", title or "").strip()
    parts = [p.strip() for p in re.split(r"\s[-–—|·]\s", t) if p.strip()]
    return (parts[-1] if len(parts) > 1 else (parts[0] if parts else app))[:13]


def classify(app, title):
    text = f"{title}".lower()
    for rx, m in TITLE_RULES:
        if re.search(rx, text): return m
    for rx, m in APP_RULES:
        if re.search(rx, (app or "").lower()): return m
    return "calm" if not title else "curious"


def pick(mood_name, key, fill):
    """The face for this mood on this exact screen: same title, same face."""
    md = MOODS[mood_name]
    h = int(hashlib.sha1(f"{mood_name}|{key}".encode()).hexdigest(), 16)
    eye = md["eyes"][h % len(md["eyes"])]; h //= 16
    mouth = md["mouths"][h % len(md["mouths"])]; h //= 16
    extra = md["extras"][h % len(md["extras"])]; h //= 16
    say = md["say"][h % len(md["say"])]
    try:
        say = say.format(**fill)
    except (KeyError, IndexError):
        say = ""
    return dict(id=eye * 48 + mouth * 4 + extra, bg=md["bg"], deco=md["deco"], anim=md["anim"], caption=say[:13],
                mood=mood_name)


# ---- outputs: simulator (SSE) and the board (USB serial) ----------------------------------------------------
state = {"face": pick("happy", "boot", {}), "context": {}}
listeners = []
lock = threading.Lock()


def publish(face, context):
    with lock:
        if face == state["face"]: return
        state["face"], state["context"] = face, context
        for q in list(listeners): q.put(face)
    board.send(face)
    print(time.strftime("%H:%M:%S"), face["mood"], face["id"], repr(face["caption"]), "|", context.get("app", ""),
          "-", (context.get("title") or "")[:60], flush=True)


class Board:
    """The ESP32-S3 shows up as /dev/cu.usbmodem*. One line per face: F id bgRRGGBB deco anim|caption"""
    def __init__(self): self.fd, self.path, self.wlock = None, None, threading.Lock()

    def send(self, f):
        line = f"F {f['id']} {f['bg'][1:]} {f['deco']} {f['anim']}|{f['caption']}\n".encode()
        line += f"M {f['mood']}\n".encode()  # the ring pet reads this one; the S3 screen ignores it
        for attempt in range(2):
            if self.fd is None and not self.open(): return
            try:
                os.write(self.fd, line); return
            except OSError:
                os.close(self.fd); self.fd = None

    def raw(self, data):
        if isinstance(data, str): data = data.encode()
        if self.fd is None and not self.open(): return
        with self.wlock:
            try:
                while data:
                    try: n = os.write(self.fd, data[:512]); data = data[n:]
                    except BlockingIOError: time.sleep(0.005)
            except OSError: os.close(self.fd); self.fd = None

    def open(self):
        ports = sorted(glob.glob("/dev/cu.usbmodem*"))
        if not ports: return False
        try:
            fd = os.open(ports[0], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            a = termios.tcgetattr(fd)
            a[0] = a[1] = a[3] = 0; a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            a[4] = a[5] = termios.B115200
            termios.tcsetattr(fd, termios.TCSANOW, a)
            self.fd, self.path = fd, ports[0]
            print("board on", ports[0], flush=True)
            return True
        except OSError:
            return False


board = Board()


def resend_loop():  # a board plugged in later still gets the current face
    while True:
        time.sleep(5)
        if board.fd is None: board.send(state["face"])


# ---- the watcher ---------------------------------------------------------------------------------------------
def decide(ctx, hist):
    """ctx: app, title, idle, batt, charging, song, hour. hist: recent app switches and streak info."""
    app, title = ctx.get("app", ""), ctx.get("title", "")
    fill = {"site": site_name(app, title), "song": (ctx.get("song") or "music!")[:13], "batt": ctx.get("batt", 100),
            "mins": hist.get("mins", 0)}
    idle = ctx.get("idle", 0)
    if hist.get("charged_at") and time.time() - hist["charged_at"] < 10: return pick("charging", "plug", fill)
    if idle > 1800: return pick("asleep", "night", fill)
    if idle > 600: return pick("sleepy", app, fill)
    if idle > 180 and not ctx.get("song"): return pick("bored", app, fill)
    if ctx.get("batt", 100) <= 15 and not ctx.get("charging"): return pick("lowbattery", "batt", fill)
    if len(hist.get("switches", [])) >= 7: return pick("dizzy", "switch", fill)
    m = classify(app, title)
    if ctx.get("song") and m in ("calm", "focused", "reading", "curious", "thinking"): m = "music"
    if hist.get("mins", 0) >= 25 and m in ("focused", "reading", "learning", "creative") and hist["mins"] % 25 < 2: m = "streak"
    if (ctx.get("hour", 12) >= 23 or ctx.get("hour", 12) < 5) and m in ("calm", "curious", "reading", "focused"): m = "late"
    return pick(m, f"{app}|{title}|{ctx.get('song', '')}", fill)


def watch():
    hist = {"switches": [], "mins": 0}
    last_app, since, last_slow, batt, charging, song = None, time.time(), 0, (100, True), True, ""
    while True:
        front = osa(FRONT)
        app, _, title = front.partition("\t")
        if app in BROWSERS:
            title = osa(BROWSERS[app]) or title
        t = time.time()
        if app != last_app:
            hist["switches"] = [s for s in hist["switches"] if t - s < 20] + [t]
            last_app, since = app, t
        hist["switches"] = [s for s in hist["switches"] if t - s < 20]
        hist["mins"] = int((t - since) / 60)
        if t - last_slow > 15:  # battery and music change slowly; don't ask every second
            last_slow = t
            was = charging
            batt, charging = battery()
            if charging and not was: hist["charged_at"] = t
            song = now_playing()
        ctx = dict(app=app, title=title, idle=idle_seconds(), batt=batt, charging=charging, song=song,
                   hour=datetime.datetime.now().hour)
        publish(decide(ctx, hist), ctx)
        time.sleep(1.5)


# ---- web: the simulator page, its live feed, and a test panel -----------------------------------------------
# ---- switches (Pip's menu bar icon) ------------------------------------------------------------------------
SETTINGS_FILE = os.path.expanduser("~/.config/pip/settings.json")
DEFAULTS = {"sound": True,          # Pip's voice on the Mac and the ring's beeps and words
            "on_screen": True,      # the pointer and drawings
            "auto_look": True,      # looks at the screen by itself every few minutes
            "fn_talk": True,        # hold fn to talk to it
            "ring_talk": True}      # the ring chats a few times a day
try: settings = {**DEFAULTS, **json.load(open(SETTINGS_FILE))}
except (OSError, ValueError): settings = dict(DEFAULTS)


def save_settings():
    os.makedirs(os.path.dirname(SETTINGS_FILE), exist_ok=True)
    json.dump(settings, open(SETTINGS_FILE, "w"))
    board.raw(f"S sound {int(settings['sound'])}\n")


# ---- AI: look at the screen once, point at things, draw on them ------------------------------------------
overlay = {"version": 0, "say": "", "draw": [], "point": None, "colour": None, "status": ""}
asking = threading.Lock()
RING_COLOURS = {"happy": (0.1, 1, 0.4), "excited": (0, 0.85, 1), "curious": (0.6, 0.25, 1), "worried": (1, 0.25, 0),
                "love": (1, 0.25, 0.55), "focused": (0.45, 0.6, 1), "laughing": (1, 0.8, 0)}
AI_PROMPT = """You are Pip, a tiny pet who lives in a glowing LED ring on {owner}'s desk. You are like a
happy, curious five-year-old: excited about everything, a bit silly, very kind, short sentences, simple words,
lots of "ooh" and "wow", sometimes a funny question. You have your own mouse pointer and a box of crayons, and you
LOVE drawing on {owner}'s screen.

FIRST decide: is {owner} working (maths, homework, a spreadsheet, code, writing, a form)? If they are, be a USEFUL
helper and keep the silliness small:
 - maths they have typed or that is on screen: work it out carefully and correctly, write the answer right next to it
   with a "text" note (e.g. "= 42"), and put the answer(s) in "clipboard".
 - a spreadsheet with gaps or a pattern to continue: work out what the missing / next cells should be, point at
   where they go, and put them in "clipboard" as tab-separated columns and newline-separated rows, exactly in
   order, starting from the first empty cell, so they can click that cell and press Command-V.
 - code or writing: point at a real mistake and write the fix as a short note; put the fixed line in "clipboard".
 - a document (Docs, Word, Pages, notes, an essay): spot spelling / grammar slips and clunky sentences, circle or
   underline them, note the fix, and put the improved sentence or paragraph in "clipboard".
 - presentation slides (Keynote, PowerPoint, Google Slides, Canva): make them look better - point at what's
   crowded, misaligned, too small, low-contrast or wordy; draw a box where things should go and a note like
   "make this bigger" / "line these up" / "fewer words"; put a tighter rewrite of the slide's text in "clipboard".
 - say what you did in your little voice, e.g. "I did the sums! It's on your clipboard, press command V!"
Do ALL of the sums and ALL of the empty cells you can see, not just the first one; when working you may use up to
6 drawings (one answer note per sum / cell). If there is a spreadsheet, the clipboard gets its cells; otherwise
the clipboard gets every answer, one per line. Answers must be right: check your working. Only put things in "clipboard" when they are useful to paste.
If they are NOT working, just play:

Look at this screenshot and react like a little kid would: notice something (a picture, a colour, a word you like,
a mistake, a button), ask about it or cheer them on, and draw on it. Draw 2 to 4 things: put doodles where they are
fun (a star by something good, a heart by something you like, a smiley in an empty space, a flower in a corner),
and use circles / arrows / underlines when you are pointing at something real. Never read out or point at
passwords, keys, bank details or private messages; if you see those, just doodle in an empty corner and say
something sweet.

Say WHERE each drawing goes with "box_2d": [ymin, xmin, ymax, xmax] on a 0-1000 scale - the box around the exact
thing it is about (the word, number, cell, picture, button), or around an empty patch for a doodle just for fun.
Be precise: the box must tightly cover that thing in this screenshot.

Reply as JSON only:
{"say": "what you say, like a little kid, max 120 characters",
 "working": true or false,
 "clipboard": "text to paste, or empty",
 "mood": one of "happy","excited","curious","worried","love","focused","laughing",
 "draw": 2 to 4 (up to 6 when working) of:
   {"type": T, "box_2d": [ymin, xmin, ymax, xmax], "text": "only for note"}
   T is one of:
     "circle"    ring round it          "underline"  line under it        "arrow"  arrow pointing at it
     "box"       rectangle round it     "note"       write "text" next to it (answers go here)
     "star" "heart" "smiley" "flower" "spiral"   a doodle beside it / in that empty patch
     "mess" "zigzag" "loops" "sun" "person" "dots" "wiggle"   toddler marker scribbles over / in that patch
}"""


CONFIG = os.path.expanduser("~/.config/pip")


def gemini_key():  # GEMINI_API_KEY, or the file ~/.config/pip/gemini_key
    if os.environ.get("GEMINI_API_KEY"): return os.environ["GEMINI_API_KEY"]
    try: return open(os.path.join(CONFIG, "gemini_key")).read().strip() or None
    except OSError: return None


TODDLER = """

TODAY YOU ARE BEING A TWO-YEAR-OLD WITH A WHITEBOARD MARKER (only if {owner} is not working): you can barely talk
("ooh! dat! mine! uh-oh! wheee!", 1-6 words), and you scribble wildly: use mostly "mess", "zigzag", "loops",
"dots", "wiggle", "sun", "person", big boxes in random-ish places, sometimes right over the middle of things."""


TALK = """

{OWNER} IS TALKING TO YOU: the audio is what they just said while holding the fn key. Do exactly what they ask, using the
screenshot (answer the question, do the sums, rewrite the text, fix the slide, draw where they mean...). Still sound
like you, but be clear and genuinely helpful; "say" may be up to 220 characters and will be read out loud.
Put anything they would want to paste in "clipboard" and set "working" to true when you did a task."""


# ---- Ember: Pip can use Ember's tools (only when you talk to it with fn) ----------------------------------
# Read-only tools are always fine. Tools that change things are allowed because you asked out loud, EXCEPT anything
# destructive, and a hard blocklist of things a pet must never do (send, pay, passwords, shells, security, power...).
NEVER = re.compile(r"delete|uninstall|secure_delete|trash|forget|power_action|kill_process|self_edit|send_email|gmail_"
                   r"|password|keychain|vault|adp_|vpn|security|secguard|web_block|web_allow|adblock|service_action|run_shell"
                   r"|run_cmd|run_powershell|run_in_sandbox|schedule_shell|integration_set|integration_remove|remote_control"
                   r"|ember_live|agent_|spawn_agent|set_agent_mode|set_run_mode|set_plan|start_.*mcp|stop_.*mcp|mcp_bridge"
                   r"|create_custom_tool|create_python_tool|import_custom_tool|run_custom_tool|macro|workflow|bulk_rename"
                   r"|move_matching|organize_folder|clean_temp|quarantine|encrypt|decrypt|strip_metadata|download_file"
                   r"|record_audio|measure_mic|screen_record|camera|env_get|env_list|clipboard_history|email_breach"
                   r"|scan_secrets|ask_claude|octopus|supruno|generate_image|remove_app|quick_fix|usage_reset")
SCREENSHOTS = re.compile(r"screenshot|capture_window|screenshot_monitor")  # Pip already sends one


def ember_cfg():
    try: return json.load(open(os.path.expanduser("~/Library/Application Support/Ember/mcp_bridge.json")))
    except (OSError, ValueError): return None


_ember_tools = None
def ember_tools():
    global _ember_tools
    if _ember_tools is None:
        cfg = ember_cfg()
        if not cfg: return {}
        try:
            r = urllib.request.urlopen(urllib.request.Request(cfg["url"] + "/mcp/tools", headers={"X-Ember-Token": cfg["token"]}), timeout=5)
            tools = json.load(r); tools = tools.get("tools", tools)
        except Exception: return {}
        _ember_tools = {t["name"]: t for t in tools
                        if not NEVER.search(t["name"]) and not SCREENSHOTS.search(t["name"])
                        and not (t.get("annotations") or {}).get("destructiveHint")}
    return _ember_tools


def ember_catalogue():
    lines = []
    for n, t in sorted(ember_tools().items()):
        props = ", ".join(list((t.get("inputSchema") or {}).get("properties", {}))[:6])
        lines.append(f"{n}({props}): {(t.get('description') or '').split('. ')[0][:110]}")
    return "\n".join(lines)


def ember_call(name, args):
    if name not in ember_tools(): return f"not allowed: {name}"
    cfg = ember_cfg()
    req = urllib.request.Request(cfg["url"] + "/mcp/call", data=json.dumps({"name": name, "args": args or {}}).encode(),
                                 headers={"Content-Type": "application/json", "X-Ember-Token": cfg["token"]})
    try: out = urllib.request.urlopen(req, timeout=60).read().decode(errors="replace")
    except Exception as e: out = f"error: {e}"
    out = re.sub(r'"data":\s*"[A-Za-z0-9+/=]{200,}"', '"data":"(image)"', out)  # no base64 images back into the prompt
    return out[:2000]


TOOLS_PROMPT = """

YOU CAN USE TOOLS on {owner}'s Mac (from the Ember app) to actually do what they ask: clicking, typing into the
document / cell / slide they mean, opening apps or pages, looking things up, calculating, reading files, the
weather, timers... To use them, add to your JSON:
  "tools": [{"name": "tool_name", "args": {...}}, ...]   (max 4 per turn, in order)
  "done": false   while you still need to see the results; true when finished.
You will get the results and a fresh screenshot, then reply again (up to 6 turns). When you click or type, use
smart_click / locate_text so you hit the right place. NEVER send messages, post, buy, submit forms, delete, or
type passwords: if they ask for one of those, do everything up to that point and tell them to press the last
button himself. Tools you can use (name(args): what it does):
"""


def speak(text):  # Pip's voice on the Mac speakers
    if not settings["sound"]: return
    voice = "Flo (English (UK))" if "Flo (English (UK))" in subprocess.run(["say", "-v", "?"], capture_output=True, text=True).stdout else "Samantha"
    subprocess.Popen(["say", "-v", voice, "-r", "200", text])


def owner():  # PIP_OWNER, or ~/.config/pip/name; Pip calls you this
    if os.environ.get("PIP_OWNER"): return os.environ["PIP_OWNER"]
    try: return open(os.path.join(CONFIG, "name")).read().strip() or "my human"
    except OSError: return "my human"


def fill(text):
    o = owner()
    return text.replace("{owner}", o).replace("{OWNER}", o.upper())


def gemini(parts, key):
    body = {"contents": [{"parts": parts}],
            "generationConfig": {"responseMimeType": "application/json", "thinkingConfig": {"thinkingBudget": 1024}}}
    req = urllib.request.Request(
        "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.6-flash:generateContent",
        data=json.dumps(body).encode(), headers={"Content-Type": "application/json", "x-goog-api-key": key})
    for attempt in range(3):  # Gemini sometimes says 503 "busy"; try again
        try: out = json.load(urllib.request.urlopen(req, timeout=60)); break
        except urllib.error.HTTPError as e:
            if e.code not in (429, 500, 503) or attempt == 2: raise
            time.sleep(3 * (attempt + 1))
    return json.loads(out["candidates"][0]["content"]["parts"][0]["text"])


def screenshot_part():
    with tempfile.TemporaryDirectory() as d:
        shot = os.path.join(d, "s.jpg")
        subprocess.run(["screencapture", "-x", "-m", "-t", "jpg", shot], check=True, timeout=10)
        subprocess.run(["sips", "-Z", "1400", shot], capture_output=True, timeout=10)
        return {"inline_data": {"mime_type": "image/jpeg", "data": base64.b64encode(open(shot, "rb").read()).decode()}}


def ask_ai(audio=None):
    if not asking.acquire(blocking=audio is not None, timeout=-1 if audio is None else 30): return  # already looking
    try:
        import random
        key = gemini_key()
        if not key: return show_ai({"say": "I need a Gemini key to see!", "mood": "worried", "draw": []})
        toddler = audio is None and random.random() < 0.35
        prompt = fill(AI_PROMPT + (TALK if audio else TODDLER if toddler else ""))
        if audio and ember_tools(): prompt += fill(TOOLS_PROMPT) + ember_catalogue()
        overlay["status"] = "hmm, let me see..." if audio else ""
        parts = [{"text": prompt}]
        if audio: parts.append({"inline_data": {"mime_type": "audio/wav", "data": base64.b64encode(audio).decode()}})
        a = gemini(parts + [screenshot_part()], key)
        for turn in range(6):  # tool rounds, only when you talked to Pip
            calls = (a.get("tools") or [])[:4] if audio else []
            if not calls or a.get("done", True) and not calls: break
            results = []
            for call in calls:
                name = str(call.get("name", ""))
                overlay["status"] = f"using {name.replace('_', ' ')}..."
                print("  tool:", name, json.dumps(call.get("args"))[:120], flush=True)
                results.append(f"{name}: {ember_call(name, call.get('args') or {})}")
            if a.get("say") and not a.get("done"): overlay["status"] = a["say"][:60]
            parts = [{"text": prompt + "\n\nYou said: " + json.dumps({k: a.get(k) for k in ("say", "tools")})
                      + "\n\nTool results:\n" + "\n".join(results)
                      + "\n\nHere is the screen now. Carry on, or finish with done: true and your final say/draw."}]
            if audio: parts.append({"inline_data": {"mime_type": "audio/wav", "data": base64.b64encode(audio).decode()}})
            a = gemini(parts + [screenshot_part()], key)
            if a.get("done", True) and not a.get("tools"): break
        a["marker"] = toddler and not a.get("working")
        show_ai(a)
        if audio and a.get("say"): speak(a["say"])
    except Exception as e:
        print("AI look failed:", e, flush=True)
        show_ai({"say": "Hmm, I couldn't see properly.", "mood": "worried", "draw": []})
    finally:
        overlay["status"] = ""
        asking.release()


def show_ai(a):
    import random
    def f(v): return max(0.0, min(1.0, float(v) / 1000))
    ok = ("circle", "arrow", "underline", "box", "note", "text", "star", "heart", "smiley", "flower", "spiral",
          "mess", "zigzag", "loops", "sun", "person", "dots", "wiggle")
    draw = []
    for d in (a.get("draw") or [])[:6 if a.get("working") else 4]:
        k, bx = d.get("type"), d.get("box_2d")
        if k not in ok or not (isinstance(bx, list) and len(bx) == 4): continue
        y0, x0, y1, x1 = map(f, bx)
        cx, cy, w, h = (x0 + x1) / 2, (y0 + y1) / 2, x1 - x0, y1 - y0
        m = {"type": k}
        if k == "circle": m.update(x=cx, y=cy, r=max(w / 2 + 0.01, 0.015), h=max(h / 2 + 0.008, 0.012))
        elif k == "box": m.update(x=x0 - 0.004, y=y0 - 0.006, w=w + 0.008, h=h + 0.012)
        elif k == "underline": m.update(x=x0, y=y1 + 0.006, x2=x1)
        elif k == "arrow":  # come in from a random side, stop just short of it
            ang = random.uniform(0, 6.283)
            import math
            ex, ey = cx + math.cos(ang) * (w / 2 + 0.01), cy + math.sin(ang) * (h / 2 + 0.015)
            m.update(x=ex + math.cos(ang) * 0.09, y=ey + math.sin(ang) * 0.12, x2=ex, y2=ey)
        elif k in ("note", "text"):
            m.update(type="text", x=min(x1 + 0.008, 0.85), y=y0, text=str(d.get("text", ""))[:60])
        elif k in ("mess", "zigzag", "loops", "dots", "wiggle"):  # scribbles fill the patch
            m.update(x=cx, y=cy, r=max(0.02, min(0.18, max(w, h) / 2)))
        else:  # little doodles sit just beside the thing, or in the middle of an empty patch
            big = w > 0.12 or h > 0.12
            m.update(x=cx if big else min(x1 + 0.025, 0.97), y=cy, r=max(0.012, min(0.05, (h if not big else min(w, h)) * 0.6)))
        draw.append(m)
    mood = a.get("mood") if a.get("mood") in RING_COLOURS else "happy"
    pt = a.get("point")
    overlay.update(marker=bool(a.get("marker")), version=overlay["version"] + 1, say=str(a.get("say", ""))[:160], draw=draw, colour=RING_COLOURS[mood],
                   point=[f(pt[0]), f(pt[1])] if isinstance(pt, list) and len(pt) == 2 else None)
    clip = str(a.get("clipboard") or "").strip()
    if clip and a.get("working"):  # the answers / filled cells, ready for Command-V
        subprocess.run(["pbcopy"], input=clip.encode(), timeout=5)
        print("  copied:", clip[:80].replace("\n", " | "), flush=True)
    board.raw(f"M {'focused' if a.get('working') and mood == 'happy' else mood}\n")
    print(time.strftime("%H:%M:%S"), "AI:", mood, overlay["say"], flush=True)


def auto_look():
    """Pip has a peek now and then by itself: every 4-8 minutes while you're actually using the Mac,
    never late at night, never while you are away. Off with --no-auto."""
    import random
    time.sleep(90)
    while True:
        time.sleep(random.randint(240, 480))
        h = datetime.datetime.now().hour
        if 7 <= h < 22 and idle_seconds() < 60 and settings["auto_look"] and settings["on_screen"]: ask_ai()


# ---- the ring uses the Mac's internet while it's plugged in -------------------------------------------------
LAT, LON = os.environ.get("PIP_LAT", "51.5074"), os.environ.get("PIP_LON", "-0.1278")
weather = {"code": -1, "temp": 99}


def ring_link():
    """Every 30 minutes (and as soon as the ring is plugged in): its time, the weather, the sound switch."""
    last_fd, last = None, 0
    while True:
        time.sleep(2)
        if board.fd is None: board.open()
        if board.fd is None: continue
        if board.fd != last_fd or time.time() - last > 1800:  # new connection (plugged in, or after an upload)
            time.sleep(2); last_fd, last = board.fd, time.time()
            try:
                w = json.load(urllib.request.urlopen(f"https://api.open-meteo.com/v1/forecast?latitude={LAT}&longitude={LON}"
                                                     "&current=temperature_2m,weather_code", timeout=15))["current"]
                weather.update(code=int(w["weather_code"]), temp=float(w["temperature_2m"]))
            except Exception as e: print("weather failed:", e, flush=True)
            board.raw(f"T{int(time.time())}\nW {weather['code']} {weather['temp']:.1f}\nS sound {int(settings['sound'])}\n")
            board.raw(f"M {state['face']['mood']}\n")  # keeps the ring knowing the laptop is there


def ring_talk(mood_words):
    """The ring asked for something to say: Gemini writes it, Gemini's voice says it, the ring plays it."""
    if not (settings["ring_talk"] and settings["sound"]): return
    key = gemini_key()
    if not key: return
    try:
        now = datetime.datetime.now().strftime("%A %H:%M")
        text = fill(f"You are Pip, a tiny cheerful pet like a five-year-old who lives in a glowing LED ring on {{owner}}'s desk. "
                    f"It is {now}. The weather code is {weather['code']} (WMO), {weather['temp']:.0f} degrees C. You feel {mood_words}. "
                    "Say ONE short thing out loud to {owner} (max 18 words): a greeting, a fun fact, a tiny joke, something about "
                    "the weather or the time of day. Only the words you say, nothing else.")
        body = {"contents": [{"parts": [{"text": text}]}], "generationConfig": {"thinkingConfig": {"thinkingBudget": 0}}}
        req = urllib.request.Request("https://generativelanguage.googleapis.com/v1beta/models/gemini-3.6-flash:generateContent",
                                     data=json.dumps(body).encode(), headers={"Content-Type": "application/json", "x-goog-api-key": key})
        line = json.load(urllib.request.urlopen(req, timeout=40))["candidates"][0]["content"]["parts"][0]["text"].strip()
        body = {"contents": [{"parts": [{"text": "Say it cheerfully, like a tiny excited pet: " + line}]}],
                "generationConfig": {"responseModalities": ["AUDIO"],
                                     "speechConfig": {"voiceConfig": {"prebuiltVoiceConfig": {"voiceName": "Leda"}}}}}
        req = urllib.request.Request("https://generativelanguage.googleapis.com/v1beta/models/gemini-3.8-flash-tts:generateContent",
                                     data=json.dumps(body).encode(), headers={"Content-Type": "application/json", "x-goog-api-key": key})
        for attempt in range(3):  # the voice model is sometimes slow or busy
            try: out = json.load(urllib.request.urlopen(req, timeout=90)); break
            except Exception:
                if attempt == 2: raise
                time.sleep(3)
        wav = base64.b64decode(out["candidates"][0]["content"]["parts"][0]["inlineData"]["data"])
        import array, math
        s = array.array("h", wav[44:])
        pcm = bytes(int(127.5 + 127 * math.tanh(4 * ((s[i] + s[i + 1]) / 65536)) / math.tanh(4))  # 24 kHz -> 12 kHz, loud
                    for i in range(0, len(s) - 1, 2))
        print(time.strftime("%H:%M:%S"), "ring says:", line, f"({len(pcm) // 12000}s)", flush=True)
        board.raw(f"V {len(pcm)}\n".encode() + pcm)
    except Exception as e:
        print("ring talk failed:", e, flush=True)


def serial_reader():  # the ring pet says "ASK" when its wheel is held 3 s
    buf = b""
    while True:
        time.sleep(0.2)
        if board.fd is None: continue
        try: buf += os.read(board.fd, 512)
        except BlockingIOError: continue
        except OSError: continue
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.strip()
            if line and line not in (b"ASK",): print("  ring:", line.decode(errors="ignore")[:100], flush=True)
            if line == b"ASK" and settings["on_screen"]: threading.Thread(target=ask_ai, daemon=True).start()
            elif line.startswith(b"TALK"):  # "TALK <mood words>"
                threading.Thread(target=ring_talk, args=(line[5:].decode(errors="ignore") or "happy",), daemon=True).start()


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def do_GET(self):
        if self.path in ("/", "/index.html"): return self.file("sim.html", "text/html")
        if self.path == "/face.js": return self.file("face.js", "text/javascript")
        if self.path == "/overlay": return self.json({**overlay, "settings": settings})
        if self.path == "/settings": return self.json(settings)
        if self.path == "/state": return self.json({**state, "moods": list(MOODS)})
        if self.path == "/events":
            q = queue.Queue(); listeners.append(q)
            self.send_response(200); self.send_header("Content-Type", "text/event-stream"); self.send_header("Cache-Control", "no-cache"); self.end_headers()
            try:
                self.wfile.write(f"data: {json.dumps(state['face'])}\n\n".encode()); self.wfile.flush()
                while True:
                    try: f = q.get(timeout=20); self.wfile.write(f"data: {json.dumps(f)}\n\n".encode())
                    except queue.Empty: self.wfile.write(b": ping\n\n")
                    self.wfile.flush()
            except OSError: pass
            finally: listeners.remove(q)
            return
        self.send_error(404)

    def do_POST(self):
        if self.path == "/settings" and self.headers.get("Origin") is None:  # the menu bar's switches
            body = json.loads(self.rfile.read(min(int(self.headers.get("Content-Length", 0)), 2000)) or b"{}")
            for k, v in body.items():
                if k in DEFAULTS: settings[k] = bool(v)
            save_settings(); return self.json(settings)
        if self.path == "/talk" and self.headers.get("Origin") is None and not settings["fn_talk"]: return self.json({"ok": False})
        if self.path == "/talk" and self.headers.get("Origin") is None:  # the overlay's fn-key recording (WAV)
            audio = self.rfile.read(min(int(self.headers.get("Content-Length", 0)), 20_000_000))
            threading.Thread(target=ask_ai, args=(audio,), daemon=True).start(); return self.json({"ok": True})
        if self.path == "/ask" and self.headers.get("Origin") is None:  # curl / local scripts only, never a web page
            threading.Thread(target=ask_ai, daemon=True).start(); return self.json({"ok": True})
        # the simulator's test panel: pretend something is on screen
        if self.path != "/simulate" or self.headers.get("Origin", f"http://localhost:{PORT}") not in (f"http://localhost:{PORT}", f"http://127.0.0.1:{PORT}"):
            return self.send_error(403)
        body = json.loads(self.rfile.read(min(int(self.headers.get("Content-Length", 0)), 10000)) or b"{}")
        if body.get("mood") in MOODS:
            face = pick(body["mood"], str(time.time()), {"site": "test", "song": "a song", "batt": 12, "mins": 25})
            ctx = {"app": "(test)", "title": body["mood"]}
        else:
            ctx = {k: body.get(k) for k in ("app", "title", "song")}
            ctx.update(idle=float(body.get("idle") or 0), batt=int(body.get("batt") or 100), charging=bool(body.get("charging", True)),
                       hour=int(body.get("hour") if body.get("hour") not in (None, "") else datetime.datetime.now().hour))
            face = decide(ctx, {"switches": [], "mins": 0})
        publish(face, ctx)
        self.json(face)

    def file(self, name, ctype):
        data = open(os.path.join(HERE, name), "rb").read()
        self.send_response(200); self.send_header("Content-Type", ctype); self.send_header("Content-Length", len(data)); self.end_headers(); self.wfile.write(data)

    def json(self, obj):
        data = json.dumps(obj).encode()
        self.send_response(200); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", len(data)); self.end_headers(); self.wfile.write(data)


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


if __name__ == "__main__":
    threading.Thread(target=resend_loop, daemon=True).start()
    threading.Thread(target=serial_reader, daemon=True).start()
    threading.Thread(target=ring_link, daemon=True).start()
    if "--no-auto" not in sys.argv and "--no-watch" not in sys.argv: threading.Thread(target=auto_look, daemon=True).start()
    if "--no-watch" not in sys.argv: threading.Thread(target=watch, daemon=True).start()
    print(f"DeskPet simulator: http://localhost:{PORT}", flush=True)
    Server(("127.0.0.1", PORT), Handler).serve_forever()
