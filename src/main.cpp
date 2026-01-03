#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Bitbang.h>
#include <vector>
#include "SoundManager.h"

// --- Hardware Definitions ---
// I2S Pins (MAX98357A)
// BCLK -> GPIO 27 (Side Connector P3/CN1)
// LRCK -> GPIO 1  (UART Connector P1 - TX Pin)
// DIN  -> GPIO 3  (UART Connector P1 - RX Pin)
// VIN  -> 5V
// GND  -> GND

TFT_eSPI tft = TFT_eSPI();

// Touch Screen Pins (CYD)
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// Use BitBang SPI for Touch to avoid conflict with SD (VSPI)
XPT2046_Bitbang ts(XPT2046_MOSI, XPT2046_MISO, XPT2046_CLK, XPT2046_CS);

// --- UI Constants ---
#define SLIDER_X 20
#define SLIDER_Y 55
#define SLIDER_W 280
#define SLIDER_H 15
#define SLIDER_KNOB_R 8

#define VOL_BAR_X 70
#define VOL_BAR_Y 205
#define VOL_BAR_W 180
#define VOL_BAR_H 15

// --- State Variables ---
int bpm = 120;
int volume = 127; 
bool isPlaying = false;
unsigned long lastTouchTime = 0;
unsigned long lastClickTime = 0;

// Time Signature State
int beatsPerBar = 4; // Default 4/4
int currentBeat = 0; // 0 to beatsPerBar-1

// --- Sequence / Program Mode ---
enum ScreenState { SCREEN_MAIN, SCREEN_EDITOR, SCREEN_SOUND_SELECT };
ScreenState currentScreen = SCREEN_MAIN;

struct SequenceStep {
  int bars;
  int beatsPerBar;
  int bpm;
};

std::vector<SequenceStep> sequence;
bool isSequenceMode = false;
int currentStepIndex = 0;
int barsPlayedInStep = 0;
int selectedStepIndex = -1; // For Editor

// --- Sound Selection State ---
std::vector<String> wavFiles;
int soundListScroll = 0;
int selectedSoundIndex = -1;
SoundType targetSoundType = SOUND_DOWNBEAT; // Which sound are we selecting?

// --- Forward Declarations ---
void updateBPM();
void updateVolume();
void updateTimeSig();
void drawSlider();
void drawVolumeBar();
void toggleMetronome();
void increaseBPM10();
void decreaseBPM10();
void increaseBPM1();
void decreaseBPM1();
void increaseVol();
void decreaseVol();
void cycleTimeSig();
void toggleEditor(); 
void toggleSoundSelect();
void drawUI();
void drawEditor();
void drawSoundSelect();
void refreshSoundList();

// --- Button Structure ---
struct Button {
  int x, y, w, h;
  String label;
  uint16_t color;
  void (*action)();
  bool isCustomDraw; 
};

// --- Button Layout ---
Button buttons[] = {
  // BPM Controls (Row 1) - Taller for fingers
  {10, 75, 65, 40, "-10", TFT_BLUE, decreaseBPM10, false},
  {85, 75, 65, 40, "-1", TFT_NAVY, decreaseBPM1, false},
  {160, 75, 65, 40, "+1", TFT_NAVY, increaseBPM1, false},
  {235, 75, 65, 40, "+10", TFT_BLUE, increaseBPM10, false},
  
  // Row 2: Time Sig & Mandolin - Taller
  {10, 120, 70, 70, "4/4", TFT_PURPLE, cycleTimeSig, false}, // Time Sig Button
  {90, 120, 150, 70, "", TFT_DARKGREEN, toggleMetronome, true}, // Mandolin Button (No Text)
  
  // Split PROG button area - Taller
  {250, 120, 60, 32, "PROG", TFT_NAVY, toggleEditor, false}, // PROG Button
  {250, 158, 60, 32, "SND", TFT_MAROON, toggleSoundSelect, false}, // SOUND Button

  // Volume Controls (Bottom Row) - Bigger
  {10, 195, 50, 40, "-", TFT_DARKGREY, decreaseVol, false},
  {260, 195, 50, 40, "+", TFT_DARKGREY, increaseVol, false}
};

const int numButtons = sizeof(buttons) / sizeof(Button);

// --- Helper Functions ---

