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

// Task to feed I2S for Tone Generation

void toneTask(void* param) {

    SoundManager* sm = (SoundManager*)param;

    size_t bytesWritten;

    int sampleRate = 44100;

    float phase = 0;

    const int chunkSize = 512;

    int16_t buffer[chunkSize];

    

    while(1) {

        if (sm->tonePlaying) {

            float phaseIncrement = (2.0f * PI * sm->toneFrequency) / sampleRate;

            

            for (int i = 0; i < chunkSize; i++) {

                // Generate Sine Wave

                float val = sin(phase);

                // Scale by volume (0-255) -> 0-1.0 * 32767

                // Reduce max amplitude slightly to avoid clipping

                int16_t sample = (int16_t)(val * 30000.0f * (sm->volume / 255.0f));

                buffer[i] = sample;

                

                phase += phaseIncrement;

                if (phase >= 2.0f * PI) phase -= 2.0f * PI;

            }

            

            i2s_write(I2S_NUM, buffer, chunkSize * sizeof(int16_t), &bytesWritten, portMAX_DELAY);

        } else {

            // Suspend self or wait

            vTaskDelay(pdMS_TO_TICKS(100));

        }

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

    

    // Create Tone Task

    xTaskCreatePinnedToCore(toneTask, "ToneTask", 4096, this, 1, &toneTaskHandle, 1);

    #endif

    

    // Load default sounds (Metro)

    // User requested to always start with Metro sounds

    String dbPath = "/Metro_Downbeat.wav";

    String bPath = "/Metro_Beat.wav";



    Serial.print("Loading Default "); Serial.println(dbPath);

    if (loadSound(SOUND_DOWNBEAT, dbPath)) {

        Serial.println("Downbeat Loaded");

    } else {

        Serial.println("Failed to load Default Downbeat!");

    }



    Serial.print("Loading Default "); Serial.println(bPath);

    if (loadSound(SOUND_BEAT, bPath)) {

        Serial.println("Beat Loaded");

    } else {

        Serial.println("Failed to load Default Beat!");

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

                    

                    // Check if valid format

                    if (isValidWav(name)) {

                        files.push_back(displayName);

                        Serial.print("  -> Added Set: "); Serial.println(displayName);

                    } else {

                        Serial.print("  -> Skipped Invalid: "); Serial.println(name);

                    }

                }

            }

        }

        file = root.openNextFile();

    }

    Serial.println("--- End List ---");

    return files;

}



bool SoundManager::isValidWav(String path) {

    if (!path.startsWith("/")) path = "/" + path;

    File file = LittleFS.open(path, "r");

    if (!file) return false;



    if (file.size() < 44) { file.close(); return false; }



    char header[44];

    file.read((uint8_t*)header, 44);

    file.close();



    // Check RIFF

    if (memcmp(header, "RIFF", 4) != 0) return false;

    // Check WAVE

    if (memcmp(header + 8, "WAVE", 4) != 0) return false;

    

    // Parse fmt

    // Note: This assumes standard 44 byte header. 

    // If fmt chunk is moved or extra chunks exist, this simple check might fail valid files.

    // But for our standard converted files, it's fine.

    // Let's be slightly more robust by checking fmt signature if possible, 

    // but for speed we'll check the standard offsets first.

    

    // AudioFormat (Offset 20, 2 bytes) -> Must be 1 (PCM)

    uint16_t fmtCode = (uint16_t)(header[20] | (header[21] << 8));

    if (fmtCode != 1) return false;



    // Channels (Offset 22, 2 bytes) -> Must be 1 or 2

    uint16_t channels = (uint16_t)(header[22] | (header[23] << 8));

    if (channels > 2) return false; 



    // Sample Rate (Offset 24, 4 bytes) -> Max 48000

    uint32_t sampleRate = (uint32_t)(header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24));

    if (sampleRate > 48000) return false;



    // BitsPerSample (Offset 34, 2 bytes) -> Max 16

    uint16_t bits = (uint16_t)(header[34] | (header[35] << 8));

    if (bits > 16) return false;



    return true;

}



bool SoundManager::loadSound(SoundType type, String fullPath) {

    if (!fullPath.startsWith("/")) fullPath = "/" + fullPath;

    

    bool success = false;

    if (type == SOUND_DOWNBEAT) {

        if (loadWavToBuffer(fullPath, downbeat)) {

            currentDownbeatPath = fullPath;

            success = true;

        }

    } else {

        if (loadWavToBuffer(fullPath, beat)) {

            currentBeatPath = fullPath;

            success = true;

        }

    }

    return success;

}



bool SoundManager::selectSound(SoundType type, String filename) {

    // Filename is just the prefix (e.g. "Standard")

    Serial.print("Selecting Sound for Type "); Serial.print(type); Serial.print(": "); Serial.println(filename);

    

    String path;

    if (type == SOUND_DOWNBEAT) {

        path = "/" + filename + "_Downbeat.wav";

    } else {

        path = "/" + filename + "_Beat.wav";

    }

    

    return loadSound(type, path);

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




void SoundManager::startTone(float frequency) {
    toneFrequency = frequency;
    tonePlaying = true;
}

void SoundManager::stopTone() {
    tonePlaying = false;
    #ifdef USE_I2S_AUDIO
    i2s_zero_dma_buffer(I2S_NUM);
    #endif
}

void SoundManager::previewSound(String filename) {
    // Preview the 'Beat' sound of the selected set
    String path = "/" + filename + "_Beat.wav";
    
    // We use a temporary buffer to avoid overwriting the main sounds until confirmed
    AudioBuffer tempBuffer;
    if (loadWavToBuffer(path, tempBuffer)) {
        #ifdef USE_I2S_AUDIO
        playI2S(&tempBuffer);
        // We need to wait for the sound to play before freeing memory
        // A simple delay is acceptable in the UI thread for a preview
        delay(100); 
        if (tempBuffer.data) free(tempBuffer.data);
        #endif
    }
}
