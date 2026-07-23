#!/usr/bin/env bash
# Upload a build to Steam via SteamPipe (steamcmd).
#
# It (1) makes sure the self-contained dist/ folder exists (building it if not),
# (2) fills the App/Depot IDs into the .vdf templates, and (3) runs steamcmd to
# upload. The build is NOT set live — you promote it from the Steamworks site
# after verifying, so this is safe to run.
#
# Prerequisites (all one-time, from your Steamworks partner account):
#   - A Steamworks app  ->  its App ID and Depot ID (Depot ID is usually App+1)
#   - steamcmd installed and on PATH   (https://developer.valvesoftware.com/wiki/SteamCMD)
#   - A Steam builder account with Edit App Metadata + publish rights
#
# Usage:
#   export STEAM_APP_ID=1234560
#   export STEAM_DEPOT_ID=1234561
#   export STEAM_ACCOUNT=your_builder_login
#   ./packaging/steam/upload_steam.sh
#
# Optional:
#   SKIP_BUILD=1   reuse the existing dist/ instead of rebuilding
#   PREVIEW=1      dry run — build the depot locally but DON'T upload
set -euo pipefail
cd "$(dirname "$0")/../.."           # repo root
STEAM_DIR="packaging/steam"

: "${STEAM_APP_ID:?set STEAM_APP_ID (from your Steamworks dashboard)}"
: "${STEAM_DEPOT_ID:?set STEAM_DEPOT_ID (usually App ID + 1)}"
: "${STEAM_ACCOUNT:?set STEAM_ACCOUNT (your Steam builder login)}"

if ! command -v steamcmd >/dev/null 2>&1; then
    echo "!! steamcmd not found on PATH."
    echo "   Install it: https://developer.valvesoftware.com/wiki/SteamCMD"
    exit 1
fi

if [[ "${SKIP_BUILD:-0}" != "1" || ! -d dist ]]; then
    echo "== Building self-contained dist/ =="
    ./packaging/build_installer.sh
fi
[[ -f dist/unnamed_strategy.exe ]] || { echo "!! dist/unnamed_strategy.exe missing — build failed?"; exit 1; }

# Fill the templates into a scratch dir (originals stay tokenised in git).
OUT="$STEAM_DIR/.generated"
mkdir -p "$OUT" "$STEAM_DIR/output"
PREVIEW_VAL="${PREVIEW:-0}"
for f in app_build depot_build; do
    sed -e "s/__APP_ID__/$STEAM_APP_ID/g" \
        -e "s/__DEPOT_ID__/$STEAM_DEPOT_ID/g" \
        -e "s/\"preview\"\t\"0\"/\"preview\"\t\"$PREVIEW_VAL\"/" \
        "$STEAM_DIR/$f.vdf" > "$OUT/$f.vdf"
done

ABS_APP_BUILD="$(cd "$OUT" && pwd)/app_build.vdf"
echo "== Uploading to Steam (app $STEAM_APP_ID, depot $STEAM_DEPOT_ID, preview=$PREVIEW_VAL) =="
echo "   (build will NOT be set live — promote it from the Steamworks site)"
steamcmd +login "$STEAM_ACCOUNT" +run_app_build "$ABS_APP_BUILD" +quit

echo ""
echo "Done. Next: Steamworks -> your app -> Builds -> set the new build live on a branch."
