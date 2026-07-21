#!/usr/bin/env python3
"""
gemini_gen.py — drive your logged-in Gemini (in Brave) to generate the game's
missing art, one asset at a time, until you run out of Gemini image quota.

It attaches to an ALREADY-RUNNING Brave over the Chrome DevTools Protocol, so it
reuses your existing Google login — the script never sees your password.

QUICK START (see README.md for detail):
  1) Fully quit Brave (check Task Manager — no brave.exe left).
  2) Launch Brave with remote debugging on your normal profile:
       & "C:\\Program Files\\BraveSoftware\\Brave-Browser\\Application\\brave.exe" --remote-debugging-port=9222
     Sign in to https://gemini.google.com if you aren't already.
  3) From tools/asset_gen/ run:  python gemini_gen.py
     Add  --only hero   or  --only icon   to filter,  --limit 5  to cap a run.

Nothing is destructive: it only writes new PNGs into assets/ and skips any file
that already exists (use --force to overwrite).

Gemini's web UI changes often. Every CSS/text selector lives in the CONFIG
block below — if a step stops working, update it there. On any failure the
script drops a screenshot + page HTML into tools/asset_gen/_debug/ so you can
see what the page looked like and fix the selector.
"""
import argparse, json, os, sys, time, datetime, re

# ── CONFIG: everything Gemini-UI-specific. Update here when Google changes it ──
CONFIG = {
    "gemini_url": "https://gemini.google.com/app",
    # Prompt input box (first match wins). Gemini uses a Quill contenteditable.
    "input_selectors": [
        'div.ql-editor[contenteditable="true"]',
        'rich-textarea div[contenteditable="true"]',
        'div[contenteditable="true"][role="textbox"]',
        'textarea',
    ],
    # "New chat" control, to reset context between assets (optional; falls back
    # to reloading the app URL).
    "new_chat_selectors": [
        'button[aria-label="New chat" i]',
        'a[aria-label="New chat" i]',
        'button[data-test-id="new-chat-button"]',
    ],
    # Text that means "you hit the quota / rate limit" -> stop cleanly.
    "quota_phrases": [
        "you've reached your limit",
        "you have reached your limit",
        "try again later",
        "come back later",
        "reached your daily limit",
        "limit for now",
        "can't generate more images",
        "unable to generate",
    ],
    # An <img> is treated as a generated result only if its src matches one of
    # these AND it renders larger than min_image_px. Avatars/icons are filtered.
    "image_src_hints": ["googleusercontent.com", "blob:", "data:image"],
    "min_image_px": 256,
}

REPO_ROOT = None  # resolved at runtime (two levels up from this file)


def log(msg):
    print(f"[{datetime.datetime.now():%H:%M:%S}] {msg}", flush=True)


def resolve_root(explicit):
    if explicit:
        return os.path.abspath(explicit)
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(here, "..", ".."))  # tools/asset_gen -> repo root


def load_manifest(path):
    with open(path, "r", encoding="utf-8") as f:
        m = json.load(f)
    d = m.get("defaults", {})
    for a in m["assets"]:
        a.setdefault("wait_seconds", d.get("wait_seconds", 120))
        a["_full_prompt"] = (d.get("wrap_prefix", "") + a["prompt"] + d.get("style", "")).strip()
    return m


def debug_dump(page, tag):
    dbg = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_debug")
    os.makedirs(dbg, exist_ok=True)
    stamp = f"{tag}_{datetime.datetime.now():%H%M%S}"
    try:
        page.screenshot(path=os.path.join(dbg, stamp + ".png"), full_page=True)
        with open(os.path.join(dbg, stamp + ".html"), "w", encoding="utf-8") as f:
            f.write(page.content())
        log(f"  (debug written to _debug/{stamp}.png/.html)")
    except Exception as e:
        log(f"  (debug dump failed: {e})")


