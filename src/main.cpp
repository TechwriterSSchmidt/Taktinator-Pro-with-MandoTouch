#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Bitbang.h>
#include <vector>
#include "SoundManager.h"
#include "ProgramManager.h"

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
// Slider removed
#define VOL_BAR_X 75
#define VOL_BAR_Y 205
#define VOL_BAR_W 170
#define VOL_BAR_H 20

// --- State Variables ---
int bpm = 120;
int volume = 127; 
bool isPlaying = false;
unsigned long lastTouchTime = 0;
unsigned long lastClickTime = 0;
unsigned long lastVisualBeatTime = 0;
bool visualBeatActive = false;

// Time Signature State
int beatsPerBar = 4; // Default 4/4
int currentBeat = 0; // 0 to beatsPerBar-1

// --- Sequence / Program Mode ---
enum ScreenState { SCREEN_MAIN, SCREEN_EDITOR, SCREEN_SOUND_SELECT, SCREEN_PROGRAM_SELECT };
ScreenState currentScreen = SCREEN_MAIN;

// SequenceStep is now in ProgramManager.h

std::vector<SequenceStep> sequence;
ProgramManager programManager;
String currentProgramPath = ""; // Path of currently loaded program

bool isSequenceMode = false;
bool isLoopMode = true; // Default to looping
int currentStepIndex = 0;
int barsPlayedInStep = 0;
int selectedStepIndex = -1; // For Editor
int editorScroll = 0; // For Editor Scrolling

// --- Sound Selection State ---
std::vector<String> wavFiles;
int soundListScroll = 0;
int selectedSoundIndex = -1;
SoundType targetSoundType = SOUND_DOWNBEAT; // Which sound are we selecting?

// --- Program Selection State ---
std::vector<String> programFiles;
int programListScroll = 0;
int selectedProgramIndex = -1;

// --- Forward Declarations ---
void updateBPM();
void updateVolume();
void updateTimeSig();
// void drawSlider(); // Removed
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
void toggleProgramSelect();
void drawUI();
void drawEditor();
void drawSoundSelect();
void drawProgramSelect();
void refreshSoundList();
void refreshProgramList();
void handleTouchProgramSelect(int x, int y);

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
  // Time Sig (Top Left)
  {5, 5, 70, 40, "4/4", TFT_PURPLE, cycleTimeSig, false}, // Index 0

  // BPM Controls (Row 1)
  {5, 55, 70, 60, "-10", TFT_BLUE, decreaseBPM10, false},
  {80, 55, 70, 60, "-1", TFT_NAVY, decreaseBPM1, false},
  {170, 55, 70, 60, "+1", TFT_NAVY, increaseBPM1, false},
  {245, 55, 70, 60, "+10", TFT_BLUE, increaseBPM10, false},
  
  // Action Row (Row 2)
  {5, 125, 145, 60, "START", TFT_DARKGREEN, toggleMetronome, false}, // Index 5 (Play/Stop)
  {170, 125, 70, 60, "PROG", TFT_NAVY, toggleProgramSelect, false},
  {245, 125, 70, 60, "SND", TFT_MAROON, toggleSoundSelect, false},

  // Volume Controls (Bottom Row)
  {5, 195, 60, 40, "-", TFT_DARKGREY, decreaseVol, false},
  {255, 195, 60, 40, "+", TFT_DARKGREY, increaseVol, false}
};

const int numButtons = sizeof(buttons) / sizeof(Button);

// --- Helper Functions ---

