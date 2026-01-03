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

#ifdef USE_I2S_AUDIO
// Task to feed I2S
void i2sTask(void* param) {
    SoundManager* sm = (SoundManager*)param;
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Keep alive
    }
}
#endif

bool SoundManager::begin() {
    if(!LittleFS.begin(true)){
        Serial.println("LittleFS Mount Failed");
        return false;
    }
    
    prefs.begin("metronome", false);

    #ifdef USE_I2S_AUDIO
    Serial.println("Initializing I2S...");
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // Mono
        .communication_format = I2S_COMM_FORMAT_I2S, // Standard I2S
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false,
        .tx_desc_auto_clear = true // Auto clear to avoid noise
    };
    
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_LRCK,
        .data_out_num = I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    
    i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM, &pin_config);
    i2s_zero_dma_buffer(I2S_NUM);
    #endif
    
    // Load saved sounds from LittleFS to RAM
    // Default to Standard if not set
    String dbPath = prefs.getString("dbPath", "/Standard_Downbeat.wav");
    String bPath = prefs.getString("bPath", "/Standard_Beat.wav");

    Serial.print("Loading "); Serial.println(dbPath);
    bool dbLoaded = loadWavToBuffer(dbPath, downbeat);
    
    if (!dbLoaded) {
        Serial.println("Failed to load downbeat. Trying fallback...");
        if (loadWavToBuffer("/Standard_Downbeat.wav", downbeat)) {
             dbPath = "/Standard_Downbeat.wav";
             prefs.putString("dbPath", dbPath);
        }
    }

    Serial.print("Loading "); Serial.println(bPath);
    bool bLoaded = loadWavToBuffer(bPath, beat);
    if (!bLoaded) {
         if (loadWavToBuffer("/Standard_Beat.wav", beat)) {
             bPath = "/Standard_Beat.wav";
             prefs.putString("bPath", bPath);
         }
    }
    
    #ifndef USE_I2S_AUDIO
    // Setup Timer for DAC
    // Use Timer 0, divider 80 (1MHz)
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &onTimer, true);
    
    // Initialize DAC to center (silence) to avoid pop
    dacWrite(26, 128);

    // Default alarm (will be updated on play)
    timerAlarmWrite(timer, 1000, true); 
    #endif
    
    return true;
}

std::vector<String> SoundManager::listWavs() {
    std::vector<String> files;
    
    Serial.println("--- Listing LittleFS Files ---");
    
    File root = LittleFS.open("/");
    if(!root){
        Serial.println("Failed to open root directory");
        return files;
    }
    
    if (!root.isDirectory()) {
        Serial.println("Error: Root is not a directory!");
        return files;
    }

    File file = root.openNextFile();
    while(file){
        String name = String(file.name());
        if(!file.isDirectory()){
            String upperName = name;
            upperName.toUpperCase();
            if(upperName.endsWith(".WAV")){
                // Only add if it's a Downbeat file to keep list clean
                // We assume matching _Beat.wav exists
                if (name.indexOf("_Downbeat") > 0) {
                    // Strip "_Downbeat.wav" for display
                    String displayName = name.substring(0, name.indexOf("_Downbeat"));
                    // Remove leading slash if present
                    if (displayName.startsWith("/")) displayName = displayName.substring(1);
                    
                    files.push_back(displayName);
                    Serial.print("  -> Added Set: "); Serial.println(displayName);
                }
            }
        }
        file = root.openNextFile();
    }
    Serial.println("--- End List ---");
    return files;
}

