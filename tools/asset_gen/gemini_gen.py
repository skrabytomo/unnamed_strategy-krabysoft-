#!/usr/bin/env python3
"""
gemini_gen.py — drive your logged-in Gemini (in Brave) to generate the game's
missing art, one asset at a time, until you run out of Gemini image quota.

ZERO third-party dependencies. It talks to an already-running Brave over the
Chrome DevTools Protocol using only the Python standard library (raw WebSocket +
Runtime.evaluate). That sidesteps MSYS2's "externally-managed" pip entirely — no
Playwright, no venv, no installs.

It reuses your existing Google login; the script never sees your password.

QUICK START (see README.md):
  Just run it — the script launches Brave for you, using its OWN dedicated
  profile (separate from your normal Brave, no port conflict, nothing to close):
      python gemini_gen.py --only collage
  On the FIRST run a Brave window opens; sign into Gemini once (it persists in
  the dedicated profile). Filters: --only hero|icon|town|collage|terrain  Cap: --limit N
  Redo existing: --force.  Attach to your own Brave instead: --no-launch.

Skips any asset whose output PNG already exists, so re-run after a quota cutoff
to continue where you left off.

Gemini's web UI changes often. Selectors/quota-phrases live in CONFIG below; on
any failure a screenshot + HTML dump go to _debug/ so you can fix the selector.
"""
import argparse, base64, datetime, http.client, json, os, socket, struct, sys, time, urllib.request

CONFIG = {
    "gemini_url": "https://gemini.google.com/app",
    "input_selectors": [
        'div.ql-editor[contenteditable="true"]',
        'rich-textarea div[contenteditable="true"]',
        'div[contenteditable="true"][role="textbox"]',
        'textarea',
    ],
    # Locale-independent first (class / icon), English aria-label last. Your
    # Gemini UI may not be in English, so never rely on the label text alone.
    "send_selectors": [
        'button.send-button',
        'button[mattooltip*="Send" i]',
        'button[aria-label*="Send" i]',
    ],
    "quota_phrases": [
        "you've reached your limit", "you have reached your limit",
        "try again later", "come back later", "reached your daily limit",
        "limit for now", "can't generate more images", "unable to generate",
    ],
    "image_src_hints": ["googleusercontent.com", "blob:", "data:image"],
    "min_image_px": 256,
}


def log(m): print(f"[{datetime.datetime.now():%H:%M:%S}] {m}", flush=True)