void drawSmallVerticalMandolin(int x, int y, uint16_t color) {
  // x,y is top-left of the bounding box (approx 20x40)
  int cx = x + 10;
  int cy_body = y + 30;
  
  // Body
  tft.fillEllipse(cx, cy_body, 8, 10, color);
  tft.drawEllipse(cx, cy_body, 8, 10, TFT_WHITE);
  tft.fillCircle(cx, cy_body, 3, TFT_BLACK); // Sound hole
  
  // Neck
  tft.fillRect(cx - 2, y + 10, 4, 15, color); // Neck matches body color
  
  // Headstock
  tft.fillRoundRect(cx - 4, y, 8, 10, 2, color);
  tft.drawRoundRect(cx - 4, y, 8, 10, 2, TFT_WHITE);
  
  // Strings (Black if body is white, White if body is black)
  uint16_t stringColor = (color == TFT_WHITE) ? TFT_BLACK : TFT_WHITE;
  tft.drawLine(cx - 1, y + 2, cx - 1, cy_body - 2, stringColor);
  tft.drawLine(cx + 1, y + 2, cx + 1, cy_body - 2, stringColor);
}

void drawMandolin(int x, int y, int w, int h, uint16_t bodyColor) {
  int cx = x + w / 2;
  int cy = y + h / 2;
  
  // Body (Teardrop shape - Ellipse)
  // Shifted right to make room for neck
  int bodyX = cx + 10; 
  int rx = 18; // Smaller
  int ry = 13; // Smaller
  
  // Draw Body
  tft.fillEllipse(bodyX, cy, rx, ry, bodyColor); 
  tft.drawEllipse(bodyX, cy, rx, ry, TFT_WHITE);
  
  // Sound hole (Black circle)
  tft.fillCircle(bodyX - 4, cy, 5, TFT_BLACK); 
  
  // Neck (Connects to body)
  int neckW = 25; // Shorter neck
  int neckH = 6;
  int neckX = (bodyX - rx) - neckW + 2; 
  
  tft.fillRect(neckX, cy - neckH/2, neckW, neckH, TFT_BROWN);
  
  // Headstock
  int headW = 10;
  int headH = 12;
  int headX = neckX - headW;
  
  // Headstock shape
  tft.fillRoundRect(headX, cy - headH/2, headW, headH, 2, bodyColor);
  tft.drawRoundRect(headX, cy - headH/2, headW, headH, 2, TFT_WHITE);
  
  // Strings
  tft.drawLine(headX + 2, cy - 2, bodyX + 4, cy - 2, TFT_SILVER);
  tft.drawLine(headX + 2, cy + 2, bodyX + 4, cy + 2, TFT_SILVER);
}

void drawButton(int index) {
  Button b = buttons[index];
  
  // Special handling for Play/Stop button color/label
  if (index == 5) { // Play/Stop Button
      uint16_t bg = isPlaying ? TFT_RED : TFT_DARKGREEN;
      String label = isPlaying ? "STOP" : "START";
      tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, bg);
      tft.drawRoundRect(b.x, b.y, b.w, b.h, 8, TFT_WHITE);
      
      // Draw Mandolin (Left side)
      // Shifted right to ensure headstock is inside button
      drawMandolin(b.x + 20, b.y, 50, b.h, TFT_ORANGE);

      tft.setTextColor(TFT_WHITE, bg);
      tft.setTextDatum(MC_DATUM);
      tft.setTextSize(2);
      // Shift text to the right to make room for Mandolin
      tft.drawString(label, b.x + b.w / 2 + 37, b.y + b.h / 2);
      return;
  }

  tft.fillRoundRect(b.x, b.y, b.w, b.h, 5, b.color);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 5, TFT_WHITE); // Border
  tft.setTextColor(TFT_WHITE, b.color);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2); 
    
  // Special handling for Time Sig label update
  if (index == 0) { // Time Sig Button Index (Now 0)
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

// drawSlider removed

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
  // Clear BPM Area (Center Top)
  tft.fillRect(80, 0, 160, 50, TFT_BLACK); 
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(4); // Large Font
  tft.drawNumber(bpm, 160, 25);
  
  tft.setTextSize(1);
  tft.drawString("BPM", 220, 35, 2); 
  
  // Draw Idle Mandolin (Black body, White outline)
  drawSmallVerticalMandolin(260, 5, TFT_BLACK);
}

