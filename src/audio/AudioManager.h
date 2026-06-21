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
    void stopMusic();
    void setMusicVolume(float v) { m_musVol = v; }
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
    std::string m_currentMusic;
    float m_sfxVol = 0.7f;
    float m_musVol = 0.35f;
};