def find_first(page, selectors, timeout=8000):
    """Return the first locator that becomes visible, else None."""
    deadline = time.time() + timeout / 1000.0
    while time.time() < deadline:
        for sel in selectors:
            loc = page.locator(sel).first
            try:
                if loc.count() > 0 and loc.is_visible():
                    return loc
            except Exception:
                pass
        time.sleep(0.25)
    return None


def page_has_quota_msg(page):
    try:
        body = page.inner_text("body").lower()
    except Exception:
        return False
    return any(p in body for p in CONFIG["quota_phrases"])


def snapshot_img_srcs(page):
    try:
        return set(page.eval_on_selector_all(
            "img", "els => els.map(e => e.currentSrc || e.src).filter(Boolean)"))
    except Exception:
        return set()


def wait_for_new_image(page, before_srcs, wait_seconds):
    """Poll for a NEW <img> that looks like a generated result. Returns src or None."""
    deadline = time.time() + wait_seconds
    hints = CONFIG["image_src_hints"]
    minpx = CONFIG["min_image_px"]
    while time.time() < deadline:
        if page_has_quota_msg(page):
            return "__QUOTA__"
        try:
            cand = page.evaluate(
                """(args) => {
                    const [before, hints, minpx] = args;
                    const set = new Set(before);
                    for (const img of document.querySelectorAll('img')) {
                        const src = img.currentSrc || img.src;
                        if (!src || set.has(src)) continue;
                        if (!hints.some(h => src.includes(h))) continue;
                        const w = img.naturalWidth || img.width;
                        const h = img.naturalHeight || img.height;
                        if (w >= minpx && h >= minpx) return src;
                    }
                    return null;
                }""",
                [list(before_srcs), hints, minpx])
        except Exception:
            cand = None
        if cand:
            return cand
        time.sleep(1.0)
    return None


def download_image(context, page, src, out_abs):
    os.makedirs(os.path.dirname(out_abs), exist_ok=True)
    data = None
    if src.startswith("data:image"):
        import base64
        b64 = src.split(",", 1)[1]
        data = base64.b64decode(b64)
    elif src.startswith("blob:"):
        # Fetch the blob inside the page (only place the blob URL is valid),
        # return it base64-encoded.
        b64 = page.evaluate(
            """async (url) => {
                const r = await fetch(url);
                const buf = await r.arrayBuffer();
                let bin = ''; const bytes = new Uint8Array(buf);
                for (let i=0;i<bytes.length;i++) bin += String.fromCharCode(bytes[i]);
                return btoa(bin);
            }""", src)
        import base64
        data = base64.b64decode(b64)
    else:
        # https URL — fetch with the browser context so auth cookies apply.
        resp = context.request.get(src)
        if not resp.ok:
            raise RuntimeError(f"HTTP {resp.status} fetching image")
        data = resp.body()
    if not data or len(data) < 1024:
        raise RuntimeError("downloaded image suspiciously small")
    with open(out_abs, "wb") as f:
        f.write(data)
    return len(data)


def start_new_chat(page):
    btn = find_first(page, CONFIG["new_chat_selectors"], timeout=3000)
    if btn:
        try:
            btn.click()
            time.sleep(1.5)
            return
        except Exception:
            pass
    # Fallback: reload the app to a fresh conversation.
    page.goto(CONFIG["gemini_url"], wait_until="domcontentloaded")
    time.sleep(2.0)