void updateVolume() {
  if (currentScreen != SCREEN_MAIN) return;
  drawVolumeBar();
}

// --- Editor Screen ---
void drawEditor() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM); // Left aligned
  tft.setTextSize(2);
  
  String title = "New Program";
  if (currentProgramPath.length() > 0) {
      title = currentProgramPath;
      int lastSlash = title.lastIndexOf('/');
      if (lastSlash >= 0) title = title.substring(lastSlash + 1);
      if (title.endsWith(".txt")) title = title.substring(0, title.length() - 4);
  }
  tft.drawString(title, 10, 5); 

  // Scroll Buttons
  if (sequence.size() > 5) {
      tft.setTextSize(1);
      tft.setTextDatum(MC_DATUM);
      // Up (Aligned with left edit buttons at 220)
      uint16_t cUp = (editorScroll > 0) ? TFT_WHITE : TFT_DARKGREY;
      tft.drawRoundRect(220, 2, 35, 25, 3, cUp);
      tft.setTextColor(cUp, TFT_BLACK);
      tft.drawString("/\\", 237, 14);
      
      // Down (Aligned with right edit buttons at 270)
      uint16_t cDown = (editorScroll + 5 < sequence.size()) ? TFT_WHITE : TFT_DARKGREY;
      tft.drawRoundRect(270, 2, 35, 25, 3, cDown);
      tft.setTextColor(cDown, TFT_BLACK);
      tft.drawString("\\/", 287, 14);
  }

  tft.setTextSize(2);
  tft.setTextDatum(TL_DATUM);
  // Reduced spacing to fit buttons
  for (int i = editorScroll; i < sequence.size() && i < editorScroll + 5; i++) {
    int displayIndex = i - editorScroll;
    int y = 35 + displayIndex * 32;
    
    uint16_t textColor = TFT_WHITE;
    uint16_t bgColor = TFT_BLACK;

    if (isSequenceMode && i == currentStepIndex) {
        // Playing -> Dark Green BG, White Text
        bgColor = TFT_DARKGREEN;
        textColor = TFT_WHITE;
        if (i == selectedStepIndex) {
             // Playing AND Selected -> Cyan BG, Black Text (High Contrast)
             // Or maybe Navy BG, White Text?
             // User said "Text color always the same".
             bgColor = TFT_NAVY;
             textColor = TFT_WHITE;
        }
    } else if (i == selectedStepIndex) {
        // Selected only -> Blue BG, White Text
        bgColor = TFT_BLUE;
        textColor = TFT_WHITE;
    }
    
    // Draw background bar if inverted
    if (bgColor != TFT_BLACK) {
        tft.fillRect(10, y - 2, 200, 30, bgColor);
    }
    
    tft.setTextColor(textColor, bgColor);
    
    String sigLabel = String(sequence[i].beatsPerBar) + "/4";
    if (sequence[i].beatsPerBar == 6) sigLabel = "6/8";
    if (sequence[i].beatsPerBar == 7) sigLabel = "7/8";
    if (sequence[i].beatsPerBar == 9) sigLabel = "9/8";

    String line = String(i + 1) + ". " + String(sequence[i].bars) + "x " + 
                  sigLabel + " " + String(sequence[i].bpm);
    tft.drawString(line, 20, y);
  }

  // Reset Text Color for Buttons
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  int yBase = 200; // Moved up from 220
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  
  // ADD (x=10, w=50)
  tft.drawRoundRect(10, yBase, 50, 35, 5, TFT_GREEN); tft.drawString("ADD", 35, yBase + 17);
  
  // DEL (x=65, w=50)
  tft.drawRoundRect(65, yBase, 50, 35, 5, TFT_RED); tft.drawString("DEL", 90, yBase + 17);
  
  // BACK (x=120, w=50)
  tft.drawRoundRect(120, yBase, 50, 35, 5, TFT_BLUE); tft.drawString("RET", 145, yBase + 17);
  
  // LOOP (x=175, w=60)
  uint16_t loopColor = isLoopMode ? TFT_CYAN : TFT_DARKGREY;
  tft.drawRoundRect(175, yBase, 60, 35, 5, loopColor); 
  tft.drawString(isLoopMode ? "LOOP" : "ONCE", 205, yBase + 17);

  // SAVE (x=240, w=70) - Replaces RUN
  if (isSequenceMode) {
      tft.drawRoundRect(240, yBase, 70, 35, 5, TFT_RED); tft.drawString("STOP", 275, yBase + 17);
  } else {
      tft.drawRoundRect(240, yBase, 70, 35, 5, TFT_ORANGE); tft.drawString("SAVE", 275, yBase + 17);
  }

  if (selectedStepIndex >= 0 && selectedStepIndex < sequence.size()) {
     int xBase = 220;
     int yStart = 40;
     tft.setTextSize(1);
     tft.setTextColor(TFT_WHITE, TFT_BLACK);
     
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
    int tabH = 30;
    
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
    tft.drawRect(10, 40, 240, 140, TFT_WHITE); 
    tft.setTextSize(2);
    tft.setTextDatum(TL_DATUM);
    
    int y = 45; 
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
    tft.drawRoundRect(260, 40, 50, 65, 5, TFT_DARKGREY);
    tft.drawString("/\\", 285, 72); // Up
    
    tft.drawRoundRect(260, 115, 50, 65, 5, TFT_DARKGREY);
    tft.drawString("\\/", 285, 147); // Down
    
    // Controls
    int yBase = 190; 
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
    if (y < 40) {
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
    if (x > 260 && y > 40 && y < 105) {
        if (soundListScroll > 0) {
            soundListScroll--;
            drawSoundSelect();
        }
        return;
    }

    // Scroll Down
    if (x > 260 && y > 115 && y < 180) {
        if (soundListScroll + 5 < wavFiles.size()) {
            soundListScroll++;
            drawSoundSelect();
        }
        return;
    }
    
    // List Selection
    if (x < 250 && y > 40 && y < 180) { 
        int idx = (y - 45) / 28 + soundListScroll;
        if (idx >= 0 && idx < wavFiles.size()) {
            selectedSoundIndex = idx;
            drawSoundSelect();
        }
        return;
    }
    
    int yBase = 190;
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
            
            drawSoundSelect(); 
        }
    }
}

