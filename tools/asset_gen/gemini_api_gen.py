#!/usr/bin/env python3
"""
gemini_api_gen.py — generate the game's art through the OFFICIAL Google
Generative Language API (Imagen / Gemini image models), instead of scraping the
Gemini web UI like gemini_gen.py does.

Why this exists: the web-UI scraper works, but automating the UI runs against
Google's terms of service, which muddies the commercial license of anything you
ship. This talks to the sanctioned HTTP API with your own API key, so the
output is cleanly licensed for a paid release (Steam etc.). Same art, clean path.

ZERO third-party dependencies — pure stdlib (urllib). It reads the SAME
`manifest.json` as the web-UI script (same `id`/`kind`/`out`/`prompt`/
`wrap_prefix`/`style` fields), so nothing else in your pipeline changes; only
the transport differs. It SKIPS any asset whose output PNG already exists, so a
re-run continues where a quota cutoff stopped it.

QUICK START:
  1. Get an API key at https://aistudio.google.com/apikey  (a free key works
     for testing; enable billing on the key for the clean *commercial* tier).
  2. Export it:   export GEMINI_API_KEY=xxxx   (Windows: set GEMINI_API_KEY=xxxx)
  3. Preview:     python gemini_api_gen.py --only terrain --dry-run
  4. Generate:    python gemini_api_gen.py --only terrain
     Filters: --only hero|icon|town|collage|terrain|all   Cap: --limit N
     Redo existing: --force.   Pick model: --model / GEMINI_IMAGE_MODEL.

Model names on the Generative Language API change over time. The default targets
Imagen; if Google renames it, pass --model or set GEMINI_IMAGE_MODEL. Check the
current list at https://ai.google.dev/gemini-api/docs/models.
"""
import argparse, base64, datetime, json, os, sys, time, urllib.error, urllib.request

API_ROOT = "https://generativelanguage.googleapis.com/v1beta/models"
# Dedicated image model. Overridable via --model / GEMINI_IMAGE_MODEL when
# Google renames it. Imagen uses the :predict endpoint; a "gemini-*-image"
# model uses :generateContent — both response shapes are handled below.
DEFAULT_MODEL = "imagen-3.0-generate-002"

# Phrases in an HTTP error body that mean "you're out of quota for now" — stop
# cleanly and let a later re-run pick up the rest (mirrors the web-UI script).
QUOTA_HINTS = ("quota", "rate limit", "resource_exhausted", "exceeded")


def log(m): print(f"[{datetime.datetime.now():%H:%M:%S}] {m}", flush=True)


def load_manifest(path):
    """Identical field handling to gemini_gen.py so ONE manifest drives both."""
    with open(path, encoding="utf-8") as f:
        m = json.load(f)
    d = m.get("defaults", {})
    for a in m["assets"]:
        wrap  = a.get("wrap_prefix", d.get("wrap_prefix", ""))
        style = a.get("style",       d.get("style", ""))
        a["_full_prompt"] = (wrap + a["prompt"] + style).strip()
        a.setdefault("aspect", d.get("aspect", "1:1"))
    return m


