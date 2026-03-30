#pragma once
#include <Arduino.h>
#include <TinyGPSPlus.h>

// ============================================================================
// LOCATION APP
// ============================================================================

extern bool inLocation;
extern TinyGPSPlus gps;
extern HardwareSerial &GPSSerial;

extern const int GPS_RX_PIN;
extern const int GPS_TX_PIN;
extern const uint32_t GPS_BAUD;

extern bool gpsBootTimeSet;

// Pacific timezone offset (DST-aware)
int getPacificOffsetMin(int y, int mon, int d, int h);

// GPS boot sync (called from setup() in main)

// App lifecycle
void locationEnter();
void locationExit();
void handleLocation();