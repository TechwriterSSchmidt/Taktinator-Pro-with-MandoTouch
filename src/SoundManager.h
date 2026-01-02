#ifndef SOUNDMANAGER_H
#define SOUNDMANAGER_H

#include <Arduino.h>
#include <vector>
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <Preferences.h>

enum SoundType {
    SOUND_DOWNBEAT,
    SOUND_BEAT
};

struct AudioBuffer {
    uint8_t* data = nullptr;
    size_t size = 0;
    uint32_t sampleRate = 44100;
    uint16_t channels = 1;
    uint16_t bitsPerSample = 8;
};

class SoundManager {
public:
    SoundManager();
    bool begin();
    std::vector<String> listWavsOnSD();
    bool selectSound(SoundType type, String sdFilename);
    void playDownbeat();
    void playBeat();
    void setVolume(uint8_t vol);
    bool areSoundsLoaded() { return downbeat.data != nullptr && beat.data != nullptr; }
    
    // Called by timer interrupt
    void IRAM_ATTR handleInterrupt();

private:
    Preferences prefs;
    AudioBuffer downbeat;
    AudioBuffer beat;
    
    uint8_t volume = 255; // 0-255
    AudioBuffer* currentBuffer = nullptr;
    volatile size_t playIndex = 0;
    volatile bool playing = false;
    
    bool loadWavToBuffer(String path, AudioBuffer& buffer);
    void copyFile(fs::FS &fsSource, String pathSource, fs::FS &fsDest, String pathDest);
};

extern SoundManager soundManager;

#endif
