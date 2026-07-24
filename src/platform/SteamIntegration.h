#pragma once
// Thin Steamworks wrapper.
//
// This compiles to pure NO-OPS unless the build is configured with
// -DUSE_STEAMWORKS=ON (which also needs -DSTEAMWORKS_SDK_ROOT=<sdk> and
// -DSTEAM_APP_ID=<id>). That means the game builds and runs today with ZERO
// Steam dependency, and lights up achievements / cloud / the overlay the moment
// you drop in the SDK and your App ID — no call sites change, they're already
// wired (steam::init/shutdown/runCallbacks). See packaging/steam/STEAMWORKS.md.
namespace steam {

// Call once at startup, before the main loop. Returns true only when the SDK is
// compiled in AND Steam initialised (Steam client running, correct app id).
// Always safe to call; returns false and does nothing in a no-SDK build.
bool init();

// Call once at shutdown.
void shutdown();

// Pump Steam callbacks — call once per frame while running.
void runCallbacks();

// True when Steam is compiled in and initialised. Gate Steam-only UI on this.
bool available();

// Achievements. `apiName` is the API Name you define in the Steamworks admin.
// No-op if unavailable. unlock() is idempotent (Steam ignores re-unlocks).
void unlockAchievement(const char* apiName);
void clearAchievement(const char* apiName);   // testing only

// True while the Steam overlay is open (pause the game / stop capturing input).
bool overlayActive();

} // namespace steam
