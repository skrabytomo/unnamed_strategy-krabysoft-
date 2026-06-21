#include <SDL2/SDL.h>
#include <stdio.h>
#include "core/Game.h"

int main(int argc, char* argv[])
{
    Game game;
    if (!game.init("Unnamed Strategy", 1280, 720)) {
        fprintf(stderr, "Failed to initialize game\n");
        return 1;
    }
    game.run();
    game.shutdown();
    return 0;
}