void handleTouchEditor(int x, int y) {
  // Scroll Buttons
  if (y < 30 && x > 210) {
      if (x > 220 && x < 255 && editorScroll > 0) {
          editorScroll--;
          drawEditor();
          return;
      }
      if (x > 270 && x < 305 && editorScroll + 5 < sequence.size()) {
          editorScroll++;
          drawEditor();
          return;
      }
  }

  // List Selection
  for (int i = editorScroll; i < sequence.size() && i < editorScroll + 5; i++) {
    int displayIndex = i - editorScroll;
    int yPos = 35 + displayIndex * 32; 
    if (y > yPos && y < yPos + 30 && x < 200) {
      selectedStepIndex = i;
      drawEditor();
      return;
    }
  }

  int yBase = 200; // Adjusted
  // ADD
  if (y > yBase && y < yBase + 35 && x > 10 && x < 60) {
    // Removed limit check
    sequence.push_back({4, 4, 120});
    selectedStepIndex = sequence.size() - 1;
    // Auto-scroll
    if (selectedStepIndex >= editorScroll + 5) {
        editorScroll = selectedStepIndex - 4;
    }
    drawEditor();
  }
  // DEL
  if (y > yBase && y < yBase + 35 && x > 65 && x < 115) {
    if (!sequence.empty() && selectedStepIndex >= 0) {
      sequence.erase(sequence.begin() + selectedStepIndex);
      if (selectedStepIndex >= sequence.size()) selectedStepIndex = sequence.size() - 1;
      drawEditor();
    }
  }
  // BACK
  if (y > yBase && y < yBase + 35 && x > 120 && x < 170) {
    toggleEditor();
  }
  // LOOP
  if (y > yBase && y < yBase + 35 && x > 175 && x < 235) {
    isLoopMode = !isLoopMode;
    drawEditor();
  }
  // SAVE / STOP
  if (y > yBase && y < yBase + 35 && x > 240 && x < 310) {
    if (isSequenceMode) {
        // STOP
        isSequenceMode = false;
        isPlaying = false;
        currentStepIndex = 0;
        barsPlayedInStep = 0;
        currentBeat = 0;
        drawEditor();
        return;
    }

    if (sequence.empty()) return;
    
    String savePath = currentProgramPath;
    if (savePath.length() == 0) {
        savePath = programManager.getNextProgramName();
    }
    
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Saving...", 160, 120);
    tft.drawString(savePath, 160, 150); // Show path being saved
    
    if (programManager.saveProgram(savePath, sequence, soundManager.getDownbeatPath(), soundManager.getBeatPath())) {
        delay(500);
        // Go back to Program Select
        currentScreen = SCREEN_PROGRAM_SELECT;
        refreshProgramList();
    } else {
        tft.drawString("Error Saving!", 160, 140);
        delay(1000);
        drawEditor();
    }
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
       if (x > xBase + 50 && x < xBase + 80) { sequence[selectedStepIndex].bpm += 5; if(sequence[selectedStepIndex].bpm > 250) sequence[selectedStepIndex].bpm = 250; }
       drawEditor();
     }
  }
}

