# Shipping to Steam (SteamPipe)

Steam does **not** use your `Setup.exe`. It installs and updates the game
itself, so you upload the **raw self-contained folder** (`dist/`) as a *depot*
via **SteamPipe**. Your existing `packaging/build_installer.sh` already produces
exactly that folder (exe + every runtime DLL + assets), so it doubles as the
Steam content root — nothing extra to assemble.

## What's here

| File | Role |
|---|---|
| `app_build.vdf` | build script: which app/depot, where the content is |
| `depot_build.vdf` | maps the whole `dist/` folder into the depot |
| `upload_steam.sh` | builds `dist/`, fills in your IDs, runs `steamcmd` |

The `.vdf` files are **templates** — `__APP_ID__` / `__DEPOT_ID__` get filled
from env vars by the script, so you never commit your real IDs.

## One-time setup

1. **Steamworks partner account** — pay the **$100 Steam Direct** fee per app,
   complete identity/bank/tax verification: <https://partner.steamgames.com>.
2. **Create the app** in Steamworks → note its **App ID** and **Depot ID**
   (the depot is usually App ID + 1).
3. **Install steamcmd**: <https://developer.valvesoftware.com/wiki/SteamCMD>.
4. **A builder Steam account** with publish rights on the app. First login will
   prompt for **Steam Guard** — do one interactive `steamcmd +login <acct>` by
   hand once so the token is cached.

## Each upload

```bash
export STEAM_APP_ID=1234560
export STEAM_DEPOT_ID=1234561
export STEAM_ACCOUNT=your_builder_login
./packaging/steam/upload_steam.sh
```

- `PREVIEW=1 ./packaging/steam/upload_steam.sh` — dry run (builds the depot,
  uploads nothing). Good first smoke test.
- `SKIP_BUILD=1 …` — reuse the current `dist/` instead of rebuilding.

The script **never sets a build live.** After it finishes, go to Steamworks →
your app → **Builds**, and promote the new build onto a branch (`default` for
release, or a `beta` branch to test first).

## Before you can actually release (Steam's gates)

- **Store page** must be built and approved; Steam enforces a **~30-day**
  minimum between page-live and release.
- **Coming-soon assets**: capsule images (several sizes), 5+ screenshots, a
  short trailer, description, tags. Screenshots can come straight from the game.
- **System requirements** — pull from the real deps: 64-bit Windows, OpenGL 3.3
  Core GPU, SDL2 runtime bundled. (No Mac/Linux build today; Windows-only is
  fine on Steam.)
- **Build passes Steam's review** (they run it once).

## AI-generated content — DISCLOSE IT (decided: yes)

Steam requires you to answer the **AI content questionnaire** at submission and
it shows an AI-disclosure line on the store page. The art here is
AI-assisted (Google Gemini/Imagen), so:

- Tick the **pre-generated AI content** box and describe it briefly, e.g.
  *"Some 2D art assets were generated with AI tools (Google Gemini/Imagen) and
  edited by the developer."*
- Add the same one-liner to the in-game/credits screen.
- Keep every prompt original — no trademarked names (genre etc.) in prompts or
  output. That's the part that actually matters for rights.

## Checklist

- [ ] Steamworks account + Steam Direct fee paid
- [ ] App created; App ID + Depot ID noted
- [ ] steamcmd installed; builder account logged in once (Steam Guard cached)
- [ ] `PREVIEW=1` dry run succeeds
- [ ] Real upload succeeds; build visible in Steamworks → Builds
- [ ] Store page + capsules + screenshots + trailer done
- [ ] AI content disclosed in the questionnaire + credits
- [ ] Build set live on a test branch, played end-to-end on a clean machine