void drawMandolin(int x, int y, int w, int h, uint16_t bodyColor) {
  int cx = x + w / 2;
  int cy = y + h / 2;
  
  // Body (Rotated 90 deg left -> Neck points left, Body on right)
  int bodyX = cx + 15;
  tft.fillEllipse(bodyX, cy, 20, 15, bodyColor); // Slimmer body
  tft.drawEllipse(bodyX, cy, 20, 15, TFT_WHITE);
  tft.fillCircle(bodyX - 5, cy, 5, TFT_BLACK); // Sound hole
  
  // Neck (Pointing Left)
  tft.fillRect(x + 10, cy - 3, 45, 6, TFT_BROWN); 
  
  // Headstock (Far Left)
  tft.fillRect(x + 2, cy - 6, 10, 12, bodyColor); 
  
  // Strings
  tft.drawLine(x + 5, cy - 1, bodyX, cy - 1, TFT_SILVER);
  tft.drawLine(x + 5, cy + 1, bodyX, cy + 1, TFT_SILVER);
}

void drawButton(int index) {
  Button b = buttons[index];
  
  if (b.isCustomDraw) {
    uint16_t bg = isPlaying ? TFT_MAROON : TFT_DARKGREEN;
    tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, bg);
    tft.drawRoundRect(b.x, b.y, b.w, b.h, 8, TFT_WHITE); // Border
    drawMandolin(b.x, b.y, b.w, b.h, TFT_ORANGE);
  } else {
    tft.fillRoundRect(b.x, b.y, b.w, b.h, 5, b.color);
    tft.drawRoundRect(b.x, b.y, b.w, b.h, 5, TFT_WHITE); // Border
    tft.setTextColor(TFT_WHITE, b.color);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2); 
    
    // Special handling for Time Sig label update
    if (index == 4) { // Time Sig Button Index
       String label;
       if (beatsPerBar == 6) label = "6/8";
       else if (beatsPerBar == 7) label = "7/8";
       else if (beatsPerBar == 9) label = "9/8";
       else label = String(beatsPerBar) + "/4";
       
       tft.drawString(label, b.x + b.w / 2, b.y + b.h / 2);
    } else {
       tft.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2); 
    }
  }
}

void drawSlider() {
  tft.fillRect(0, SLIDER_Y - 12, 320, SLIDER_H + 24, TFT_BLACK);
  tft.fillRoundRect(SLIDER_X, SLIDER_Y + SLIDER_H/2 - 3, SLIDER_W, 6, 3, TFT_DARKGREY);
  
  float p = (float)(bpm - 40) / (208.0 - 40.0);
  if (p < 0) p = 0; if (p > 1) p = 1;
  
  int knobX = SLIDER_X + (int)(p * SLIDER_W);
  tft.fillCircle(knobX, SLIDER_Y + SLIDER_H/2, SLIDER_KNOB_R, TFT_ORANGE);
  tft.drawCircle(knobX, SLIDER_Y + SLIDER_H/2, SLIDER_KNOB_R, TFT_WHITE);
}

void drawVolumeBar() {
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);

  tft.drawRect(VOL_BAR_X, VOL_BAR_Y, VOL_BAR_W, VOL_BAR_H, TFT_WHITE);
  
  int fillW = map(volume, 0, 255, 0, VOL_BAR_W - 2);
  if (fillW < 0) fillW = 0;
  if (fillW > VOL_BAR_W - 2) fillW = VOL_BAR_W - 2;
  
  uint16_t barColor = TFT_GREEN;
  if (volume > 200) barColor = TFT_RED;
  else if (volume > 100) barColor = TFT_YELLOW;
  
  tft.fillRect(VOL_BAR_X + 1, VOL_BAR_Y + 1, fillW, VOL_BAR_H - 2, barColor);
  tft.fillRect(VOL_BAR_X + 1 + fillW, VOL_BAR_Y + 1, VOL_BAR_W - 2 - fillW, VOL_BAR_H - 2, TFT_BLACK);
}

void updateBPM() {
  if (currentScreen != SCREEN_MAIN) return;
  tft.fillRect(0, 20, 320, 35, TFT_BLACK); 
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawNumber(bpm, 160, 20, 4);
  tft.setTextSize(1);
  tft.drawString("BPM", 240, 28, 2); 
  drawSlider();
}

void updateVolume() {
  if (currentScreen != SCREEN_MAIN) return;
  drawVolumeBar();
}

