#pragma once

enum class GameState
{
    WorldMap,
    Combat,
    Town,
    Campaign,
    Editor,
    MainMenu,
    WatchAI,     // both sides AI-controlled; player observes
};
