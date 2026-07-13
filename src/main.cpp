#include <SDL2/SDL.h>
#include <stdio.h>
#include <string>
#include "core/Game.h"

int main(int argc, char* argv[])
{
    // Dev/test hook: --watch-ai-test[=N[:S[:Z]]] skips the menu, runs a
    // hidden (never-shown, no-focus-steal) window, and drops straight into
    // an N-player Watch AI game so AI-vs-AI/alliance behaviour can be
    // verified from gLog output alone. N defaults to 6. S is map shape:
    // 0=Hexagon (default), 1=JebusCross, 2=JebusCross3, 3=Ring. Z is map
    // size: 0=Small (default) .. 3=XLarge.
    bool watchAiTest  = false;
    int  watchPlayers = 6;
    int  watchShape   = 0;
    int  watchSize    = 0;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--watch-ai-test", 0) == 0) {
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
    if (watchAiTest) game.autoStartWatchAI(watchPlayers, watchShape, watchSize);
    game.run();
    game.shutdown();
    return 0;
}