void updateTimeSig() {
  if (currentScreen != SCREEN_MAIN) return;
  drawButton(0); 
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
  if (currentScreen == SCREEN_PROGRAM_SELECT) {
    drawProgramSelect();
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
  if (currentScreen == SCREEN_MAIN || currentScreen == SCREEN_PROGRAM_SELECT) {
    currentScreen = SCREEN_EDITOR;
    if (sequence.empty()) {
      sequence.push_back({4, 4, 120});
    }
    // Select the first item by default so controls appear
    if (!sequence.empty()) {
        selectedStepIndex = 0;
    }
    drawEditor();
  } else {
    // Back to Program Select instead of Main
    currentScreen = SCREEN_PROGRAM_SELECT;
    refreshProgramList();
  }
}

void toggleProgramSelect() {
    if (currentScreen == SCREEN_MAIN) {
        currentScreen = SCREEN_PROGRAM_SELECT;
        refreshProgramList();
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

void increaseBPM10() { bpm += 10; if (bpm > 250) bpm = 250; updateBPM(); }
void decreaseBPM10() { bpm -= 10; if (bpm < 40) bpm = 40; updateBPM(); }
void increaseBPM1() { bpm += 1; if (bpm > 250) bpm = 250; updateBPM(); }
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

// --- Program Select Screen ---
void drawProgramSelect() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(2);
    tft.drawString("Select Program", 10, 5);

    // File List
    tft.drawRect(10, 30, 240, 140, TFT_WHITE); 
    tft.setTextSize(2);
    tft.setTextDatum(TL_DATUM);
    
    int y = 35; 
    for (int i = programListScroll; i < programFiles.size() && i < programListScroll + 5; i++) {
        if (i == selectedProgramIndex) tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        else tft.setTextColor(TFT_WHITE, TFT_BLACK);
        
        String dispName = programFiles[i];
        // Remove /programs/ prefix and .txt suffix for display
        int lastSlash = dispName.lastIndexOf('/');
        if (lastSlash >= 0) dispName = dispName.substring(lastSlash + 1);
        if (dispName.endsWith(".txt")) dispName = dispName.substring(0, dispName.length() - 4);

        if (dispName.length() > 20) {
            dispName = dispName.substring(0, 17) + "...";
        }
        tft.drawString(dispName, 25, y); 
        y += 28; 
    }

    // Scroll Buttons
    tft.setTextDatum(MC_DATUM);
    tft.drawRoundRect(260, 30, 50, 65, 5, TFT_DARKGREY);
    tft.drawString("/\\", 285, 62); // Up
    
    tft.drawRoundRect(260, 105, 50, 65, 5, TFT_DARKGREY);
    tft.drawString("\\/", 285, 137); // Down
    
    // Controls
    int yBase = 185; 
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    
    // BACK (x=10, w=60)
    tft.drawRoundRect(10, yBase, 60, 35, 5, TFT_BLUE); tft.drawString("BACK", 40, yBase + 17);
    
    // NEW (x=80, w=60)
    tft.drawRoundRect(80, yBase, 60, 35, 5, TFT_GREEN); tft.drawString("NEW", 110, yBase + 17);
    
    // EDIT (x=150, w=60)
    tft.drawRoundRect(150, yBase, 60, 35, 5, TFT_NAVY); tft.drawString("EDIT", 180, yBase + 17);
    
    // PLAY (x=220, w=60)
    uint16_t playColor = (isSequenceMode) ? TFT_RED : TFT_DARKGREEN;
    String playLabel = (isSequenceMode) ? "STOP" : "PLAY";
    tft.drawRoundRect(220, yBase, 60, 35, 5, playColor); tft.drawString(playLabel, 250, yBase + 17);
    
    // DEL (x=290, w=25) - Small
    tft.drawRoundRect(290, yBase, 25, 35, 5, TFT_RED); 
    tft.setTextSize(1);
    tft.drawString("X", 302, yBase + 17);
}

void refreshProgramList() {
    programFiles = programManager.listPrograms();
    selectedProgramIndex = -1;
    programListScroll = 0;
    drawProgramSelect();
}

void handleTouchProgramSelect(int x, int y) {
    // Scroll Up
    if (x > 260 && y > 30 && y < 95) {
        if (programListScroll > 0) {
            programListScroll--;
            drawProgramSelect();
        }
        return;
    }

    // Scroll Down
    if (x > 260 && y > 105 && y < 170) {
        if (programListScroll + 5 < programFiles.size()) {
            programListScroll++;
            drawProgramSelect();
        }
        return;
    }
    
    // List Selection
    if (x < 250 && y > 30 && y < 170) { 
        int idx = (y - 35) / 28 + programListScroll;
        if (idx >= 0 && idx < programFiles.size()) {
            selectedProgramIndex = idx;
            drawProgramSelect();
        }
        return;
    }
    
    int yBase = 185;
    if (y > yBase - 10 && y < yBase + 50) {
        // BACK
        if (x > 10 && x < 70) {
            toggleProgramSelect();
            return;
        }
        // NEW
        if (x > 80 && x < 140) {
            sequence.clear();
            sequence.push_back({4, 4, 120});
            currentProgramPath = programManager.getNextProgramName(); // Pre-assign name
            currentScreen = SCREEN_EDITOR;
            selectedStepIndex = 0;
            drawEditor();
            return;
        }
        // EDIT
        if (x > 150 && x < 210) {
            if (selectedProgramIndex >= 0 && selectedProgramIndex < programFiles.size()) {
                String dbPath, bPath;
                if (programManager.loadProgram(programFiles[selectedProgramIndex], sequence, dbPath, bPath)) {
                    soundManager.loadSound(SOUND_DOWNBEAT, dbPath);
                    soundManager.loadSound(SOUND_BEAT, bPath);
                    currentProgramPath = programFiles[selectedProgramIndex];
                    currentScreen = SCREEN_EDITOR;
                    selectedStepIndex = 0;
                    drawEditor();
                }
            }
            return;
        }
        // PLAY/STOP
        if (x > 220 && x < 280) {
            if (isSequenceMode) {
                // Stop
                isSequenceMode = false;
                isPlaying = false;
                currentStepIndex = 0;
                barsPlayedInStep = 0;
                currentBeat = 0;
                drawProgramSelect();
            } else {
                // Play
                if (selectedProgramIndex >= 0 && selectedProgramIndex < programFiles.size()) {
                    String dbPath, bPath;
                    if (programManager.loadProgram(programFiles[selectedProgramIndex], sequence, dbPath, bPath)) {
                        soundManager.loadSound(SOUND_DOWNBEAT, dbPath);
                        soundManager.loadSound(SOUND_BEAT, bPath);
                        currentProgramPath = programFiles[selectedProgramIndex];
                        if (!sequence.empty()) {
                            isSequenceMode = true;
                            isPlaying = true;
                            currentStepIndex = 0;
                            barsPlayedInStep = 0;
                            currentBeat = 0;
                            beatsPerBar = sequence[0].beatsPerBar;
                            bpm = sequence[0].bpm;
                            
                            // Switch to Editor View for Playback
                            currentScreen = SCREEN_EDITOR;
                            selectedStepIndex = -1; // Deselect specific step so we just see the playback highlight
                            drawEditor();
                        }
                    }
                }
            }
            return;
        }
        // DEL
        if (x > 290 && x < 315) {
            if (selectedProgramIndex >= 0 && selectedProgramIndex < programFiles.size()) {
                programManager.deleteProgram(programFiles[selectedProgramIndex]);
                refreshProgramList();
            }
            return;
        }
    }
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

  // Init Program Manager
  programManager.begin();
  
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
      
      // Visual Beat (Blink)
      if (currentScreen == SCREEN_MAIN) {
          drawSmallVerticalMandolin(260, 5, TFT_WHITE);
          lastVisualBeatTime = millis();
          visualBeatActive = true;
      }

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
                 if (isLoopMode) {
                    currentStepIndex = 0; // Loop
                 } else {
                    // Stop
                    isSequenceMode = false;
                    isPlaying = false;
                    currentStepIndex = 0;
                    barsPlayedInStep = 0;
                    currentBeat = 0;
                    // Restore selection to first item when stopping automatically
                    if (!sequence.empty()) selectedStepIndex = 0;
                    if (currentScreen == SCREEN_EDITOR) drawEditor();
                    return; // Stop processing
                 }
              }
              beatsPerBar = sequence[currentStepIndex].beatsPerBar;
              bpm = sequence[currentStepIndex].bpm;
              
              // Auto-scroll to keep current step visible
              if (currentScreen == SCREEN_EDITOR) {
                  if (currentStepIndex < editorScroll) {
                      editorScroll = currentStepIndex;
                  } else if (currentStepIndex >= editorScroll + 5) {
                      editorScroll = currentStepIndex - 4;
                  }
                  drawEditor();
              }
           }
        }
      }
    }
  }
  
  // Turn off visual beat
  if (visualBeatActive && millis() - lastVisualBeatTime > 100) {
      if (currentScreen == SCREEN_MAIN) {
          drawSmallVerticalMandolin(260, 5, TFT_BLACK);
      }
      visualBeatActive = false;
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
            } else if (currentScreen == SCREEN_PROGRAM_SELECT) {
               if (millis() - lastTouchTime > 200) {
                  handleTouchProgramSelect(touchX, touchY);
                  lastTouchTime = millis();
               }
            } else {
        
                // Buttons
                if (millis() - lastTouchTime > 200) { 
                  for (int i = 0; i < numButtons; i++) {
                    if (touchX > buttons[i].x && touchX < buttons[i].x + buttons[i].w &&
                        touchY > buttons[i].y && touchY < buttons[i].y + buttons[i].h) {
                      
                      tft.drawRoundRect(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, 5, TFT_WHITE);
                      buttons[i].action();
                      
                      // Redraw button to clear selection highlight ONLY if we are still on the main screen
                      if (currentScreen == SCREEN_MAIN) {
                          drawButton(i);
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
