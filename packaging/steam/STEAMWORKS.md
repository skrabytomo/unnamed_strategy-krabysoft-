# Steamworks SDK integration

The code is **already wired** — `main.cpp` calls `steam::init()` / `steam::shutdown()`
and the game loop calls `steam::runCallbacks()`. All of it lives behind
`src/platform/SteamIntegration.{h,cpp}` and compiles to **no-ops** until you turn
the SDK on, so nothing you see today depends on Steam. Flipping it on needs only
the SDK download and your App ID — no call sites change.

## Turn it on

1. **Download the Steamworks SDK** from your partner site and extract it. You want
   the folder that contains `public/` and `redistributable_bin/` (usually `sdk/`).
2. **Configure with the flags:**
   ```bash
   cmake -B build -G Ninja \
     -DUSE_STEAMWORKS=ON \
     -DSTEAMWORKS_SDK_ROOT=/path/to/steamworks/sdk \
     -DSTEAM_APP_ID=1234560          # your real App ID
   cmake --build build -j4
   ```
3. **Dev testing:** put a file named `steam_appid.txt` next to the exe
   (`build/bin/`) containing just your App ID (e.g. `1234560`). This lets the
   built game attach to your running Steam client without launching from the
   library. **Do not ship `steam_appid.txt`** — it's dev-only.
4. **Bundle the redistributable** with the game: copy
   `redistributable_bin/win64/steam_api64.dll` next to the exe (add it to
   `dist/` in `build_installer.sh` when you go live).

If any of that is missing at runtime, `steam::init()` logs a `[STEAM]` line and
returns false — the game runs fine without Steam.

## What's available now

| Call | Does |
|---|---|
| `steam::init()` / `shutdown()` | start/stop the API (handles RestartAppIfNecessary) |
| `steam::runCallbacks()` | pumps callbacks each frame (already in the loop) |
| `steam::available()` | true when Steam is live — gate Steam-only UI on it |
| `steam::unlockAchievement("ACH_ID")` | unlock an achievement (idempotent) |
| `steam::overlayActive()` | true while the Steam overlay is open |

## Wiring an achievement

Define the achievement's **API Name** in the Steamworks admin (Stats & Achievements),
then call it from the relevant game event, e.g.:

```cpp
#include "platform/SteamIntegration.h"
// ... when the player wins their first battle:
steam::unlockAchievement("ACH_FIRST_WIN");
```

That's the whole pattern — pick the events you want (first town captured, win a
campaign map, reach hero level 10, finish Conquest, …) and drop a one-liner at
each. Tell me the achievement list and I'll place the calls.

## Cloud saves

Two options, easiest first:
- **Steam Auto-Cloud** (no code): in Steamworks → Cloud, add the save path
  pattern. The game already stores saves in the OS per-user dir
  (`SDL_GetPrefPath`), so point Auto-Cloud at that folder and you're done.
- **ISteamRemoteStorage API** (code): only if you outgrow Auto-Cloud. Say the
  word and I'll add read/write helpers to the wrapper.

## The overlay

Requires no code beyond what's here, but the game must render through the normal
SDL2 GL swapchain (it does). Use `steam::overlayActive()` to auto-pause and stop
consuming input while the overlay is up — tell me and I'll add that gate to the
input loop.
