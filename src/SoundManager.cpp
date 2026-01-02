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
    Serial.println("Loading /downbeat.wav...");
    bool dbLoaded = loadWavToBuffer("/downbeat.wav", downbeat);
    
    if (!dbLoaded) {
        Serial.println("Failed to load downbeat.wav. Skipping beat.wav to avoid delays.");
    } else {
        Serial.println("Loading /beat.wav...");
        bool bLoaded = loadWavToBuffer("/beat.wav", beat);
        if (!bLoaded) Serial.println("Failed to load beat.wav");
    }
    
    // Setup Timer for DAC
    // Use Timer 0, divider 80 (1MHz)
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &onTimer, true);
    
    // Initialize DAC to center (silence) to avoid pop
    dacWrite(26, 128);

    // Default alarm (will be updated on play)
    timerAlarmWrite(timer, 1000, true); 
    // timerAlarmEnable(timer); // Don't enable yet, wait for play
    
    return true;
}

std::vector<String> SoundManager::listWavsOnSD() {
    std::vector<String> files;
    
    Serial.println("--- Listing SD Files ---");
    
    // Ensure Touch CS is HIGH (Deselected)
    pinMode(33, OUTPUT);
    digitalWrite(33, HIGH);

    // Initialize global SPI for SD
    // SCK=18, MISO=19, MOSI=23, SS=5
    SPI.begin(18, 19, 23, 5);
    
    // Try mounting
    // Note: If card is >32GB, it might be exFAT which is not supported by standard SD lib.
    // Must be FAT32.
    if(!SD.begin(5, SPI, 4000000)){
        Serial.println("SD Mount Failed (4MHz), trying 1MHz...");
        if(!SD.begin(5, SPI, 1000000)){
            Serial.println("SD Mount Failed! Check card format (FAT32) and connections.");
            Serial.println("Debug: Pins SCK=18, MISO=19, MOSI=23, CS=5");
            
            // Check if card is present at all (might not work if begin failed completely)
            if (SD.cardType() == CARD_NONE) {
                 Serial.println("Debug: No SD Card detected or Filesystem invalid.");
                 Serial.println("HINT: Error 13 means the card is readable but the format is wrong.");
                 Serial.println("HINT: Please format as FAT32 (not exFAT). Use 'SD Memory Card Formatter' if possible.");
            } else {
                 Serial.print("Debug: Card Type detected: ");
                 Serial.println(SD.cardType());
            }
            return files;
        }
    }
    
    Serial.println("SD Mounted Successfully");
    
    // Print Card Stats
    Serial.printf("Card Size: %llu MB\n", SD.cardSize() / (1024 * 1024));
    Serial.printf("Total Bytes: %llu MB\n", SD.totalBytes() / (1024 * 1024));
    Serial.printf("Used Bytes: %llu MB\n", SD.usedBytes() / (1024 * 1024));

    File root = SD.open("/");
    if(!root){
        Serial.println("Failed to open root directory");
        SD.end();
        return files;
    }
    
    if (!root.isDirectory()) {
        Serial.println("Error: Root is not a directory!");
        SD.end();
        return files;
    }

    Serial.println("Root opened. Iterating...");
    // root.rewindDirectory(); // Removed to avoid potential issues

    File file = root.openNextFile();
    if (!file) {
        Serial.println("Warning: root.openNextFile() returned false immediately.");
        Serial.println("Possible causes: Empty card, incompatible filesystem settings, or LFN issues.");
    }

    int count = 0;
    while(file){
        String name = String(file.name());
        Serial.print("Entry found: "); Serial.println(name);
        
        if(!file.isDirectory()){
            // Case insensitive check
            String upperName = name;
            upperName.toUpperCase();
            
            if(upperName.endsWith(".WAV")){
                Serial.print("  -> Added to list: "); Serial.println(name);
                files.push_back(name);
            } else {
                Serial.println("  -> Skipped (not .wav)");
            }
        } else {
             Serial.println("  -> Skipped (Directory)");
        }
        file = root.openNextFile();
        count++;
    }
    Serial.print("Total entries processed: "); Serial.println(count);
    SD.end(); 
    // SPI.end(); // Keep SPI active
    Serial.println("--- End List ---");
    return files;
}

