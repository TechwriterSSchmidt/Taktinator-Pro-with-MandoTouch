#include "SoundManager.h"
#include <SPI.h>

SoundManager soundManager;

hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// Forward declaration of interrupt handler
void IRAM_ATTR onTimer() {
    portENTER_CRITICAL_ISR(&timerMux);
    soundManager.handleInterrupt();
    portEXIT_CRITICAL_ISR(&timerMux);
}

SoundManager::SoundManager() {}

bool SoundManager::begin() {
    if(!LittleFS.begin(true)){
        Serial.println("LittleFS Mount Failed");
        return false;
    }
    
    prefs.begin("metronome", false);
    
    // Load saved sounds from LittleFS to RAM
    loadWavToBuffer("/downbeat.wav", downbeat);
    loadWavToBuffer("/beat.wav", beat);
    
    // Setup Timer for DAC
    // Use Timer 0, divider 80 (1MHz)
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &onTimer, true);
    
    // Default alarm (will be updated on play)
    timerAlarmWrite(timer, 1000, true); 
    timerAlarmEnable(timer);
    
    return true;
}

std::vector<String> SoundManager::listWavsOnSD() {
    std::vector<String> files;
    // Note: Caller must handle SPI conflict (disable Touch, enable SD)
    
    if(!SD.begin(5)){
        Serial.println("SD Mount Failed");
        return files;
    }
    
    File root = SD.open("/");
    if(!root){
        Serial.println("Failed to open directory");
        return files;
    }
    
    File file = root.openNextFile();
    while(file){
        if(!file.isDirectory()){
            String name = String(file.name());
            if(name.endsWith(".wav") || name.endsWith(".WAV")){
                files.push_back(name);
            }
        }
        file = root.openNextFile();
    }
    SD.end(); // Release SPI
    return files;
}

bool SoundManager::selectSound(SoundType type, String sdFilename) {
    // Note: Caller must handle SPI conflict
    if(!SD.begin(5)) return false;
    
    String destPath = (type == SOUND_DOWNBEAT) ? "/downbeat.wav" : "/beat.wav";
    
    // Copy file
    if (LittleFS.exists(destPath)) LittleFS.remove(destPath);
    
    File source = SD.open(sdFilename, "r");
    if (!source) { SD.end(); return false; }
    
    File dest = LittleFS.open(destPath, "w");
    if (!dest) { source.close(); SD.end(); return false; }
    
    uint8_t buf[512];
    while (source.available()) {
        size_t len = source.read(buf, 512);
        dest.write(buf, len);
    }
    
    dest.close();
    source.close();
    SD.end();
    
    // Reload buffer
    if (type == SOUND_DOWNBEAT) {
        loadWavToBuffer(destPath, downbeat);
    } else {
        loadWavToBuffer(destPath, beat);
    }
    
    return true;
}

bool SoundManager::loadWavToBuffer(String path, AudioBuffer& buffer) {
    if (!LittleFS.exists(path)) return false;
    File file = LittleFS.open(path, "r");
    if (!file) return false;
    
    // Simple WAV parsing
    file.seek(12); // Skip RIFF header
    
    bool foundFmt = false;
    bool foundData = false;
    
    while (file.available()) {
        char chunkID[4];
        file.read((uint8_t*)chunkID, 4);
        uint32_t chunkSize;
        file.read((uint8_t*)&chunkSize, 4);
        
        if (memcmp(chunkID, "fmt ", 4) == 0) {
            uint16_t fmtCode; file.read((uint8_t*)&fmtCode, 2);
            uint16_t channels; file.read((uint8_t*)&channels, 2);
            uint32_t sampleRate; file.read((uint8_t*)&sampleRate, 4);
            uint32_t byteRate; file.read((uint8_t*)&byteRate, 4);
            uint16_t blockAlign; file.read((uint8_t*)&blockAlign, 2);
            uint16_t bitsPerSample; file.read((uint8_t*)&bitsPerSample, 2);
            
            buffer.sampleRate = sampleRate;
            buffer.channels = channels;
            buffer.bitsPerSample = bitsPerSample;
            
            // Skip any extra fmt bytes
            if (chunkSize > 16) file.seek(file.position() + chunkSize - 16);
            foundFmt = true;
        } else if (memcmp(chunkID, "data", 4) == 0) {
            if (buffer.data) free(buffer.data);
            buffer.size = chunkSize;
            buffer.data = (uint8_t*)malloc(chunkSize);
            if (buffer.data) {
                file.read(buffer.data, chunkSize);
                foundData = true;
            }
            break; // Stop after data
        } else {
            file.seek(file.position() + chunkSize);
        }
    }
    
    file.close();
    return foundFmt && foundData;
}

void SoundManager::playDownbeat() {
    if (!downbeat.data) return;
    portENTER_CRITICAL(&timerMux);
    playing = false;
    currentBuffer = &downbeat;
    playIndex = 0;
    // Update timer: 1MHz / sampleRate
    if (downbeat.sampleRate > 0) {
        timerAlarmWrite(timer, 1000000 / downbeat.sampleRate, true);
    }
    playing = true;
    portEXIT_CRITICAL(&timerMux);
}

void SoundManager::playBeat() {
    if (!beat.data) return;
    portENTER_CRITICAL(&timerMux);
    playing = false;
    currentBuffer = &beat;
    playIndex = 0;
    if (beat.sampleRate > 0) {
        timerAlarmWrite(timer, 1000000 / beat.sampleRate, true);
    }
    playing = true;
    portEXIT_CRITICAL(&timerMux);
}

void SoundManager::handleInterrupt() {
    if (!playing || !currentBuffer || !currentBuffer->data) return;
    
    if (playIndex >= currentBuffer->size) {
        playing = false;
        dacWrite(26, 128); // Silence
        return;
    }
    
    uint8_t output = 128;
    
    // Safety check for buffer overrun
    if (playIndex + 1 >= currentBuffer->size && currentBuffer->bitsPerSample == 16) {
         playing = false; return;
    }

    if (currentBuffer->bitsPerSample == 8) {
        output = currentBuffer->data[playIndex];
        playIndex += currentBuffer->channels;
    } else if (currentBuffer->bitsPerSample == 16) {
        // Convert 16-bit signed to 8-bit unsigned
        int16_t sample = (int16_t)(currentBuffer->data[playIndex] | (currentBuffer->data[playIndex+1] << 8));
        output = (sample / 256) + 128;
        playIndex += 2 * currentBuffer->channels;
    }
    
    dacWrite(26, output);
}
