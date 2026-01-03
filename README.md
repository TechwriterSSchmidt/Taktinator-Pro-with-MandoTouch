# Taktinator Pro with MandoTouch

A digital metronome designed for the **ESP32-Cheap-Yellow-Display (CYD)**, featuring a custom Mandolin-themed UI.

## Features
- **BPM Range:** 40 - 208 BPM (Standard Metronome Range).
- **Controls:**
  - **Linear Slider:** Quickly swipe to set approximate tempo.
  - **Fine Tune Buttons:** Adjust tempo by +/- 1 or +/- 10 BPM.
  - **Volume Control:** On-screen volume adjustment.
- **Visuals:**
  - **MandoTouch Button:** A custom-drawn Mandolin icon serves as the Start/Stop button.
  - **Status Indication:** Button changes color (Green = Ready, Red = Playing) and animates on touch.
- **Audio:** Generates a crisp 1kHz click sound (50ms duration) on each beat.

## Hardware
- **Board:** ESP32-2432S028R (Cheap Yellow Display)
- **Audio Output:** GPIO 26 (PWM)
- **Touch Controller:** XPT2046 (VSPI)
- **Display Driver:** ILI9341 (HSPI)

## Technical Details
This project solves the common CYD issue of conflicting SPI buses for Touch and Display.
- **Display (TFT_eSPI):** Forced to use **HSPI** (Pins 12, 13, 14).
- **Touch (XPT2046):** Configured on a separate **VSPI** instance (Pins 25, 32, 33, 39).

## Build & Upload
1. Open in VS Code with PlatformIO.
2. Build and Upload to your ESP32 CYD.
3. Tap the Mandolin to start the beat!

## Credits & Resources
- **Metronome Sounds:** Special thanks to **Alejandro Hernandez** for the high-quality metronome samples.
  - Download them here: [Reaper Tips - Metronome Sounds](https://www.reapertips.com/resources/metronome-sounds)
  - If you enjoy these sounds, please consider supporting his project!
