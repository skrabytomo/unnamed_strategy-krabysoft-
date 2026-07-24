#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string>
#include "core/Game.h"
#include "data/ContentExport.h"
#include "version_gen.h"   // GAME_GIT_HASH / GAME_GIT_BRANCH / GAME_BUILD_DATE (build-time)

#ifndef GAME_VERSION
#define GAME_VERSION "0.0.0"
#endif

int main(int argc, char* argv[])
{
    // First line of every run's log: exactly which build produced it.
    gLog("[VERSION] Unnamed Strategy v%s  (%s @ %s, built %s)\n",
         GAME_VERSION, GAME_GIT_BRANCH, GAME_GIT_HASH, GAME_BUILD_DATE);
    // Dev/test hook: --watch-ai-test[=N[:S[:Z]]] skips the menu, runs a
    // hidden (never-shown, no-focus-steal) window, and drops straight into
    // an N-player Watch AI game so AI-vs-AI/alliance behaviour can be
    // verified from gLog output alone. N defaults to 6. S is map shape:
    // 0=Hexagon (default), 1=JebusCross, 2=JebusCross3, 3=Ring. Z is map
    // size: 0=Small (default) .. 3=XLarge.
    // --seed=N forces the world seed (worldgen, factions, personalities and
    // combat RNGs all derive from it) so any run — watch-AI test or normal
    // game — is reproducible. Without it every run gets a time-based seed;
    // either way the chosen seed is logged as [SEED] at game start.
    // --export-content=DIR dumps the hardcoded content registries (buildings,
    // units, factions, resources, terrain, asset inventory) to JSON in DIR and
    // exits. Runs before SDL init so it works headless / in CI. This is how
    // other front-ends get the balance data without a second copy of it.
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--export-content=", 0) == 0) {
            std::string dir = arg.substr(17);
            if (dir.empty()) dir = "export";
            return exportContentJson(dir) ? 0 : 1;
        }
    }

    bool     watchAiTest  = false;
    int      watchPlayers = 6;
    int      watchShape   = 0;
    int      watchSize    = 0;
    bool     seedSet      = false;
    uint32_t seedVal      = 0;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--seed=", 0) == 0) {
            seedSet = true;
            seedVal = static_cast<uint32_t>(strtoul(arg.c_str() + 7, nullptr, 10));
        } else if (arg.rfind("--watch-ai-test", 0) == 0) {
            watchAiTest = true;
            auto eq = arg.find('=');
            if (eq != std::string::npos) {
                std::string val = arg.substr(eq + 1);
                auto c1 = val.find(':');
                if (c1 != std::string::npos) {
                    watchPlayers = atoi(val.substr(0, c1).c_str());
                    std::string rest = val.substr(c1 + 1);
                    auto c2 = rest.find(':');
                    if (c2 != std::string::npos) {
                        watchShape = atoi(rest.substr(0, c2).c_str());
                        watchSize  = atoi(rest.substr(c2 + 1).c_str());
                    } else {
                        watchShape = atoi(rest.c_str());
                    }
                } else {
                    watchPlayers = atoi(val.c_str());
                }
            }
        }
    }

    Game game;
    if (!game.init("Unnamed Strategy", 1280, 720, watchAiTest)) {
        fprintf(stderr, "Failed to initialize game\n");
        return 1;
    }
    if (seedSet)     game.setForcedSeed(seedVal);
    if (watchAiTest) game.autoStartWatchAI(watchPlayers, watchShape, watchSize);
    game.run();
    game.shutdown();
    return 0;
}