# ── Minimal WebSocket client (RFC 6455, client-masked text frames) ────────────
class WS:
    def __init__(self, host, port, path):
        self.sock = socket.create_connection((host, port), timeout=15)
        key = base64.b64encode(os.urandom(16)).decode()
        req = (f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\n"
               "Upgrade: websocket\r\nConnection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n")
        self.sock.sendall(req.encode())
        self._buf = b""
        while b"\r\n\r\n" not in self._buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("WebSocket handshake closed")
            self._buf += chunk
        head, self._buf = self._buf.split(b"\r\n\r\n", 1)
        if b" 101 " not in head.split(b"\r\n", 1)[0] + b" ":
            raise RuntimeError("WebSocket handshake failed: " + head.decode("latin1", "replace"))

    def settimeout(self, t): self.sock.settimeout(t)

    def _read(self, n):
        while len(self._buf) < n:
            chunk = self.sock.recv(1 << 16)
            if not chunk:
                raise ConnectionError("WebSocket closed")
            self._buf += chunk
        out, self._buf = self._buf[:n], self._buf[n:]
        return out

    def _frame(self, opcode, payload=b""):
        mask = os.urandom(4)
        n = len(payload)
        hdr = bytearray([0x80 | opcode])
        if n < 126:
            hdr.append(0x80 | n)
        elif n < 65536:
            hdr.append(0x80 | 126); hdr += struct.pack(">H", n)
        else:
            hdr.append(0x80 | 127); hdr += struct.pack(">Q", n)
        hdr += mask
        self.sock.sendall(bytes(hdr) + bytes(b ^ mask[i & 3] for i, b in enumerate(payload)))

    def send(self, text): self._frame(0x1, text.encode("utf-8"))

    def recv(self):
        data = bytearray()
        while True:
            b0, b1 = self._read(2)
            fin, opcode = b0 & 0x80, b0 & 0x0F
            length = b1 & 0x7F
            if length == 126:
                length = struct.unpack(">H", self._read(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", self._read(8))[0]
            payload = self._read(length) if length else b""
            if opcode == 0x8:
                raise ConnectionError("WebSocket close frame")
            if opcode == 0x9:  # ping -> pong
                self._frame(0xA, payload); continue
            if opcode == 0xA:  # pong
                continue
            data += payload
            if fin:
                return data.decode("utf-8", "replace")


# ── CDP client over the WebSocket ─────────────────────────────────────────────
class CDP:
    def __init__(self, ws): self.ws = ws; self._id = 0

    def call(self, method, params=None, timeout=30):
        self._id += 1
        mid = self._id
        self.ws.settimeout(timeout + 15)
        self.ws.send(json.dumps({"id": mid, "method": method, "params": params or {}}))
        deadline = time.time() + timeout + 12
        while time.time() < deadline:
            msg = json.loads(self.ws.recv())
            if msg.get("id") == mid:
                if "error" in msg:
                    raise RuntimeError(f"{method}: {msg['error']}")
                return msg.get("result", {})
        raise TimeoutError(method)

    def evaluate(self, expression, await_promise=False, timeout=30):
        r = self.call("Runtime.evaluate",
                      {"expression": expression, "awaitPromise": await_promise,
                       "returnByValue": True}, timeout=timeout)
        if "exceptionDetails" in r:
            raise RuntimeError("JS: " + r["exceptionDetails"].get("text", "exception"))
        return r.get("result", {}).get("value")


def http_json(port, path):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    c.request("GET", path)
    r = c.getresponse()
    body = r.read()
    c.close()
    return json.loads(body)


def port_up(port):
    try:
        http_json(port, "/json")
        return True
    except Exception:
        return False


BRAVE_CANDIDATES = [
    r"C:\Program Files\BraveSoftware\Brave-Browser\Application\brave.exe",
    r"C:\Program Files (x86)\BraveSoftware\Brave-Browser\Application\brave.exe",
    os.path.expandvars(r"%LOCALAPPDATA%\BraveSoftware\Brave-Browser\Application\brave.exe"),
    "/usr/bin/brave-browser", "/usr/bin/brave",
]


def find_brave(explicit):
    if explicit:
        return explicit if os.path.exists(explicit) else None
    for p in BRAVE_CANDIDATES:
        if p and os.path.exists(p):
            return p
    return None


def ensure_brave(port, brave_path, profile, allow_launch):
    """Return True with Brave listening on `port`. Auto-launches a DEDICATED
    profile instance if needed — separate from the user's normal Brave, so
    there's no port conflict and nothing to close. Login persists in `profile`,
    so you only sign into Gemini once. Returns (ok, just_launched)."""
    if port_up(port):
        return True, False
    if not allow_launch:
        return False, False
    brave = find_brave(brave_path)
    if not brave:
        log("Brave not found. Pass --brave <path to brave.exe>, or use --no-launch and start it yourself.")
        return False, False
    os.makedirs(profile, exist_ok=True)
    import subprocess
    argv = [brave, f"--remote-debugging-port={port}", f"--user-data-dir={profile}",
            "--no-first-run", "--no-default-browser-check", "--start-maximized",
            "https://gemini.google.com/app"]
    flags = 0x00000008 | 0x00000200 if os.name == "nt" else 0  # DETACHED_PROCESS|NEW_PROCESS_GROUP
    log(f"Launching Brave (dedicated automation profile) on port {port}…")
    try:
        subprocess.Popen(argv, creationflags=flags,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, close_fds=True)
    except Exception as e:
        log(f"  launch failed: {e}")
        return False, False
    for _ in range(40):
        if port_up(port):
            log("  Brave debug port is up.")
            return True, True
        time.sleep(1)
    log("  Brave didn't expose the debug port in time.")
    return False, False


def js_call(func_body, *args):
    """Wrap a JS arrow/function source and call it with json-encoded args."""
    return "(" + func_body + ")(" + ",".join(json.dumps(a) for a in args) + ")"


# ── page-side JS ──────────────────────────────────────────────────────────────
JS_TYPE = """
(sels, promptText) => {
  let el = null;
  for (const s of sels) { const e = document.querySelector(s); if (e) { el = e; break; } }
  if (!el) return "NO_INPUT";
  el.focus();
  try { document.execCommand('selectAll', false, null);
        document.execCommand('insertText', false, promptText); }
  catch (e) { el.textContent = promptText; }
  el.dispatchEvent(new InputEvent('input', { bubbles: true }));
  return "TYPED";
}
"""

# Submit the typed prompt. Locale-independent: prefer class / send-icon /
# submit-type, fall back to a full synthetic Enter sequence on the editor.
JS_SEND = """
(inputSels, sendSels) => {
  const vis = e => e && e.offsetParent !== null && !e.disabled &&
                   e.getAttribute('aria-disabled') !== 'true';
  for (const s of sendSels) { const b = document.querySelector(s); if (vis(b)) { b.click(); return "CLICK:"+s; } }
  for (const b of document.querySelectorAll('button[type="submit"]')) if (vis(b)) { b.click(); return "SUBMIT"; }
  // Any visible button carrying a "send" material icon (locale-independent).
  for (const b of document.querySelectorAll('button')) {
    if (!vis(b)) continue;
    const ic = b.querySelector('mat-icon, svg, i');
    const tag = ((ic && (ic.getAttribute('fonticon') || ic.getAttribute('data-mat-icon-name') ||
                 ic.textContent)) || '') + ' ' + (b.className || '');
    if (/send/i.test(tag)) { b.click(); return "ICON"; }
  }
  let el = null;
  for (const s of inputSels) { const e = document.querySelector(s); if (e) { el = e; break; } }
  if (el) {
    el.focus();
    for (const t of ['keydown','keypress','keyup'])
      el.dispatchEvent(new KeyboardEvent(t, {key:'Enter', code:'Enter', keyCode:13, which:13, bubbles:true, cancelable:true}));
    return "ENTER";
  }
  return "NO_SEND";
}
"""

# Current text in the prompt box (empty string once Gemini accepts the submit).
JS_INPUT_TEXT = """
(sels) => { for (const s of sels) { const e = document.querySelector(s);
  if (e) return (e.innerText || e.value || '').trim(); } return null; }
"""

JS_SNAPSHOT = """
() => Array.from(document.querySelectorAll('img'))
        .map(e => e.currentSrc || e.src).filter(Boolean)
"""

JS_WAIT_IMAGE = """
(before, hints, minpx, quota, maxMs) => new Promise(resolve => {
  const seen = new Set(before);
  const t0 = Date.now();
  const tick = () => {
    const body = (document.body.innerText || '').toLowerCase();
    if (quota.some(q => body.includes(q))) return resolve("__QUOTA__");
    for (const img of document.querySelectorAll('img')) {
      const src = img.currentSrc || img.src;
      if (!src || seen.has(src)) continue;
      if (!hints.some(h => src.includes(h))) continue;
      const w = img.naturalWidth || img.width, h = img.naturalHeight || img.height;
      if (w >= minpx && h >= minpx) return resolve(src);
    }
    if (Date.now() - t0 > maxMs) return resolve(null);
    setTimeout(tick, 1000);
  };
  tick();
})
"""

# Primary download: read the already-decoded <img> pixels via a canvas. Works
# for Gemini's same-origin blob: images regardless of CSP/CORS (fetch is blocked
# for blobs), and yields full native-resolution PNG.
JS_EXTRACT_PNG = """
(src) => {
  const imgs = Array.from(document.querySelectorAll('img'));
  let img = imgs.find(i => (i.currentSrc || i.src) === src)
         || imgs.find(i => (i.currentSrc || i.src || '').startsWith('blob:'));
  if (!img) return "ERR:img-not-found";
  const w = img.naturalWidth, h = img.naturalHeight;
  if (!w || !h) return "ERR:img-not-loaded";
  try {
    const c = document.createElement('canvas'); c.width = w; c.height = h;
    c.getContext('2d').drawImage(img, 0, 0, w, h);
    return c.toDataURL('image/png').split(',')[1];   // strips "data:image/png;base64,"
  } catch (e) { return "ERR:" + e.message; }           // tainted (cross-origin) etc.
}
"""

# Fallback download for cross-origin https images (not blobs): in-page fetch.
JS_FETCH_B64 = """
(src) => fetch(src).then(r => r.arrayBuffer()).then(buf => {
  const by = new Uint8Array(buf); let bin = ''; const C = 0x8000;
  for (let i = 0; i < by.length; i += C) bin += String.fromCharCode.apply(null, by.subarray(i, i + C));
  return btoa(bin);
}).catch(e => "ERR:" + e.message)
"""

JS_HAS_INPUT = """
(sels) => sels.some(s => !!document.querySelector(s))
"""


def debug_dump(cdp, tag):
    dbg = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_debug")
    os.makedirs(dbg, exist_ok=True)
    stamp = f"{tag}_{datetime.datetime.now():%H%M%S}"
    try:
        shot = cdp.call("Page.captureScreenshot", {"format": "png"}, timeout=20)
        with open(os.path.join(dbg, stamp + ".png"), "wb") as f:
            f.write(base64.b64decode(shot["data"]))
    except Exception as e:
        log(f"  (screenshot failed: {e})")
    try:
        html = cdp.evaluate("document.documentElement.outerHTML")
        with open(os.path.join(dbg, stamp + ".html"), "w", encoding="utf-8") as f:
            f.write(html or "")
    except Exception:
        pass
    log(f"  (debug -> _debug/{stamp}.*)")


def save_image(cdp, src, out_abs):
    os.makedirs(os.path.dirname(out_abs), exist_ok=True)
    data = None
    if src.startswith("data:image"):
        data = base64.b64decode(src.split(",", 1)[1])
    else:
        # 1) canvas readback (handles the same-origin blob: image Gemini returns)
        b64 = cdp.evaluate(js_call(JS_EXTRACT_PNG, src), timeout=45)
        if isinstance(b64, str) and b64 and not b64.startswith("ERR:"):
            data = base64.b64decode(b64)
        else:
            # 2) in-page fetch, then 3) plain download (for cross-origin https)
            b64f = cdp.evaluate(js_call(JS_FETCH_B64, src), await_promise=True, timeout=60)
            if isinstance(b64f, str) and not b64f.startswith("ERR:"):
                data = base64.b64decode(b64f)
            elif not src.startswith("blob:"):
                try:
                    with urllib.request.urlopen(src, timeout=30) as r:
                        data = r.read()
                except Exception as e:
                    raise RuntimeError(f"canvas={b64}; fetch={b64f}; urllib={e}")
            else:
                raise RuntimeError(f"canvas={b64}; fetch={b64f}")
    if not data or len(data) < 1024:
        raise RuntimeError("downloaded image suspiciously small")
    with open(out_abs, "wb") as f:
        f.write(data)
    return len(data)


def wait_for_input(cdp, timeout=20):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            if cdp.evaluate(js_call(JS_HAS_INPUT, CONFIG["input_selectors"])):
                return True
        except Exception:
            pass
        time.sleep(0.5)
    return False


def generate_one(cdp, asset, out_abs):
    log(f"→ {asset['id']}  ->  {asset['out']}")
    cdp.call("Page.navigate", {"url": CONFIG["gemini_url"]}, timeout=30)  # fresh chat
    if not wait_for_input(cdp, 25):
        debug_dump(cdp, "no_input_" + asset["id"])
        return "fail"
    time.sleep(1.0)

    before = cdp.evaluate(JS_SNAPSHOT) or []
    if cdp.evaluate(js_call(JS_TYPE, CONFIG["input_selectors"], asset["_full_prompt"])) != "TYPED":
        debug_dump(cdp, "type_fail_" + asset["id"])
        return "fail"
    time.sleep(0.6)

    # Send, then confirm the box cleared (Gemini empties it on accept). Retry once.
    sent = False
    for attempt in range(2):
        how = cdp.evaluate(js_call(JS_SEND, CONFIG["input_selectors"], CONFIG["send_selectors"]))
        deadline = time.time() + 6
        while time.time() < deadline:
            txt = cdp.evaluate(js_call(JS_INPUT_TEXT, CONFIG["input_selectors"]))
            if txt is not None and len(txt) < 5:
                sent = True
                break
            time.sleep(0.5)
        if sent:
            log(f"  submitted (via {how}); waiting up to {asset['wait_seconds']}s for the image…")
            break
        log(f"  submit attempt {attempt+1} didn't take (via {how}); retrying…")
    if not sent:
        try:
            btns = cdp.evaluate(
                "JSON.stringify(Array.from(document.querySelectorAll('button'))"
                ".filter(b=>b.offsetParent!==null).map(b=>({al:b.getAttribute('aria-label'),"
                "cls:b.className,tt:b.getAttribute('mattooltip'),"
                "ic:(b.querySelector('mat-icon,svg,i')||{}).textContent})).slice(0,40))")
            log("  visible buttons (for selector fixing): " + (btns or "[]"))
        except Exception:
            pass
        debug_dump(cdp, "send_fail_" + asset["id"])
        return "fail"

    src = cdp.evaluate(js_call(JS_WAIT_IMAGE, before, CONFIG["image_src_hints"],
                               CONFIG["min_image_px"], CONFIG["quota_phrases"],
                               asset["wait_seconds"] * 1000),
                       await_promise=True, timeout=asset["wait_seconds"] + 20)
    if src == "__QUOTA__":
        return "quota"
    if not src:
        # Under batch load Gemini can deliver the image just after the window.
        # Grace re-check before giving up (the resume-on-rerun net still covers
        # true failures).
        log("  no image in window — grace re-check (45s)…")
        src = cdp.evaluate(js_call(JS_WAIT_IMAGE, before, CONFIG["image_src_hints"],
                                   CONFIG["min_image_px"], CONFIG["quota_phrases"], 45 * 1000),
                           await_promise=True, timeout=65)
        if src == "__QUOTA__":
            return "quota"
    if not src:
        debug_dump(cdp, "no_image_" + asset["id"])
        return "fail"
    try:
        n = save_image(cdp, src, out_abs)
        log(f"  saved {n} bytes -> {asset['out']}")
        return "ok"
    except Exception as e:
        log(f"  download failed: {e}")
        debug_dump(cdp, "dl_fail_" + asset["id"])
        return "fail"


def load_manifest(path):
    with open(path, encoding="utf-8") as f:
        m = json.load(f)
    d = m.get("defaults", {})
    for a in m["assets"]:
        a.setdefault("wait_seconds", d.get("wait_seconds", 120))
        # Per-entry wrap_prefix/style override the defaults so different asset
        # kinds can look different (dark-bust portraits vs parchment collages).
        wrap  = a.get("wrap_prefix", d.get("wrap_prefix", ""))
        style = a.get("style",       d.get("style", ""))
        a["_full_prompt"] = (wrap + a["prompt"] + style).strip()
    return m


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Generate missing game art via logged-in Gemini in Brave (stdlib only).")
    ap.add_argument("--manifest", default=os.path.join(here, "manifest.json"))
    ap.add_argument("--port", type=int, default=9222)
    ap.add_argument("--only", default="all",
                    help="manifest 'kind' to generate (e.g. terrain, dwelling, hero, capsule) or 'all'. "
                         "ANY kind you add to manifest.json works — no script edit needed.")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--repo-root", default=os.path.abspath(os.path.join(here, "..", "..")))
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--between", type=float, default=6.0)
    ap.add_argument("--brave", default=None, help="path to brave.exe (auto-detected if omitted)")
    ap.add_argument("--profile", default=os.path.join(here, ".brave-profile"),
                    help="dedicated Brave profile dir (persists your Gemini login across runs)")
    ap.add_argument("--no-launch", action="store_true",
                    help="don't auto-launch Brave; attach to one you started yourself")
    ap.add_argument("--max-backoff", type=float, default=180.0,
                    help="cap on the progressive throttle backoff after failures")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    root = os.path.abspath(args.repo_root)
    man = load_manifest(args.manifest)

    # Validate --only against the kinds actually in the manifest, so a new kind
    # needs zero code changes (just add entries) yet a typo is caught helpfully.
    kinds = sorted({a.get("kind", "?") for a in man["assets"]})
    if args.only != "all" and args.only not in kinds:
        log(f"--only '{args.only}' matches no manifest kind. Available: {', '.join(kinds)}")
        return 2

    pending = []
    for a in man["assets"]:
        if args.only != "all" and a.get("kind") != args.only:
            continue
        out_abs = os.path.join(root, a["out"].replace("/", os.sep))
        if os.path.exists(out_abs) and not args.force:
            continue
        pending.append((a, out_abs))
    if args.limit > 0:
        pending = pending[:args.limit]

    log(f"repo root: {root}")
    log(f"{len(pending)} asset(s) pending" + (f" (kind={args.only})" if args.only != "all" else ""))
    for a, _ in pending:
        log(f"    {a['id']:14s} {a['out']}")
    if args.dry_run:
        return 0
    if not pending:
        log("Nothing to do. (Everything exists — use --force to redo.)")
        return 0

    # Attach to Brave — auto-launching a dedicated-profile instance if needed
    # (no conflict with your normal Brave, nothing for you to open).
    ok, just_launched = ensure_brave(args.port, args.brave, args.profile, not args.no_launch)
    if not ok:
        log(f"Could not reach or launch Brave on port {args.port}.")
        return 2
    targets = http_json(args.port, "/json")
    pages = [t for t in targets if t.get("type") == "page" and t.get("webSocketDebuggerUrl")]
    if not pages:
        log("No page target in Brave. Open a tab (any URL) and retry.")
        return 2
    page = next((p for p in pages if "gemini.google.com" in (p.get("url") or "")), pages[0])
    ws_url = page["webSocketDebuggerUrl"]           # ws://127.0.0.1:PORT/devtools/page/ID
    _, rest = ws_url.split("://", 1)
    hostport, path = rest.split("/", 1)
    host, port = hostport.split(":")
    ws = WS(host, int(port), "/" + path)
    cdp = CDP(ws)

    cdp.call("Page.navigate", {"url": CONFIG["gemini_url"]}, timeout=30)
    # First launch of the dedicated profile isn't logged into Gemini yet — give
    # a generous window to sign in once (it persists in the profile afterwards).
    login_wait = 240 if just_launched else 30
    if just_launched:
        log("A Brave window opened. If it asks you to sign in, log into Google/Gemini "
            "there — you only do this ONCE (login persists). Waiting up to "
            f"{login_wait}s for the prompt box…")
    if not wait_for_input(cdp, login_wait):
        url = cdp.evaluate("location.href") or ""
        if "accounts.google.com" in url or "signin" in url.lower():
            log("Still on the sign-in page — finish signing in, then just re-run (no need to touch Brave).")
        else:
            log("Gemini prompt box never appeared — is the tab on gemini.google.com and loaded?")
            debug_dump(cdp, "startup")
        return 2

    ok = fail = 0
    fail_streak = 0
    for i, (asset, out_abs) in enumerate(pending, 1):
        log(f"[{i}/{len(pending)}]")
        try:
            result = generate_one(cdp, asset, out_abs)
        except Exception as e:
            log(f"  error: {e}")
            result = "fail"
        if result == "quota":
            log("Gemini quota reached — stopping cleanly. Re-run later to continue.")
            break
        if result == "ok":
            ok += 1
            fail_streak = 0
            time.sleep(args.between)
        else:
            fail += 1
            fail_streak += 1
            # Failures in a batch almost always mean throttling — Google slows
            # image gen after a burst. Back off progressively so the run can
            # recover instead of burning through the rest at 1-in-3.
            backoff = min(args.between + fail_streak * 30, args.max_backoff)
            log(f"  throttle backoff {backoff:.0f}s (fail streak {fail_streak})")
            time.sleep(backoff)

    log(f"Done. generated={ok}  failed={fail}. Remaining will be picked up next run.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
