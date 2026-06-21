#include "InputState.h"

void InputState::beginFrame()
{
    m_down.clear();
    m_up.clear();
    m_mouse.dx      = 0;
    m_mouse.dy      = 0;
    m_mouse.wheelY  = 0.0f;
    m_mouse.leftDown  = false;
    m_mouse.leftUp    = false;
    m_mouse.rightDown = false;
}

void InputState::handleEvent(const SDL_Event& e)
{
    switch (e.type) {
    case SDL_KEYDOWN:
        if (!e.key.repeat) {
            m_held[e.key.keysym.sym] = true;
            m_down[e.key.keysym.sym] = true;
        }
        break;
    case SDL_KEYUP:
        m_held[e.key.keysym.sym] = false;
        m_up[e.key.keysym.sym]   = true;
        break;
    case SDL_MOUSEMOTION:
        m_mouse.x  = e.motion.x;
        m_mouse.y  = e.motion.y;
        m_mouse.dx = e.motion.xrel;
        m_mouse.dy = e.motion.yrel;
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (e.button.button == SDL_BUTTON_LEFT)  { m_mouse.left   = true;  m_mouse.leftDown  = true; }
        if (e.button.button == SDL_BUTTON_RIGHT) { m_mouse.right = true; m_mouse.rightDown = true; }
        if (e.button.button == SDL_BUTTON_MIDDLE)  m_mouse.middle = true;
        break;
    case SDL_MOUSEBUTTONUP:
        if (e.button.button == SDL_BUTTON_LEFT)  { m_mouse.left   = false; m_mouse.leftUp    = true; }
        if (e.button.button == SDL_BUTTON_RIGHT)   m_mouse.right  = false;
        if (e.button.button == SDL_BUTTON_MIDDLE)  m_mouse.middle = false;
        break;
    case SDL_MOUSEWHEEL:
        m_mouse.wheelY = static_cast<float>(e.wheel.y);
        break;
    }
}

bool InputState::keyHeld(SDL_Keycode k) const {
    auto it = m_held.find(k);
    return it != m_held.end() && it->second;
}
bool InputState::keyDown(SDL_Keycode k) const {
    auto it = m_down.find(k);
    return it != m_down.end() && it->second;
}
bool InputState::keyUp(SDL_Keycode k) const {
    auto it = m_up.find(k);
    return it != m_up.end() && it->second;
}
