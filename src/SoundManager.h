#ifndef SOUNDMANAGER_H
#define SOUNDMANAGER_H

#include <Arduino.h>
#include <vector>
#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <driver/i2s.h>

// --- CONFIG ---
#define USE_I2S_AUDIO  // Comment out to use internal DAC (Pin 26)
// --------------

#ifdef USE_I2S_AUDIO
  // GPIO 21 is LCD Backlight (Causes flickering!)
  // GPIO 35 is Input Only (Cannot be BCLK!)
  // Recommended CYD I2S Pins:
  #define I2S_BCLK  27 // P3/CN1 Header (Side)
  #define I2S_LRCK  1  // TX Pin on UART/P1 Header (Connector Pin!)
  #define I2S_DOUT  3  // RX Pin on UART/P1 Header (Connector Pin!)
  #define I2S_NUM   I2S_NUM_0
#endif

enum SoundType {
    SOUND_DOWNBEAT,
    SOUND_BEAT
};

struct AudioBuffer {
    uint8_t* data = nullptr; // Stores 16-bit signed samples (cast to int16_t*) if I2S, else 8-bit unsigned
    size_t size = 0;         // Size in bytes
    uint32_t sampleRate = 44100;
    uint16_t channels = 1;
    uint16_t bitsPerSample = 16; // 16 for I2S, 8 for DAC
};

class SoundManager {
public:
    SoundManager();
    bool begin();
    std::vector<String> listWavs();
    bool selectSound(SoundType type, String filename);
    bool loadSound(SoundType type, String fullPath);
    void playDownbeat();
    void playBeat();
    void setVolume(uint8_t vol);
    bool areSoundsLoaded() { return downbeat.data != nullptr && beat.data != nullptr; }
    
    String getDownbeatPath() { return currentDownbeatPath; }
    String getBeatPath() { return currentBeatPath; }

    // Called by timer interrupt (DAC Mode only)
    void IRAM_ATTR handleInterrupt();

private:
    Preferences prefs;
    AudioBuffer downbeat;
    AudioBuffer beat;
    
    String currentDownbeatPath;
    String currentBeatPath;

    uint8_t volume = 255; // 0-255
    AudioBuffer* currentBuffer = nullptr;
    volatile size_t playIndex = 0;
    volatile bool playing = false;
    
    bool loadWavToBuffer(String path, AudioBuffer& buffer);
    bool isValidWav(String path);
    
    #ifdef USE_I2S_AUDIO
    void playI2S(AudioBuffer* buffer);
    #endif
};

extern SoundManager soundManager;

#endif
