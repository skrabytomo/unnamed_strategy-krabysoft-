#pragma once
#include <SDL2/SDL.h>
#include <unordered_map>

struct MouseState
{
    int   x = 0, y = 0;        // current screen pos
    int   dx = 0, dy = 0;      // delta this frame
    bool  left    = false;
    bool  right   = false;
    bool  middle  = false;
    bool  leftDown  = false;   // pressed this frame
    bool  leftUp    = false;   // released this frame
    bool  rightDown = false;   // pressed this frame
    float wheelY  = 0.0f;
};

class InputState
{
public:
    void beginFrame();                  // clear per-frame flags
    void handleEvent(const SDL_Event& e);

    bool keyHeld(SDL_Keycode k)    const;
    bool keyDown(SDL_Keycode k)    const; // pressed this frame
    bool keyUp(SDL_Keycode k)      const; // released this frame

    const MouseState& mouse() const { return m_mouse; }

private:
    MouseState m_mouse;

    std::unordered_map<SDL_Keycode, bool> m_held;
    std::unordered_map<SDL_Keycode, bool> m_down;
    std::unordered_map<SDL_Keycode, bool> m_up;
};
