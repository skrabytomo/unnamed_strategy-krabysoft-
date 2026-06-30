#pragma once
#include <algorithm>
#include <imgui.h>

// ── Animation state ───────────────────────────────────────────────────────────
enum class AnimState : uint8_t { Idle, Attack, Hurt, Dead };

// ── Per-unit sprite animator ──────────────────────────────────────────────────
// Sprite layout — one PNG per unit (faction × tier):
//   assets/sprites/faction_F_tT.png  (F=0-8, T=1-6)
//   8 cols = frames: [idle:4][attack:2][hurt:1][dead:1]
//   1 row  = the unit (any height; full texture height used)
//
// Drop in any sprite sheet: rename to faction_F_tT.png, set TOTAL_COLS
// to the number of animation columns in your source file.

struct SpriteAnimator
{
    AnimState state    = AnimState::Idle;
    float     t        = 0.0f;
    int       frame    = 0;    // 0-3 within current state
    int       faction  = 0;    // 0-8
    int       tier     = 1;    // 1-6, selects which per-unit texture
    bool      mirror   = false; // enemy units face left
    int       numCols  = 8;    // actual frame count derived from texture dimensions

    // Column offsets — must match the source sprite sheet column layout
    static constexpr int COL_IDLE   = 0;  // cols 0-3
    static constexpr int COL_ATTACK = 4;  // cols 4-5
    static constexpr int COL_HURT   = 6;  // col  6
    static constexpr int COL_DEAD   = 7;  // col  7
    static constexpr int TOTAL_COLS = 8;  // default columns (overridden per sprite)

    void setState(AnimState s)
    {
        if (s == state) return;
        state = s; frame = 0; t = 0.0f;
    }

    void update(float dt)
    {
        int   nf  = numFrames();
        float dur = frameDuration();
        t += dt;
        if (t >= dur) {
            t -= dur;
            frame = (frame + 1) % nf;
            // Non-looping states snap back to Idle when done
            if (frame == 0 && (state == AnimState::Attack || state == AnimState::Hurt))
                state = AnimState::Idle;
        }
    }

    // Compute UV corners for the current frame.
    // Each per-unit texture is a single row; full texture height is used.
    // u0/u1 are already mirrored if mirror==true.
    void getUV(float& u0, float& v0, float& u1, float& v1) const
    {
        int col = atlasCol();
        float fw = 1.0f / numCols;

        float pu0 = col * fw;
        float pu1 = pu0 + fw;
        v0 = 0.0f;
        v1 = 1.0f;

        if (mirror) { u0 = pu1; u1 = pu0; }
        else        { u0 = pu0; u1 = pu1; }
    }

private:
    int numFrames() const
    {
        switch (state) {
        case AnimState::Idle:   return 4;
        case AnimState::Attack: return 2;
        default:                return 1;  // Hurt, Dead
        }
    }

    float frameDuration() const
    {
        switch (state) {
        case AnimState::Idle:   return 0.22f;
        case AnimState::Attack: return 0.10f;
        case AnimState::Hurt:   return 0.12f;
        default:                return 1.0f;  // Dead: hold indefinitely
        }
    }

    int atlasCol() const
    {
        int col = 0;
        switch (state) {
        case AnimState::Idle:   col = COL_IDLE   + frame; break;
        case AnimState::Attack: col = COL_ATTACK + frame; break;
        case AnimState::Hurt:   col = COL_HURT;           break;
        default:                col = COL_DEAD;            break;
        }
        return std::min(col, numCols - 1);  // clamp to actual sheet width
    }
};