bool SoundManager::selectSound(SoundType type, String filename) {
    // Filename is just the prefix (e.g. "Standard")
    Serial.print("Selecting Sound for Type "); Serial.print(type); Serial.print(": "); Serial.println(filename);
    
    bool success = false;

    if (type == SOUND_DOWNBEAT) {
        String dbPath = "/" + filename + "_Downbeat.wav";
        if (loadWavToBuffer(dbPath, downbeat)) {
            prefs.putString("dbPath", dbPath);
            success = true;
        } else {
            Serial.println("Failed to load Downbeat: " + dbPath);
        }
    } else {
        String bPath = "/" + filename + "_Beat.wav";
        if (loadWavToBuffer(bPath, beat)) {
            prefs.putString("bPath", bPath);
            success = true;
        } else {
            Serial.println("Failed to load Beat: " + bPath);
        }
    }
    
    return success;
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
            
            #ifdef USE_I2S_AUDIO
            // I2S Mode: Keep 16-bit signed
            uint32_t targetSize = chunkSize;
            // If 8-bit, size doubles. If 16-bit, same. If 24-bit, 2/3.
            if (buffer.bitsPerSample == 8) targetSize = chunkSize * 2;
            else if (buffer.bitsPerSample == 24) targetSize = (chunkSize / 3) * 2;
            #else
            // DAC Mode: Convert to 8-bit unsigned
            uint32_t targetSize = chunkSize;
            if (buffer.bitsPerSample == 16) targetSize = chunkSize / 2;
            else if (buffer.bitsPerSample == 24) targetSize = chunkSize / 3;
            #endif
            
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
                    
                    #ifdef USE_I2S_AUDIO
                    // Convert to 16-bit signed
                    for (size_t i = 0; i < toRead; ) {
                        int16_t val = 0;
                        if (buffer.bitsPerSample == 16) {
                             // Already 16-bit signed
                             val = (int16_t)(tempBuf[i] | (tempBuf[i+1] << 8));
                             i += 2;
                        } else if (buffer.bitsPerSample == 24) {
                             // 24-bit signed -> 16-bit signed (drop LSB)
                             val = (int16_t)(tempBuf[i+1] | (tempBuf[i+2] << 8));
                             i += 3;
                        } else {
                             // 8-bit unsigned -> 16-bit signed
                             // (val - 128) * 256
                             val = ((int16_t)tempBuf[i] - 128) << 8;
                             i += 1;
                        }
                        
                        // Store as bytes (Little Endian)
                        if (outIndex < buffer.size) {
                            buffer.data[outIndex++] = val & 0xFF;
                            buffer.data[outIndex++] = (val >> 8) & 0xFF;
                        }
                    }
                    #else
                    // Convert to 8-bit unsigned
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
                    #endif
                    
                    bytesRead += toRead;
                    if (bytesRead % 10240 == 0) delay(1); // Yield
                }
                
                free(tempBuf);
                
                #ifdef USE_I2S_AUDIO
                buffer.bitsPerSample = 16;
                #else
                buffer.bitsPerSample = 8; 
                #endif
                
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

#ifdef USE_I2S_AUDIO
void SoundManager::playI2S(AudioBuffer* buffer) {
    if (!buffer || !buffer->data) return;
    
    // Apply Volume (Software Scaling)
    // We create a temporary buffer or scale in place?
    // Scaling in place modifies the original sound, which is bad if we change volume.
    // But we only have one copy in RAM.
    // Better: Scale on the fly while writing to I2S?
    // i2s_write takes a buffer.
    // Let's allocate a small chunk, scale, and write.
    
    size_t bytesWritten;
    size_t totalBytes = buffer->size;
    size_t chunkBytes = 1024; // Process in chunks
    int16_t* src = (int16_t*)buffer->data;
    
    // Temp buffer for scaled audio
    int16_t* tempBuf = (int16_t*)malloc(chunkBytes);
    if (!tempBuf) return;
    
    size_t processed = 0;
    while (processed < totalBytes) {
        size_t remaining = totalBytes - processed;
        size_t currentChunk = (remaining > chunkBytes) ? chunkBytes : remaining;
        size_t samples = currentChunk / 2;
        
        // Scale
        for (size_t i = 0; i < samples; i++) {
            int32_t val = src[(processed / 2) + i];
            val = (val * volume) / 255;
            tempBuf[i] = (int16_t)val;
        }
        
        // Write to I2S (Blocking if DMA full)
        // Since this is called from main loop (via playDownbeat -> playI2S), it might block UI.
        // But for short clicks (50ms), it's fine.
        // Wait! playDownbeat is called from Timer Interrupt in main loop?
        // No, playDownbeat is called from main loop logic.
        // If we block here for 50ms, the UI will freeze for 50ms. That's acceptable for a metronome click.
        // Actually, i2s_write returns quickly if DMA buffer is large enough.
        // We set dma_buf_count=8, len=64 -> 512 samples -> ~11ms.
        // If sound is 50ms, we will block for ~40ms.
        // To avoid blocking, we should use a task.
        // But for now, let's try direct write.
        
        i2s_write(I2S_NUM, tempBuf, currentChunk, &bytesWritten, portMAX_DELAY);
        processed += currentChunk;
    }
    
    free(tempBuf);
    
    // Write some silence to flush?
    // i2s_zero_dma_buffer(I2S_NUM); // This clears the buffer, might cut off sound.
    // Better to write 0s.
    // int16_t silence[64] = {0};
    // i2s_write(I2S_NUM, silence, 128, &bytesWritten, portMAX_DELAY);
}
#endif

void SoundManager::playDownbeat() {
    #ifdef USE_I2S_AUDIO
    playI2S(&downbeat);
    #else
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
    #endif
}

void SoundManager::playBeat() {
    #ifdef USE_I2S_AUDIO
    playI2S(&beat);
    #else
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
    #endif
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
