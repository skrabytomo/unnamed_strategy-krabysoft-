#pragma once
#include <cmath>
#include <cstdint>

// Deterministic seeded 2D gradient noise (Perlin-style), no external deps.
class Noise2D
{
public:
    explicit Noise2D(uint32_t seed = 0) : m_seed(seed) {}

    // Single octave, returns approximately [-1, 1]
    float sample(float x, float y) const
    {
        int ix = static_cast<int>(std::floor(x));
        int iy = static_cast<int>(std::floor(y));
        float fx = x - ix;
        float fy = y - iy;

        float ux = fade(fx);
        float uy = fade(fy);

        float n00 = grad(ix,   iy,   fx,       fy      );
        float n10 = grad(ix+1, iy,   fx - 1.f, fy      );
        float n01 = grad(ix,   iy+1, fx,       fy - 1.f);
        float n11 = grad(ix+1, iy+1, fx - 1.f, fy - 1.f);

        return lerp(lerp(n00, n10, ux), lerp(n01, n11, ux), uy);
    }

    // Fractal Brownian Motion — sums octaves of noise
    float fbm(float x, float y, int octaves = 5,
              float lacunarity = 2.0f, float gain = 0.5f) const
    {
        float v = 0.0f, amp = 1.0f, freq = 1.0f, maxV = 0.0f;
        for (int i = 0; i < octaves; ++i) {
            v    += amp * sample(x * freq, y * freq);
            maxV += amp;
            amp  *= gain;
            freq *= lacunarity;
        }
        return v / maxV;
    }

    // Ridged noise — good for mountains and ridgelines
    float ridged(float x, float y, int octaves = 4) const
    {
        float v = 0.0f, amp = 1.0f, freq = 1.0f, maxV = 0.0f;
        for (int i = 0; i < octaves; ++i) {
            float s = 1.0f - std::abs(sample(x * freq, y * freq));
            v    += amp * s * s;
            maxV += amp;
            amp  *= 0.5f;
            freq *= 2.0f;
        }
        return v / maxV;
    }

private:
    static float fade(float t) { return t*t*t*(t*(t*6.f-15.f)+10.f); }
    static float lerp(float a, float b, float t) { return a + t*(b-a); }

    float grad(int ix, int iy, float fx, float fy) const
    {
        static constexpr float gx[] = { 1,-1, 1,-1, 0, 0, 1,-1 };
        static constexpr float gy[] = { 1, 1,-1,-1, 1,-1, 0, 0 };
        uint32_t h = hash(ix, iy) & 7u;
        return gx[h]*fx + gy[h]*fy;
    }

    uint32_t hash(int x, int y) const
    {
        uint32_t h = m_seed;
        h ^= static_cast<uint32_t>(x) * 2654435761u;
        h ^= static_cast<uint32_t>(y) * 2246822519u;
        h ^= (h >> 16); h *= 0x45d9f3bu; h ^= (h >> 16);
        return h;
    }

    uint32_t m_seed;
};