// --- Editor Screen ---
void drawEditor() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(2);
  tft.drawString("Rhythm Program", 160, 5);

  tft.setTextSize(2);
  tft.setTextDatum(TL_DATUM);
  // Reduced spacing to fit buttons
  for (int i = 0; i < sequence.size() && i < 5; i++) {
    int y = 35 + i * 32;
    uint16_t color = (i == selectedStepIndex) ? TFT_YELLOW : TFT_WHITE;
    tft.setTextColor(color, TFT_BLACK);
    
    String sigLabel = String(sequence[i].beatsPerBar) + "/4";
    if (sequence[i].beatsPerBar == 6) sigLabel = "6/8";
    if (sequence[i].beatsPerBar == 7) sigLabel = "7/8";
    if (sequence[i].beatsPerBar == 9) sigLabel = "9/8";

    String line = String(i + 1) + ". " + String(sequence[i].bars) + "x " + 
                  sigLabel + " " + String(sequence[i].bpm);
    tft.drawString(line, 20, y);
  }

  int yBase = 200; // Moved up from 220
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  
  tft.drawRoundRect(10, yBase, 60, 35, 5, TFT_GREEN); tft.drawString("ADD", 40, yBase + 17);
  tft.drawRoundRect(80, yBase, 60, 35, 5, TFT_RED); tft.drawString("DEL", 110, yBase + 17);
  tft.drawRoundRect(150, yBase, 60, 35, 5, TFT_BLUE); tft.drawString("BACK", 180, yBase + 17);
  
  uint16_t playColor = isSequenceMode ? TFT_RED : TFT_GREEN;
  String playLabel = isSequenceMode ? "STOP" : "RUN";
  tft.drawRoundRect(220, yBase, 90, 35, 5, playColor); tft.drawString(playLabel, 265, yBase + 17);

  if (selectedStepIndex >= 0 && selectedStepIndex < sequence.size()) {
     int xBase = 220;
     int yStart = 40;
     tft.setTextSize(1);
     
     tft.drawString("Bars", xBase + 40, yStart);
     tft.drawRoundRect(xBase, yStart + 10, 30, 30, 3, TFT_WHITE); tft.drawString("-", xBase + 15, yStart + 25);
     tft.drawRoundRect(xBase + 50, yStart + 10, 30, 30, 3, TFT_WHITE); tft.drawString("+", xBase + 65, yStart + 25);
     
     yStart += 50;
     tft.drawString("Sig", xBase + 40, yStart);
     tft.drawRoundRect(xBase, yStart + 10, 30, 30, 3, TFT_WHITE); tft.drawString("-", xBase + 15, yStart + 25);
     tft.drawRoundRect(xBase + 50, yStart + 10, 30, 30, 3, TFT_WHITE); tft.drawString("+", xBase + 65, yStart + 25);

     yStart += 50;
     tft.drawString("BPM", xBase + 40, yStart);
     tft.drawRoundRect(xBase, yStart + 10, 30, 30, 3, TFT_WHITE); tft.drawString("-", xBase + 15, yStart + 25);
     tft.drawRoundRect(xBase + 50, yStart + 10, 30, 30, 3, TFT_WHITE); tft.drawString("+", xBase + 65, yStart + 25);
  }
}

// --- Sound Select Screen ---
void drawSoundSelect() {
    tft.fillScreen(TFT_BLACK);
    
    // Tabs
    int tabW = 145;
    int tabH = 35;
    
    // Tab 1: Downbeat
    uint16_t c1 = (targetSoundType == SOUND_DOWNBEAT) ? TFT_GREEN : TFT_DARKGREY;
    tft.fillRoundRect(10, 5, tabW, tabH, 5, c1);
    tft.setTextColor(TFT_WHITE, c1);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.drawString("Downbeat", 10 + tabW/2, 5 + tabH/2);
    
    // Tab 2: Upbeat
    uint16_t c2 = (targetSoundType == SOUND_BEAT) ? TFT_GREEN : TFT_DARKGREY;
    tft.fillRoundRect(165, 5, tabW, tabH, 5, c2);
    tft.setTextColor(TFT_WHITE, c2);
    tft.drawString("Upbeat", 165 + tabW/2, 5 + tabH/2);

    // File List
    tft.drawRect(10, 50, 240, 150, TFT_WHITE); 
    tft.setTextSize(2);
    tft.setTextDatum(TL_DATUM);
    
    int y = 55; 
    for (int i = soundListScroll; i < wavFiles.size() && i < soundListScroll + 5; i++) {
        if (i == selectedSoundIndex) tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        else tft.setTextColor(TFT_WHITE, TFT_BLACK);
        
        String dispName = wavFiles[i];
        if (dispName.length() > 20) {
            dispName = dispName.substring(0, 17) + "...";
        }
        tft.drawString(dispName, 25, y); // Moved slightly right (20->25)
        y += 28; 
    }

    // Scroll Buttons
    tft.setTextDatum(MC_DATUM);
    tft.drawRoundRect(260, 50, 50, 70, 5, TFT_DARKGREY);
    tft.drawString("/\\", 285, 85); // Up
    
    tft.drawRoundRect(260, 130, 50, 70, 5, TFT_DARKGREY);
    tft.drawString("\\/", 285, 165); // Down
    
    // Controls
    int yBase = 210; 
    tft.setTextDatum(MC_DATUM);
    tft.drawRoundRect(10, yBase, 100, 35, 5, TFT_BLUE); tft.drawString("BACK", 60, yBase + 17);
    tft.drawRoundRect(210, yBase, 100, 35, 5, TFT_GREEN); tft.drawString("SELECT", 260, yBase + 17);
}

