#ifndef DISPLAY_SELECTION_H
#define DISPLAY_SELECTION_H

// Pick the include + class that match your Waveshare e-paper panel (see wiki "Support Models").
// https://www.waveshare.com/wiki/E-Paper_ESP32_Driver_Board
// GxEPD2: https://github.com/ZinggJM/GxEPD2 — GxEPD2_BW = black/white only.
//
// 7.5" HD 800×480 — two different controller families (same resolution, NOT interchangeable):
//   GxEPD2_750_T7        → GDEW075T7, GD7965
//   GxEPD2_750_GDEY075T7 → GDEY075T7, UC8179
// If you see text briefly then a white/wrong image, set USE_GDEY075T7_DRIVER to 1 (or 0) and reflash.
#define USE_GDEY075T7_DRIVER 1

#include <GxEPD2_BW.h>
#if USE_GDEY075T7_DRIVER
#include <gdey/GxEPD2_750_GDEY075T7.h>
#define EPD_DRIVER_CLASS GxEPD2_750_GDEY075T7
#else
#include <epd/GxEPD2_750_T7.h>
#define EPD_DRIVER_CLASS GxEPD2_750_T7
#endif
//
// Other sizes if the label is not 800×480 HD:
//   #include <epd/GxEPD2_750.h>       #define EPD_DRIVER_CLASS GxEPD2_750       // 7.5" 640×384 (older)
//
// For black/white/red (3-color) panels use GxEPD2_3C + the *c driver from GxEPD2 docs (not BW).
//
// Other sizes (examples):
//   #include <epd/GxEPD2_290_T94_V2.h> #define EPD_DRIVER_CLASS GxEPD2_290_T94_V2 // 2.9" 128×296
//   #include <epd/GxEPD2_213_BN.h>     #define EPD_DRIVER_CLASS GxEPD2_213_BN     // 2.13" 122×250

#endif
