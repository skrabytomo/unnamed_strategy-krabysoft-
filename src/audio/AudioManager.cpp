#include "../core/DevLog.h"
#include "AudioManager.h"
#include <cstring>
#include <cstdio>

bool AudioManager::init()
{
    SDL_AudioSpec want{};
    want.freq     = 44100;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 2048;
    want.callback = nullptr;

    m_sfxDev = SDL_OpenAudioDevice(nullptr, 0, &want, &m_sfxSpec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (m_sfxDev == 0) {
        fprintf(stderr, "AudioManager: SFX device open failed: %s\n", SDL_GetError());
        return false;
    }

    m_musDev = SDL_OpenAudioDevice(nullptr, 0, &want, &m_musSpec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (m_musDev == 0) {
        fprintf(stderr, "AudioManager: Music device open failed: %s\n", SDL_GetError());
        SDL_CloseAudioDevice(m_sfxDev);
        m_sfxDev = 0;
        return false;
    }

    SDL_PauseAudioDevice(m_sfxDev, 0);
    SDL_PauseAudioDevice(m_musDev, 0);
    gLog("AudioManager: initialized (sfx=%u, mus=%u)\n", m_sfxDev, m_musDev);
    return true;
}

void AudioManager::shutdown()
{
    if (m_sfxDev) { SDL_CloseAudioDevice(m_sfxDev); m_sfxDev = 0; }
    if (m_musDev) { SDL_CloseAudioDevice(m_musDev); m_musDev = 0; }
    for (auto& kv : m_wavs)
        if (kv.second.buf) SDL_FreeWAV(kv.second.buf);
    m_wavs.clear();
    if (m_musicWav.buf) { SDL_FreeWAV(m_musicWav.buf); m_musicWav = Wav{}; }
}

void AudioManager::convertToDevice(Wav& w, const SDL_AudioSpec& target)
{
    if (w.buf == nullptr) return;
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, w.spec.format, w.spec.channels, w.spec.freq,
                          target.format, target.channels, target.freq) <= 0)
        return;  // no conversion needed or error
    cvt.len = static_cast<int>(w.len);
    cvt.buf = static_cast<uint8_t*>(SDL_malloc(cvt.len * cvt.len_mult));
    if (!cvt.buf) return;
    std::memcpy(cvt.buf, w.buf, w.len);
    SDL_FreeWAV(w.buf);
    w.buf = nullptr;
    if (SDL_ConvertAudio(&cvt) == 0) {
        w.buf = cvt.buf;
        w.len = static_cast<uint32_t>(cvt.len_cvt);
        w.spec = target;
    } else {
        SDL_free(cvt.buf);
    }
}

bool AudioManager::loadWav(const char* name, const char* path)
{
    Wav w;
    SDL_AudioSpec spec;
    if (!SDL_LoadWAV(path, &spec, &w.buf, &w.len)) {
        fprintf(stderr, "AudioManager: failed to load %s: %s\n", path, SDL_GetError());
        return false;
    }
    w.spec = spec;
    // Convert to sfx device spec (for SFX sounds; music uses separate device)
    if (m_sfxDev) convertToDevice(w, m_sfxSpec);
    m_wavs[name] = w;
    gLog("AudioManager: loaded '%s' from %s (%u bytes)\n", name, path, w.len);
    return true;
}

void AudioManager::playSound(const char* name)
{
    if (!m_sfxDev) return;
    auto it = m_wavs.find(name);
    if (it == m_wavs.end() || !it->second.buf) return;

    // Mix to sfx volume
    uint32_t len = it->second.len;
    uint8_t* mixed = static_cast<uint8_t*>(SDL_malloc(len));
    if (!mixed) return;
    std::memset(mixed, 0, len);
    SDL_MixAudioFormat(mixed, it->second.buf, m_sfxSpec.format, len,
                       static_cast<int>(m_sfxVol * SDL_MIX_MAXVOLUME));
    SDL_QueueAudio(m_sfxDev, mixed, len);
    SDL_free(mixed);
}

void AudioManager::registerMusic(const char* name, const char* path)
{
    // Path-only registration — no RAM cost. Loaded on demand in playMusic.
    m_musicPaths[name] = path;
}

void AudioManager::playMusic(const char* name)
{
    if (!m_musDev) return;

    // On-demand streaming-style load: if this track isn't the resident one,
    // free the old one and load the new from disk. Only ONE music track is ever
    // in RAM (previously all ~15 tracks were preloaded ~= 500MB).
    if (m_loadedMusicName != name) {
        auto pit = m_musicPaths.find(name);
        if (pit == m_musicPaths.end()) {
            // Fall back to a preloaded m_wavs entry (SFX-style) if present.
            auto it = m_wavs.find(name);
            if (it == m_wavs.end() || !it->second.buf) { m_currentMusic.clear(); return; }
        } else {
            if (m_musicWav.buf) { SDL_FreeWAV(m_musicWav.buf); m_musicWav = Wav{}; }
            SDL_AudioSpec spec;
            if (!SDL_LoadWAV(pit->second.c_str(), &spec, &m_musicWav.buf, &m_musicWav.len)) {
                fprintf(stderr, "AudioManager: failed to stream %s: %s\n",
                        pit->second.c_str(), SDL_GetError());
                m_currentMusic.clear();
                return;
            }
            m_musicWav.spec = spec;
            m_loadedMusicName = name;
        }
    }

    // Pick the buffer: streamed track if we have one, else the m_wavs fallback.
    const Wav* src = (m_loadedMusicName == name && m_musicWav.buf)
                   ? &m_musicWav : nullptr;
    if (!src) { auto it = m_wavs.find(name); if (it != m_wavs.end()) src = &it->second; }
    if (!src || !src->buf) { m_currentMusic.clear(); return; }

    SDL_ClearQueuedAudio(m_musDev);
    m_currentMusic = name;

    uint32_t len = src->len;
    uint8_t* mixed = static_cast<uint8_t*>(SDL_malloc(len));
    if (!mixed) return;
    std::memset(mixed, 0, len);
    SDL_MixAudioFormat(mixed, src->buf, m_musSpec.format, len,
                       static_cast<int>(m_musVol * SDL_MIX_MAXVOLUME));
    SDL_QueueAudio(m_musDev, mixed, len);
    SDL_free(mixed);
}

void AudioManager::stopMusic()
{
    if (m_musDev) SDL_ClearQueuedAudio(m_musDev);
    m_currentMusic.clear();
}

void AudioManager::setMusicVolume(float v)
{
    if (v == m_musVol) return;
    m_musVol = v;
    // The already-queued track was mixed at the old volume — re-queue it at the
    // new one so the slider takes effect immediately instead of on the next loop.
    if (!m_currentMusic.empty()) {
        std::string cur = m_currentMusic;
        playMusic(cur.c_str());
    }
}

void AudioManager::update()
{
    if (!m_musDev || m_currentMusic.empty()) return;
    // If music buffer nearly empty, re-queue to loop
    if (SDL_GetQueuedAudioSize(m_musDev) < 8192) {
        auto it = m_wavs.find(m_currentMusic);
        if (it == m_wavs.end() || !it->second.buf) return;
        uint32_t len = it->second.len;
        uint8_t* mixed = static_cast<uint8_t*>(SDL_malloc(len));
        if (!mixed) return;
        std::memset(mixed, 0, len);
        SDL_MixAudioFormat(mixed, it->second.buf, m_musSpec.format, len,
                           static_cast<int>(m_musVol * SDL_MIX_MAXVOLUME));
        SDL_QueueAudio(m_musDev, mixed, len);
        SDL_free(mixed);
    }
}
