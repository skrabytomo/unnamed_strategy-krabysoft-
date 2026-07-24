#include "SteamIntegration.h"

#ifdef USE_STEAMWORKS
// ── Real implementation — only compiled with -DUSE_STEAMWORKS=ON ──────────────
#include <steam/steam_api.h>
#include "../core/DevLog.h"

#ifndef STEAM_APP_ID
#define STEAM_APP_ID 0
#endif

namespace {
bool g_ready = false;
}

namespace steam {

bool init()
{
    // In a dev tree, a `steam_appid.txt` (containing STEAM_APP_ID) next to the
    // exe makes this return false (no relaunch). In a real Steam install it
    // relaunches the game through Steam so the API can attach.
    if (SteamAPI_RestartAppIfNecessary(STEAM_APP_ID)) {
        gLog("[STEAM] relaunching through Steam client…\n");
        return false;
    }
    if (!SteamAPI_Init()) {
        gLog("[STEAM] SteamAPI_Init failed — is the Steam client running and "
             "steam_appid.txt present? (game continues without Steam)\n");
        return false;
    }
    g_ready = true;
    uint64 id = (SteamUser() ? SteamUser()->GetSteamID().ConvertToUint64() : 0);
    gLog("[STEAM] initialised (app id %d, user %llu)\n", (int)STEAM_APP_ID,
         (unsigned long long)id);
    if (SteamUserStats()) SteamUserStats()->RequestCurrentStats();
    return true;
}

void shutdown()
{
    if (g_ready) { SteamAPI_Shutdown(); g_ready = false; }
}

void runCallbacks()
{
    if (g_ready) SteamAPI_RunCallbacks();
}

bool available() { return g_ready; }

void unlockAchievement(const char* apiName)
{
    if (!g_ready || !apiName) return;
    if (ISteamUserStats* s = SteamUserStats()) {
        s->SetAchievement(apiName);
        s->StoreStats();
    }
}

void clearAchievement(const char* apiName)
{
    if (!g_ready || !apiName) return;
    if (ISteamUserStats* s = SteamUserStats()) {
        s->ClearAchievement(apiName);
        s->StoreStats();
    }
}

bool overlayActive()
{
    return g_ready && SteamUtils() && SteamUtils()->IsOverlayEnabled();
}

} // namespace steam

#else
// ── No-SDK build (default) — everything is a no-op ───────────────────────────
namespace steam {
bool init()                          { return false; }
void shutdown()                      {}
void runCallbacks()                  {}
bool available()                     { return false; }
void unlockAchievement(const char*)  {}
void clearAchievement(const char*)   {}
bool overlayActive()                 { return false; }
} // namespace steam
#endif
