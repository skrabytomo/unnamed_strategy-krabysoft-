#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <unordered_map>
#include <vector>

class AudioManager {
public:
    bool init();
    void shutdown();
    void playSound(const char* name);           // plays SFX once
    void playMusic(const char* name);           // loops background music
    // Register a music track by PATH only — does NOT load it into RAM. The
    // track is loaded from disk when played and freed when another starts, so
    // only ONE music track is ever resident (music was ~500MB of RAM before).
    void registerMusic(const char* name, const char* path);
    void stopMusic();
    // Music is pre-mixed into the device queue when a track starts, so a volume
    // change must re-mix and re-queue the current track to be audible before the
    // track next loops (can be minutes). Defined in .cpp; restarts current track.
    void setMusicVolume(float v);
    void setSfxVolume(float v)   { m_sfxVol = v; }
    bool loadWav(const char* name, const char* path);
    void update();   // call each frame to restart music loop when done

private:
    struct Wav { uint8_t* buf=nullptr; uint32_t len=0; SDL_AudioSpec spec={}; };
    static void convertToDevice(Wav& w, const SDL_AudioSpec& target);

    SDL_AudioDeviceID m_sfxDev = 0;
    SDL_AudioDeviceID m_musDev = 0;
    SDL_AudioSpec     m_sfxSpec{};
    SDL_AudioSpec     m_musSpec{};
    std::unordered_map<std::string, Wav> m_wavs;
    // Music tracks registered by path only (streamed on demand, not preloaded).
    std::unordered_map<std::string, std::string> m_musicPaths;
    std::string m_loadedMusicName;   // which track is currently resident in m_musicWav
    Wav         m_musicWav{};         // the single currently-loaded music track
    std::string m_currentMusic;
    float m_sfxVol = 0.7f;
    float m_musVol = 0.35f;
};