def generate_one(context, page, asset, out_abs):
    log(f"→ {asset['id']}  ->  {asset['out']}")
    start_new_chat(page)

    box = find_first(page, CONFIG["input_selectors"], timeout=15000)
    if not box:
        debug_dump(page, "no_input_" + asset["id"])
        raise RuntimeError("prompt input box not found (update CONFIG.input_selectors)")

    before = snapshot_img_srcs(page)
    box.click()
    box.type(asset["_full_prompt"], delay=4)
    time.sleep(0.3)
    page.keyboard.press("Enter")
    log(f"  prompt sent, waiting up to {asset['wait_seconds']}s for the image…")

    src = wait_for_new_image(page, before, asset["wait_seconds"])
    if src == "__QUOTA__":
        return "quota"
    if not src:
        debug_dump(page, "no_image_" + asset["id"])
        if page_has_quota_msg(page):
            return "quota"
        return "fail"

    try:
        n = download_image(context, page, src, out_abs)
        log(f"  saved {n} bytes -> {asset['out']}")
        return "ok"
    except Exception as e:
        log(f"  download failed: {e}")
        debug_dump(page, "dl_fail_" + asset["id"])
        return "fail"


def main():
    global REPO_ROOT
    ap = argparse.ArgumentParser(description="Generate missing game art via logged-in Gemini in Brave.")
    ap.add_argument("--manifest", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "manifest.json"))
    ap.add_argument("--port", type=int, default=9222, help="Brave remote-debugging port")
    ap.add_argument("--only", choices=["hero", "icon", "all"], default="all")
    ap.add_argument("--limit", type=int, default=0, help="max assets to generate this run (0 = no cap)")
    ap.add_argument("--repo-root", default=None, help="override repo root (default: two levels up)")
    ap.add_argument("--force", action="store_true", help="regenerate even if the output file exists")
    ap.add_argument("--between", type=float, default=3.0, help="seconds to pause between assets")
    ap.add_argument("--dry-run", action="store_true", help="list what would be generated and exit")
    args = ap.parse_args()

    REPO_ROOT = resolve_root(args.repo_root)
    man = load_manifest(args.manifest)

    pending = []
    for a in man["assets"]:
        if args.only != "all" and a.get("kind") != args.only:
            continue
        out_abs = os.path.join(REPO_ROOT, a["out"].replace("/", os.sep))
        if os.path.exists(out_abs) and not args.force:
            continue
        pending.append((a, out_abs))
    if args.limit > 0:
        pending = pending[:args.limit]

    log(f"repo root: {REPO_ROOT}")
    log(f"{len(pending)} asset(s) pending" + (f" (kind={args.only})" if args.only != "all" else ""))
    for a, _ in pending:
        log(f"    {a['id']:14s} {a['out']}")
    if args.dry_run:
        return 0
    if not pending:
        log("Nothing to do. (Everything exists — use --force to redo.)")
        return 0

    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        log("Playwright is not installed. Run  ./setup.sh  first (or: pip install playwright).")
        return 2

    ok = fail = 0
    with sync_playwright() as pw:
        try:
            browser = pw.chromium.connect_over_cdp(f"http://localhost:{args.port}")
        except Exception as e:
            log(f"Could not attach to Brave on port {args.port}: {e}")
            log("Is Brave running with  --remote-debugging-port={0}  ? (Fully quit Brave first.)".format(args.port))
            return 2

        ctx = browser.contexts[0] if browser.contexts else browser.new_context()
        page = ctx.new_page()
        page.goto(CONFIG["gemini_url"], wait_until="domcontentloaded")
        time.sleep(2.5)
        if "accounts.google.com" in page.url or "signin" in page.url.lower():
            log("Gemini is asking you to sign in — sign in in that Brave window, then re-run.")
            return 2

        for i, (asset, out_abs) in enumerate(pending, 1):
            log(f"[{i}/{len(pending)}]")
            try:
                result = generate_one(ctx, page, asset, out_abs)
            except Exception as e:
                log(f"  error: {e}")
                result = "fail"
            if result == "quota":
                log("Gemini quota reached — stopping cleanly. Re-run later to continue where you left off.")
                break
            if result == "ok":
                ok += 1
            else:
                fail += 1
            time.sleep(args.between)

        try:
            page.close()
        except Exception:
            pass

    log(f"Done. generated={ok}  failed={fail}. Remaining will be picked up on the next run.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