void refreshSoundList() {
    wavFiles = soundManager.listWavs();
    
    selectedSoundIndex = -1;
    soundListScroll = 0;
    drawSoundSelect();
}

void handleTouchSoundSelect(int x, int y) {
    // Tabs
    if (y < 45) {
        if (x > 10 && x < 155) {
            targetSoundType = SOUND_DOWNBEAT;
            drawSoundSelect();
        } else if (x > 165 && x < 310) {
            targetSoundType = SOUND_BEAT;
            drawSoundSelect();
        }
        return;
    }

    // Scroll Up
    if (x > 260 && y > 50 && y < 120) {
        if (soundListScroll > 0) {
            soundListScroll--;
            drawSoundSelect();
        }
        return;
    }

    // Scroll Down
    if (x > 260 && y > 130 && y < 200) {
        if (soundListScroll + 5 < wavFiles.size()) {
            soundListScroll++;
            drawSoundSelect();
        }
        return;
    }
    
    // List Selection
    if (x < 250 && y > 50 && y < 200) { 
        int idx = (y - 55) / 28 + soundListScroll;
        if (idx >= 0 && idx < wavFiles.size()) {
            selectedSoundIndex = idx;
            drawSoundSelect();
        }
        return;
    }
    
    int yBase = 210;
    // BACK
    if (y > yBase && x < 110) {
        toggleSoundSelect();
        return;
    }

    // SELECT
    if (y > yBase && x > 210) {
        if (selectedSoundIndex >= 0 && selectedSoundIndex < wavFiles.size()) {
            tft.fillScreen(TFT_BLACK);
            tft.drawString("Loading...", 160, 120);
            
            soundManager.selectSound(targetSoundType, wavFiles[selectedSoundIndex]);
            
            toggleSoundSelect(); 
        }
    }
}

void handleTouchEditor(int x, int y) {
  // List Selection
  for (int i = 0; i < sequence.size() && i < 5; i++) {
    int yPos = 35 + i * 32; // Adjusted
    if (y > yPos && y < yPos + 30 && x < 200) {
      selectedStepIndex = i;
      drawEditor();
      return;
    }
  }

  int yBase = 200; // Adjusted
  // ADD
  if (y > yBase && y < yBase + 35 && x > 10 && x < 70) {
    if (sequence.size() < 5) {
      sequence.push_back({4, 4, 120});
      selectedStepIndex = sequence.size() - 1;
      drawEditor();
    }
  }
  // DEL
  if (y > yBase && y < yBase + 35 && x > 80 && x < 140) {
    if (!sequence.empty() && selectedStepIndex >= 0) {
      sequence.erase(sequence.begin() + selectedStepIndex);
      if (selectedStepIndex >= sequence.size()) selectedStepIndex = sequence.size() - 1;
      drawEditor();
    }
  }
  // BACK
  if (y > yBase && y < yBase + 35 && x > 150 && x < 210) {
    toggleEditor();
  }
  // RUN/STOP
  if (y > yBase && y < yBase + 35 && x > 220 && x < 310) {
    isSequenceMode = !isSequenceMode;
    isPlaying = isSequenceMode;
    currentStepIndex = 0;
    barsPlayedInStep = 0;
    currentBeat = 0;
    if (isSequenceMode && !sequence.empty()) {
       beatsPerBar = sequence[0].beatsPerBar;
       bpm = sequence[0].bpm;
    }
    drawEditor();
  }

  // Edit Controls
  if (selectedStepIndex >= 0 && selectedStepIndex < sequence.size()) {
     int xBase = 220;
     int yStart = 40;
     
     // Bars
     if (y > yStart + 10 && y < yStart + 40) {
       if (x > xBase && x < xBase + 30) { sequence[selectedStepIndex].bars--; if(sequence[selectedStepIndex].bars < 1) sequence[selectedStepIndex].bars = 1; }
       if (x > xBase + 50 && x < xBase + 80) { sequence[selectedStepIndex].bars++; }
       drawEditor();
     }
     
     // Sig
     yStart += 50;
     if (y > yStart + 10 && y < yStart + 40) {
       if (x > xBase && x < xBase + 30) { 
          int& b = sequence[selectedStepIndex].beatsPerBar;
          if (b == 9) b = 7; else if (b == 7) b = 6; else if (b == 6) b = 5; else if (b == 5) b = 4; else if (b == 4) b = 3; else if (b == 3) b = 2; 
       }
       if (x > xBase + 50 && x < xBase + 80) { 
          int& b = sequence[selectedStepIndex].beatsPerBar;
          if (b == 2) b = 3; else if (b == 3) b = 4; else if (b == 4) b = 5; else if (b == 5) b = 6; else if (b == 6) b = 7; else if (b == 7) b = 9;
       }
       drawEditor();
     }

     // BPM
     yStart += 50;
     if (y > yStart + 10 && y < yStart + 40) {
       if (x > xBase && x < xBase + 30) { sequence[selectedStepIndex].bpm -= 5; if(sequence[selectedStepIndex].bpm < 40) sequence[selectedStepIndex].bpm = 40; }
       if (x > xBase + 50 && x < xBase + 80) { sequence[selectedStepIndex].bpm += 5; if(sequence[selectedStepIndex].bpm > 208) sequence[selectedStepIndex].bpm = 208; }
       drawEditor();
     }
  }
}