bool SoundManager::selectSound(SoundType type, String sdFilename) {
    Serial.print("Selecting sound: "); Serial.println(sdFilename);
    
    // Ensure Touch CS is HIGH
    pinMode(33, OUTPUT);
    digitalWrite(33, HIGH);

    SPI.begin(18, 19, 23, 5);
    
    if(!SD.begin(5, SPI, 4000000)) {
        Serial.println("SD Mount Failed during selection");
        return false;
    }
    
    String destPath = (type == SOUND_DOWNBEAT) ? "/downbeat.wav" : "/beat.wav";
    
    // Copy file
    if (LittleFS.exists(destPath)) LittleFS.remove(destPath);
    
    File source = SD.open(sdFilename, "r"); // Removed leading slash requirement
    if (!source) { 
        // Try adding slash if missing
        if (!sdFilename.startsWith("/")) source = SD.open("/" + sdFilename, "r");
    }
    
    if (!source) { 
        Serial.println("Failed to open source file on SD");
        SD.end(); return false; 
    }
    
    File dest = LittleFS.open(destPath, "w");
    if (!dest) { 
        Serial.println("Failed to open dest file on LittleFS");
        source.close(); SD.end(); return false; 
    }
    
    uint8_t buf[512];
    while (source.available()) {
        size_t len = source.read(buf, 512);
        dest.write(buf, len);
    }
    
    dest.close();
    source.close();
    SD.end();
    // SPI.end();
    
    Serial.println("Copy successful, reloading buffer...");
    
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
            
            Serial.print("WAV Format: Code="); Serial.print(fmtCode);
            Serial.print(", Chan="); Serial.print(channels);
            Serial.print(", Rate="); Serial.print(sampleRate);
            Serial.print(", Bits="); Serial.println(bitsPerSample);

            if (fmtCode != 1) {
                Serial.println("Error: Unsupported WAV format (Compressed/Float). Must be PCM (1).");
                file.close();
                return false;
            }

            // Safety Check: Reject High-Res files to prevent RAM overflow/Crash
            if (sampleRate > 48000 || bitsPerSample > 16) {
                Serial.println("Error: File format too high for RAM! (Max 48kHz, 16-bit)");
                Serial.println("-> Please select a smaller file from SD Card menu.");
                file.close();
                return false;
            }

            buffer.sampleRate = sampleRate;
            buffer.channels = channels;
            buffer.bitsPerSample = bitsPerSample;
            
            // Skip any extra fmt bytes
            if (chunkSize > 16) file.seek(file.position() + chunkSize - 16);
            foundFmt = true;
        } else if (memcmp(chunkID, "data", 4) == 0) {
            if (buffer.data) free(buffer.data);
            
            // Calculate target size (convert to 8-bit)
            // If 16-bit, size is half. If 8-bit, size is same.
            uint32_t targetSize = chunkSize;
            if (buffer.bitsPerSample == 16) targetSize = chunkSize / 2;
            else if (buffer.bitsPerSample == 24) targetSize = chunkSize / 3;
            
            // Limit size to available RAM (safety margin)
            size_t freeHeap = ESP.getFreeHeap();
            if (targetSize > freeHeap - 40000) {
                Serial.println("Error: WAV file too large for RAM!");
                Serial.print("Required: "); Serial.print(targetSize);
                Serial.print(", Free: "); Serial.println(freeHeap);
                file.close();
                return false;
            }

            buffer.size = targetSize;
            buffer.data = (uint8_t*)malloc(targetSize);
            
            if (buffer.data) {
                // Read and convert on the fly
                uint8_t* tempBuf = (uint8_t*)malloc(512);
                if (!tempBuf) {
                    Serial.println("Error: Temp Malloc failed!");
                    free(buffer.data); buffer.data = nullptr;
                    file.close(); return false;
                }

                size_t bytesRead = 0;
                size_t outIndex = 0;
                
                while (bytesRead < chunkSize) {
                    size_t toRead = chunkSize - bytesRead;
                    if (toRead > 512) toRead = 512;
                    file.read(tempBuf, toRead);
                    
                    // Convert to 8-bit
                    for (size_t i = 0; i < toRead; ) {
                        uint8_t val = 128;
                        if (buffer.bitsPerSample == 16) {
                             // 16-bit signed -> 8-bit unsigned
                             int16_t s = (int16_t)(tempBuf[i] | (tempBuf[i+1] << 8));
                             val = (s / 256) + 128;
                             i += 2;
                        } else if (buffer.bitsPerSample == 24) {
                             // 24-bit signed -> 8-bit unsigned
                             val = tempBuf[i+2] ^ 0x80;
                             i += 3;
                        } else {
                             // 8-bit unsigned (already correct)
                             val = tempBuf[i];
                             i += 1;
                        }
                        if (outIndex < buffer.size) buffer.data[outIndex++] = val;
                    }
                    
                    bytesRead += toRead;
                    if (bytesRead % 10240 == 0) delay(1); // Yield
                }
                
                free(tempBuf);
                
                // Update buffer info to reflect 8-bit storage
                buffer.bitsPerSample = 8; 
                
                foundData = true;
                Serial.print("Loaded & Converted bytes: "); Serial.println(targetSize);
            } else {
                Serial.println("Error: Malloc failed!");
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
    timerAlarmEnable(timer); // Enable timer
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
    timerAlarmEnable(timer); // Enable timer
    portEXIT_CRITICAL(&timerMux);
}

void SoundManager::setVolume(uint8_t vol) {
    volume = vol;
}

void IRAM_ATTR SoundManager::handleInterrupt() {
    if (!playing || !currentBuffer || !currentBuffer->data) {
        timerAlarmDisable(timer); // Stop interrupt
        return;
    }
    
    if (playIndex >= currentBuffer->size) {
        playing = false;
        dacWrite(26, 128); // Silence
        timerAlarmDisable(timer); // Stop interrupt
        return;
    }
    
    // Data is always 8-bit unsigned now (converted on load)
    uint8_t output = currentBuffer->data[playIndex];
    
    // Advance index (skip other channels if stereo)
    // Since we kept channels during conversion, we skip N bytes
    playIndex += currentBuffer->channels;
    
    // Apply Volume (Optimized)
    if (volume != 255) {
        // Fast integer scaling: (sample * volume) >> 8
        // Center at 0 for scaling: (val - 128)
        int16_t sample = (int16_t)output - 128;
        sample = (sample * volume) >> 8; // Bit shift is faster than division
        output = (uint8_t)(sample + 128);
    }

    dacWrite(26, output);
}