def _post(url, body, timeout=180):
    """POST JSON, return (status, parsed_json_or_text). Never raises on HTTP
    error status — returns the error body so the caller can detect quota."""
    data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(url, data=data,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, json.loads(r.read().decode("utf-8", "replace"))
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", "replace")
        try:
            return e.code, json.loads(raw)
        except Exception:
            return e.code, raw


def _extract_png_b64(resp):
    """Pull the base64 image out of either API response shape:
       Imagen :predict  -> predictions[].bytesBase64Encoded
       Gemini :generateContent -> candidates[].content.parts[].inlineData.data
    """
    if not isinstance(resp, dict):
        return None
    for p in resp.get("predictions", []) or []:
        b = p.get("bytesBase64Encoded") or p.get("bytes_base64_encoded")
        if b:
            return b
    for c in resp.get("candidates", []) or []:
        for part in (c.get("content", {}) or {}).get("parts", []) or []:
            inline = part.get("inlineData") or part.get("inline_data") or {}
            if inline.get("data"):
                return inline["data"]
    return None


def generate_one(model, api_key, asset, out_abs, timeout):
    prompt = asset["_full_prompt"]
    is_imagen = "imagen" in model.lower()
    if is_imagen:
        url = f"{API_ROOT}/{model}:predict?key={api_key}"
        body = {"instances": [{"prompt": prompt}],
                "parameters": {"sampleCount": 1, "aspectRatio": asset.get("aspect", "1:1")}}
    else:
        url = f"{API_ROOT}/{model}:generateContent?key={api_key}"
        body = {"contents": [{"parts": [{"text": prompt}]}],
                "generationConfig": {"responseModalities": ["IMAGE"]}}

    status, resp = _post(url, body, timeout=timeout)
    if status != 200:
        blob = json.dumps(resp).lower() if isinstance(resp, dict) else str(resp).lower()
        if status == 429 or any(h in blob for h in QUOTA_HINTS):
            return "quota"
        msg = resp.get("error", {}).get("message", resp) if isinstance(resp, dict) else resp
        log(f"  HTTP {status}: {str(msg)[:200]}")
        return "fail"

    b64 = _extract_png_b64(resp)
    if not b64:
        log(f"  no image in response (keys={list(resp)[:6] if isinstance(resp, dict) else type(resp)})")
        return "fail"
    data = base64.b64decode(b64)
    if len(data) < 1024:
        log("  image suspiciously small")
        return "fail"
    os.makedirs(os.path.dirname(out_abs), exist_ok=True)
    with open(out_abs, "wb") as f:
        f.write(data)
    log(f"  saved {len(data)} bytes -> {asset['out']}")
    return "ok"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Generate game art via the official Google Generative Language API (stdlib only).")
    ap.add_argument("--manifest", default=os.path.join(here, "manifest.json"))
    ap.add_argument("--only", choices=["hero", "icon", "town", "collage", "terrain", "dwelling", "all"], default="all")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--repo-root", default=os.path.abspath(os.path.join(here, "..", "..")))
    ap.add_argument("--force", action="store_true", help="regenerate even if the output PNG already exists")
    ap.add_argument("--between", type=float, default=2.0, help="seconds to sleep between successful calls")
    ap.add_argument("--model", default=os.environ.get("GEMINI_IMAGE_MODEL", DEFAULT_MODEL))
    ap.add_argument("--api-key", default=os.environ.get("GEMINI_API_KEY") or os.environ.get("GOOGLE_API_KEY"))
    ap.add_argument("--timeout", type=float, default=180.0)
    ap.add_argument("--max-backoff", type=float, default=120.0)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    root = os.path.abspath(args.repo_root)
    man = load_manifest(args.manifest)

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
    log(f"model:     {args.model}")
    log(f"{len(pending)} asset(s) pending" + (f" (kind={args.only})" if args.only != "all" else ""))
    for a, _ in pending:
        log(f"    {a['id']:16s} {a['out']}")
    if args.dry_run:
        return 0
    if not pending:
        log("Nothing to do. (Everything exists — use --force to redo.)")
        return 0
    if not args.api_key:
        log("No API key. Set GEMINI_API_KEY (get one at https://aistudio.google.com/apikey) or pass --api-key.")
        return 2

    ok = fail = 0
    fail_streak = 0
    for i, (asset, out_abs) in enumerate(pending, 1):
        log(f"[{i}/{len(pending)}] {asset['id']}")
        try:
            result = generate_one(args.model, args.api_key, asset, out_abs, args.timeout)
        except Exception as e:
            log(f"  error: {e}")
            result = "fail"
        if result == "quota":
            log("API quota reached — stopping cleanly. Re-run later to continue.")
            break
        if result == "ok":
            ok += 1
            fail_streak = 0
            time.sleep(args.between)
        else:
            fail += 1
            fail_streak += 1
            backoff = min(args.between + fail_streak * 10, args.max_backoff)
            log(f"  backoff {backoff:.0f}s (fail streak {fail_streak})")
            time.sleep(backoff)

    log(f"Done. generated={ok}  failed={fail}. Remaining will be picked up next run.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