void updateTimeSig() {
  if (currentScreen != SCREEN_MAIN) return;
  drawButton(4); 
}

void drawUI() {
  if (currentScreen == SCREEN_EDITOR) {
    drawEditor();
    return;
  }
  if (currentScreen == SCREEN_SOUND_SELECT) {
    drawSoundSelect();
    return;
  }

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(1);
  tft.drawString("Taktinator Pro", 160, 0, 2); 
  
  updateBPM();
  drawVolumeBar(); 
  
  for (int i = 0; i < numButtons; i++) {
    drawButton(i);
  }
}

void toggleEditor() {
  if (currentScreen == SCREEN_MAIN) {
    currentScreen = SCREEN_EDITOR;
    if (sequence.empty()) {
      sequence.push_back({4, 4, 120});
    }
    drawEditor();
  } else {
    currentScreen = SCREEN_MAIN;
    drawUI();
  }
}

void toggleSoundSelect() {
    if (currentScreen == SCREEN_MAIN) {
        currentScreen = SCREEN_SOUND_SELECT;
        refreshSoundList();
    } else {
        currentScreen = SCREEN_MAIN;
        drawUI();
    }
}

// --- Actions ---

void toggleMetronome() {
  isPlaying = !isPlaying;
  currentBeat = 0; 
  drawButton(5); 
}

void cycleTimeSig() {
  if (beatsPerBar == 2) beatsPerBar = 3;
  else if (beatsPerBar == 3) beatsPerBar = 4;
  else if (beatsPerBar == 4) beatsPerBar = 5;
  else if (beatsPerBar == 5) beatsPerBar = 6;
  else if (beatsPerBar == 6) beatsPerBar = 7;
  else if (beatsPerBar == 7) beatsPerBar = 9;
  else beatsPerBar = 2;
  
  currentBeat = 0; 
  updateTimeSig();
}

void increaseBPM10() { bpm += 10; if (bpm > 208) bpm = 208; updateBPM(); }
void decreaseBPM10() { bpm -= 10; if (bpm < 40) bpm = 40; updateBPM(); }
void increaseBPM1() { bpm += 1; if (bpm > 208) bpm = 208; updateBPM(); }
void decreaseBPM1() { bpm -= 1; if (bpm < 40) bpm = 40; updateBPM(); }

void increaseVol() { 
  volume += 5; 
  if (volume > 255) volume = 255; 
  soundManager.setVolume((uint8_t)volume);
  updateVolume(); 
}
void decreaseVol() { 
  volume -= 5; 
  if (volume < 0) volume = 0; 
  soundManager.setVolume((uint8_t)volume);
  updateVolume(); 
}

