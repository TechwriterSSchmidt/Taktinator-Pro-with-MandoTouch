#ifndef PROGRAMMANAGER_H
#define PROGRAMMANAGER_H

#include <Arduino.h>
#include <vector>
#include <FS.h>
#include <LittleFS.h>

struct SequenceStep {
  int bars;
  int beatsPerBar;
  int bpm;
};

class ProgramManager {
public:
    void begin() {
        if (!LittleFS.begin()) {
            Serial.println("LittleFS Mount Failed");
            return;
        }
        // Create programs directory if it doesn't exist
        if (!LittleFS.exists("/programs")) {
            LittleFS.mkdir("/programs");
        }
    }

    std::vector<String> listPrograms() {
        std::vector<String> programs;
        fs::File root = LittleFS.open("/programs");
        if (!root || !root.isDirectory()) {
            return programs;
        }

        fs::File file = root.openNextFile();
        while (file) {
            String fileName = file.name();
            if (fileName.endsWith(".txt")) {
                if (!fileName.startsWith("/")) {
                    fileName = "/programs/" + fileName;
                }
                programs.push_back(fileName);
            }
            file = root.openNextFile();
        }
        return programs;
    }

    String getNextProgramName() {
        const char* names[] = {
            "MandoRock", "MandoTschuess", "MandoEver", "MandoPop", "MandoJazz", 
            "MandoBlues", "MandoMetal", "MandoFolk", "MandoGrass", "MandoClassic", 
            "MandoPunk", "MandoSoul", "MandoFunk", "MandoDisco", "MandoTechno", 
            "MandoBeat", "MandoGroove", "MandoVibe", "MandoJam", "MandoFlow",
            "MandoCool", "MandoSlow", "MandoJuice", "MandoBad", "MandoFast", 
            "MandoJoy", "MandoChill", "MandoHype", "MandoZen", "MandoCrazy",
            "MandoHello", "MandoHappy", "MandoSad", "MandoRelax", "MandoPower",
            "MandoDream", "MandoFire", "MandoIce", "MandoStorm", "MandoSun"
        };
        int numNames = 40;

        // Create indices
        std::vector<int> indices(numNames);
        for(int i=0; i<numNames; ++i) indices[i] = i;

        // Shuffle indices (Fisher-Yates)
        for (int i = numNames - 1; i > 0; i--) {
            int j = random(i + 1);
            int temp = indices[i];
            indices[i] = indices[j];
            indices[j] = temp;
        }

        // Try names in random order
        for (int i = 0; i < numNames; i++) {
            String name = "/programs/" + String(names[indices[i]]) + ".txt";
            if (!LittleFS.exists(name)) {
                return name;
            }
        }

        // Fallback to numbered MandoProg if all cool names are taken
        int i = 1;
        while (true) {
            String name = "/programs/MandoProg_" + String(i) + ".txt";
            if (!LittleFS.exists(name)) {
                return name;
            }
            i++;
        }
    }

    bool saveProgram(String path, const std::vector<SequenceStep>& sequence) {
        fs::File file = LittleFS.open(path, FILE_WRITE);
        if (!file) return false;

        for (const auto& step : sequence) {
            file.printf("%d,%d,%d\n", step.bars, step.beatsPerBar, step.bpm);
        }
        file.close();
        return true;
    }

    bool loadProgram(String path, std::vector<SequenceStep>& sequence) {
        fs::File file = LittleFS.open(path, FILE_READ);
        if (!file) return false;

        sequence.clear();
        while (file.available()) {
            String line = file.readStringUntil('\n');
            line.trim();
            if (line.length() > 0) {
                int c1 = line.indexOf(',');
                int c2 = line.lastIndexOf(',');
                if (c1 > 0 && c2 > c1) {
                    int bars = line.substring(0, c1).toInt();
                    int bpb = line.substring(c1 + 1, c2).toInt();
                    int bpm = line.substring(c2 + 1).toInt();
                    sequence.push_back({bars, bpb, bpm});
                }
            }
        }
        file.close();
        return true;
    }

    void deleteProgram(String path) {
        if (LittleFS.exists(path)) {
            LittleFS.remove(path);
        }
    }
};

extern ProgramManager programManager;

#endif