// --- Setup & Loop ---

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("--- Taktinator Pro (Metronome) ---");

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(true); // Re-enable inversion for CYD display
  tft.fillScreen(TFT_BLACK);
  
  // Title
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.drawCentreString("Taktinator Pro", 160, 90, 1);

  // Subtitle
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawCentreString("With MandoTouch", 160, 130, 1);

  delay(3000); // Show logo for 3 seconds

  ts.begin();
  pinMode(XPT2046_IRQ, INPUT);
  // ts.setRotation(1); // Bitbang lib might not support rotation or handles it differently
  
  // Init Sound Manager
  if (!soundManager.begin()) {
      Serial.println("Sound Manager Init Failed");
  }
  soundManager.setVolume((uint8_t)volume);
  
  // Check if sounds are loaded, if not, go to Sound Select
  if (!soundManager.areSoundsLoaded()) {
      Serial.println("Sounds missing or invalid. Opening Sound Select...");
      currentScreen = SCREEN_SOUND_SELECT;
      refreshSoundList();
  } else {
      drawUI();
  }
}

void loop() {
  // Metronome Logic
  if (isPlaying) {
    unsigned long interval = 60000 / bpm;
    unsigned long currentMillis = millis();
    
    if (currentMillis - lastClickTime >= interval) {
      lastClickTime = currentMillis;
      
      // Play Sound
      if (currentBeat == 0) {
          soundManager.playDownbeat();
      } else {
          soundManager.playBeat();
      }
      
      // Advance Beat
      currentBeat++;
      if (currentBeat >= beatsPerBar) {
        currentBeat = 0;
        
        // Sequence Logic
        if (isSequenceMode && !sequence.empty()) {
           barsPlayedInStep++;
           if (barsPlayedInStep >= sequence[currentStepIndex].bars) {
              barsPlayedInStep = 0;
              currentStepIndex++;
              if (currentStepIndex >= sequence.size()) {
                 currentStepIndex = 0; // Loop
              }
              beatsPerBar = sequence[currentStepIndex].beatsPerBar;
              bpm = sequence[currentStepIndex].bpm;
              if (currentScreen == SCREEN_EDITOR) drawEditor();
           }
        }
      }
    }
  }

  // Touch Logic (Throttled to 20ms)
  static unsigned long lastTouchCheck = 0;
  if (millis() - lastTouchCheck > 20) {
      lastTouchCheck = millis();
      
      if (digitalRead(XPT2046_IRQ) == LOW) {
        TouchPoint p = ts.getTouch();
        // Check zRaw for pressure to avoid false positives if needed, though IRQ is usually reliable
        if (p.zRaw > 200) {
            int touchX = map(p.xRaw, 200, 3700, 0, 320);
            int touchY = map(p.yRaw, 240, 3800, 0, 240);
            if (touchX < 0) touchX = 0; if (touchX > 320) touchX = 320;
            if (touchY < 0) touchY = 0; if (touchY > 240) touchY = 240;
        
            if (currentScreen == SCREEN_EDITOR) {
               if (millis() - lastTouchTime > 200) {
                  handleTouchEditor(touchX, touchY);
                  lastTouchTime = millis();
               }
            } else if (currentScreen == SCREEN_SOUND_SELECT) {
               if (millis() - lastTouchTime > 200) {
                  handleTouchSoundSelect(touchX, touchY);
                  lastTouchTime = millis();
               }
            } else {
        
            // Slider
            if (touchY > SLIDER_Y - 15 && touchY < SLIDER_Y + SLIDER_H + 15) {
               float pos = (float)(touchX - SLIDER_X) / (float)SLIDER_W;
               if (pos < 0) pos = 0; if (pos > 1) pos = 1;
               bpm = 40 + (int)(pos * (208 - 40));
               updateBPM();
            } else {
        
                // Buttons
                if (millis() - lastTouchTime > 200) { 
                  for (int i = 0; i < numButtons; i++) {
                    if (touchX > buttons[i].x && touchX < buttons[i].x + buttons[i].w &&
                        touchY > buttons[i].y && touchY < buttons[i].y + buttons[i].h) {
                      
                      tft.drawRoundRect(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, 5, TFT_WHITE);
                      buttons[i].action();
                      
                      if (buttons[i].isCustomDraw) {
                         drawButton(i); 
                      } else {
                         tft.drawRoundRect(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, 5, buttons[i].color);
                         if (i == 4) drawButton(i);
                      }
                      
                      lastTouchTime = millis();
                      break; 
                    }
                  }
                }
            }
        }
      }
  }
}
}
