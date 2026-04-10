/*
 * CHIP-BOY — ESP32S3 based smart watch with environmental sensors and mp3 player 
 * Copyright (c) 2026 Victor Ranulfo Molina
 * SPDX-License-Identifier: MIT
 *
 * https://github.com/hungggryvic/Chip-Boy
 */

struct BtnState;

// Enums at top so Arduino's auto-prototypes don't break references.
enum Field { F_HOUR, F_MIN, F_SEC, F_AMPM, F_MONTH, F_DAY, F_YEAR, F_WDAY, F_MOON };
Field currentField = F_HOUR;  // only one global instance

// Alarm app fields (2-D cursor like SET: HMS / AM/PM / ON/OFF)
enum AlarmField { A_HOUR, A_MIN, A_SEC, A_AMPM, A_ENABLE };

// Includes & libs
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <Adafruit_VL53L1X.h>

// 1. CAMERA FIRST (defines its own sensor_t)
#define CAMERA_MODEL_XIAO_ESP32S3
#include "esp_camera.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "camera_pins.h"
#include "ESP_I2S.h"

#include <DFRobotDFPlayerMini.h>

#include "IronTidesApp.h"
#include "BlackjackApp.h"
#include "ObscurusApp.h"
#include "SkyApp.h"
#include "RacingApp.h"
#include "SlotsApp.h"
#include "BatteryApp.h"
#include "RecordingsApp.h"
#include "RadioApp.h"
#include "LocationApp.h"
#include "NavigationApp.h"
#include "CameraApp.h"
#include "MicrophoneApp.h"

// ============================================================================
// SPLASH BITMAP — 128×64 chipboy logo
// ============================================================================
const unsigned char epd_bitmap_chipboy [] PROGMEM = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x80, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0xf0, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x39, 0xc0, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x01, 0xf8, 0xc0, 0x00, 0x00, 0x00, 0x7f, 0x80, 0x00, 0x39, 0xe0, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x03, 0xfc, 0xc0, 0x00, 0x00, 0x00, 0xff, 0xc0, 0x00, 0xf7, 0xc0, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x07, 0x8d, 0xc0, 0x00, 0x00, 0x00, 0x1f, 0xe0, 0x00, 0x3f, 0xc0, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x0f, 0x0d, 0xc1, 0xc0, 0x00, 0x00, 0x1c, 0xe0, 0x00, 0x1f, 0x80, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x7e, 0x1d, 0x81, 0xc0, 0x00, 0x00, 0x38, 0xe0, 0x00, 0x04, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x39, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x7f, 0xf3, 0xe3, 0x0f, 0x80, 0x00, 0x3f, 0x83, 0x8c, 0xc0, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x3c, 0x07, 0xf3, 0x8f, 0xc0, 0x00, 0x7f, 0x87, 0xcc, 0xc0, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x78, 0x0f, 0xf3, 0x9c, 0xc3, 0xf0, 0x71, 0xce, 0xcc, 0xc0, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x78, 0x0e, 0x77, 0x18, 0xc3, 0xf0, 0x71, 0xdc, 0xfd, 0x80, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x78, 0x1e, 0x67, 0x39, 0xc0, 0x00, 0xe1, 0xdc, 0xf9, 0x80, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x78, 0x1c, 0xff, 0xff, 0x80, 0x00, 0xe1, 0xcf, 0xdf, 0x80, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x7c, 0x3c, 0xff, 0xff, 0x00, 0x01, 0xe3, 0xc7, 0x8f, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x3f, 0xfc, 0x00, 0x70, 0x00, 0x0f, 0xff, 0x80, 0x03, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x3f, 0xf8, 0x00, 0xf0, 0x00, 0x3f, 0xff, 0x00, 0x7f, 0xe0, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0xe0, 0x00, 0x7f, 0xfc, 0x00, 0xff, 0xc0, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xe0, 0x00, 0xe3, 0x80, 0x01, 0xcc, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xc0, 0x00, 0xe7, 0x80, 0x01, 0x8c, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x80, 0x00, 0xff, 0x00, 0x00, 0xf8, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

bool inGame = false;
bool inGamesMenu = false;
bool inSky = false;  // true while the games submenu is showing
uint8_t gamesMenuCursor = 0;  // 0=IronTides, 1=Blackjack, etc.
bool gameplayRunning = false; // true only while a game is actively running (not paused)
bool g_gamePauseRequest = false; // consumed by active game to trigger its pause


struct BtnState {
  uint8_t pin;
  bool lastRead = HIGH;      // HIGH=released (pull-ups)
  bool stable = HIGH;
  unsigned long lastChange = 0;
  bool longFired = false;
  unsigned long pressStart = 0;
  bool suppressOnRelease = false;
};

// Rename Adafruit's sensor_t before any Adafruit libs load it
#define sensor_t adafruit_sensor_t
#include <Adafruit_Sensor.h>
#undef sensor_t

//include Adafruit sensor-using libraries
#include <Adafruit_BME280.h>
#include <math.h>
#include <vector>
#include <ctype.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Protect the internal include inside Adafruit_BNO055
#define sensor_t adafruit_sensor_t
#include <Adafruit_BNO055.h>
#undef sensor_t

// ROTARY ENCODER 
#define ENC_A_PIN D8
#define ENC_B_PIN D7


uint8_t encPrevAB = 0;
int8_t encAccum = 0;

const unsigned long ENC_SW_DEBOUNCE_MS = 35;

bool encSwStable = HIGH;       // HIGH = released (INPUT_PULLUP)
bool encSwLastRead = HIGH;
unsigned long encSwLastChange = 0;

// Encoder hold-for-shuffle detection (2 seconds)
static unsigned long encSwHoldStartMs  = 0;
static bool          encSwHoldArmed    = false;
static bool          encSwHoldConsumed = false;
static const unsigned long ENC_HOLD_SHUFFLE_MS = 2000;

// Returns true if encoder switch is currently held stable LOW
static inline bool encSwitchIsHeld() {
  return (encSwStable == LOW);
}


extern bool g_mcp_ok;
extern uint8_t g_mcp_gpio;
// Debounced "press event" for encoder switch (D6 -> GND, INPUT_PULLUP)
// Returns true exactly once per physical press (on the debounced falling edge).
static bool encSwitchPressedEvent() {
  bool r = (g_mcp_ok && (g_mcp_gpio & (1u << 7))) ? HIGH : LOW;  // GP7: HIGH=released, LOW=pressed

  // raw edge -> start debounce timer
  if (r != encSwLastRead) {
    encSwLastRead = r;
    encSwLastChange = millis();
  }

  // if stable long enough, accept new stable state
  if ((millis() - encSwLastChange) >= ENC_SW_DEBOUNCE_MS && r != encSwStable) {
    bool becameLow = (encSwStable == HIGH && r == LOW);  // falling edge
    encSwStable = r;
    if (becameLow) return true;  // one event per press
  }

  return false;
}

#define BATT_PIN D3   // voltage divider mid-point -> D3 (100k/100k)

// -------- LOCATION: forward decls (must be before locationEnter/handleLocation) --------
extern bool inMenu;
extern bool uiDirty;
extern uint8_t lastSecondDrawn;
extern unsigned long menuEnableButtonsAt;

extern bool screenEnabled;
extern bool displayReady;
extern Adafruit_SSD1306 display;

extern BtnState b1;
extern BtnState b3;

// false = DATA screen, true = MAP screen
static bool locationMapMode = false;

bool btnWasShortPressed(const BtnState &b_in);
void buzzerStartLow();

void redrawMenuNow();


bool inBattery = false;

// ---------------- DISTANCE (VL53L1X) ----------------
bool inDistance = false;

// Distance unit mode/state
static bool distImperial = false;   // false=MET, true=IMP
static uint8_t distUnitIdx = 0;     // 0,1,2 (pairs across MET/IMP)

Adafruit_VL53L1X tof;
bool tofReady = false;         // begin() succeeded at least once
bool tofRanging = false;       // currently ranging (enabled)
uint32_t tofLastReadMs = 0;
int16_t tofDistanceMm = -1;

extern bool inRadio;  // defined later in this file

// Encoder switch gestures:
// - Single press: IMMEDIATE play/pause
// - Double press: NEXT (and cancels the single toggle by toggling back)
// - Triple press: PREV (and cancels the single toggle by toggling back)
static void encSwitchGestureService() {
  const unsigned long MULTI_GAP_MS = 320;

  static uint8_t count = 0;
  static unsigned long lastPressAt = 0;
  static bool singleToggled = false;

  // New press event (debounced falling edge)
  if (encSwitchPressedEvent()) {
    count++;
    lastPressAt = millis();

    if (count == 1) {
      // Single press behaviour depends on game state:
      //  - If gameplay is running: this press pauses the game; do NOT touch radio.
      //    Mark gameplayRunning=false so subsequent presses can control the radio.
      //  - If we are in a paused game (inGame && !gameplayRunning): this press is
      //    handled by the game's own BTN1 resume — just toggle radio as normal.
      //  - Outside a game: toggle radio as normal.
      if (gameplayRunning) {
        // First press while playing — game will catch this via its own enc check.
        // Clear flag so the pause menu encoder presses reach the radio.
        gameplayRunning = false;
      } else {
        radioTogglePausePlay(false);
        recordingsEncToggle(); 
        singleToggled = true;
      }
      return;
    }

    if (count == 2) {
      // Double press: cancel the single toggle (audio back to original)
      if (singleToggled) {
        radioTogglePausePlay(false);
        recordingsEncNext(); 
        singleToggled = false;
      }
      return;
    }

    // Triple press: cancel single if needed, then PREV immediately
    if (count >= 3) {
      if (singleToggled) {
        radioTogglePausePlay(false);
        recordingsEncPrev();
        singleToggled = false;
      }
      radioSkipPrev();   // this already redraws if inRadio
      count = 0;
      return;
    }
  }

  // If we have 2 presses and the window expired, commit "NEXT"
  if (count == 2 && (millis() - lastPressAt) > MULTI_GAP_MS) {
    radioSkipNext();     // redraws if inRadio
    count = 0;
    singleToggled = false;
  }

  // If we have 1 press and the window expired, NOW redraw so icon updates
  if (count == 1 && (millis() - lastPressAt) > MULTI_GAP_MS) {
    if (inRadio) { uiDirty = true; redrawRadioNow(); }
    count = 0;
    singleToggled = false;
  }
}

static int8_t encoderReadStep() {
  uint8_t a = (digitalRead(ENC_A_PIN) == HIGH) ? 1 : 0;
  uint8_t b = (digitalRead(ENC_B_PIN) == HIGH) ? 1 : 0;
  uint8_t ab = (a << 1) | b;

  static const int8_t t[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0
  };

  int8_t delta = t[(encPrevAB << 2) | ab];
  encPrevAB = ab;

  if (delta != 0) {
    encAccum += delta;

    // HALF-STEP: 1 "click" ≈ 2 transitions
    if (encAccum >= 2)  { encAccum = 0; return +1; }
    if (encAccum <= -2) { encAccum = 0; return -1; }
  }
  return 0;
}
unsigned long lastBmagRead = 0;
const unsigned long BMAG_INTERVAL_MS = 200;  // update rate (~5 Hz)
float cachedBmag = 0;

static String calcFormat(double v);

struct Tok;  // forward
static bool calcTokenize(const String& in, std::vector<Tok>& out, String &err, double ansVal, bool hasAns);
static bool calcToRPN(const std::vector<Tok>& in, std::vector<Tok>& out, String &err);
static bool calcEvalRPN(const std::vector<Tok>& rpn, double &val, String &err);

// Alarm helpers
void drawAlarmSetView();
void drawAlarmRinging();
void serviceAlarmRinging();
void startAlarmRinging(int fromView);
void serviceTimerRinging();
void startTimerRinging(int fromView);
void stopTimerRinging();
void stopAlarmRinging();
static inline bool timeMatchesAlarmNow();
static inline String timeHHMMSS();

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
const uint8_t OLED_ADDR = 0x3C;  // 7-bit address

// Buzzer on D2 — short beep on MCP presses
#define BUZZER_PIN D2
#define BUZZ_HZ 2000
#define BEEP_MS 25

bool buzzing = false;
unsigned long buzzEnd = 0;

inline void buzzerStart() {
  tone(BUZZER_PIN, BUZZ_HZ);
  buzzing = true;
  buzzEnd = millis() + BEEP_MS;
}

inline void buzzerStop() {
  noTone(BUZZER_PIN);
  buzzing = false;
}

#define BUZZ_EXIT_HZ (BUZZ_HZ / 2)

void buzzerStartLow() {
  tone(BUZZER_PIN, BUZZ_EXIT_HZ);
  buzzing = true;
  buzzEnd = millis() + BEEP_MS;
}

// ── Low-battery alert ────────────────────────────────────────────────────────
// Reverse of the startup chirp (1600→800→400→200 Hz).
// Plays once at 10%, twice at 5%. Uses flags to prevent re-triggering
// as long as the battery stays in the same threshold band.
static bool battAlerted10 = false;
static bool battAlerted5  = false;

static void playLowBattChirp(int times) {
  for (int n = 0; n < times; n++) {
    tone(BUZZER_PIN, 1600); delay(60); noTone(BUZZER_PIN); delay(40);
    tone(BUZZER_PIN, 800);  delay(50); noTone(BUZZER_PIN); delay(40);
    tone(BUZZER_PIN, 400);  delay(50); noTone(BUZZER_PIN); delay(40);
    tone(BUZZER_PIN, 200);  delay(50); noTone(BUZZER_PIN);
    if (n < times - 1) delay(300);  // pause between repeats
  }
}

static void checkLowBatteryAlert() {
  static uint32_t lastCheckMs = 0;
  if (millis() - lastCheckMs < 15000UL) return;  // check every 15 seconds
  lastCheckMs = millis();

  int pct = readBatteryPercent();

  // Reset flags if battery has recovered above the band (e.g. user plugged in)
  if (pct > 10) { battAlerted10 = false; battAlerted5 = false; return; }
  if (pct > 5)  { battAlerted5  = false; }

  if (pct <= 5 && !battAlerted5) {
    battAlerted5  = true;
    battAlerted10 = true;  // also suppress the 10% alert going forward
    playLowBattChirp(2);
  } else if (pct <= 10 && !battAlerted10) {
    battAlerted10 = true;
    playLowBattChirp(1);
  }
}
// ── End low-battery alert ────────────────────────────────────────────────────

// Navigation-only beep: one octave lower than buzzerStartLow()
#define BUZZ_NAV_HZ (BUZZ_EXIT_HZ / 2)

void buzzerStartNav() {
  tone(BUZZER_PIN, BUZZ_NAV_HZ);
  buzzing = true;
  buzzEnd = millis() + BEEP_MS;
}

// Clock / state
int year = 2025;
int month = 10;
int day = 2;
int hour = 12;
int minute= 0;
int second= 0;

unsigned long lastTickMs = 0;

// GPS boot time sync
void normalizeDateTime();  // forward decl — defined later
static void normalizeDateTimeSigned() {
  while (second < 0) { second += 60; minute--; }
  while (second >= 60) { second -= 60; minute++; }

  while (minute < 0) { minute += 60; hour--; }
  while (minute >= 60) { minute -= 60; hour++; }

  while (hour < 0) { hour += 24; day--; }
  while (hour >= 24) { hour -= 24; day++; }

  normalizeDateTime();
}

// Tries to set clock from GPS once at boot.
// Returns true if it successfully set time/date.

bool setMode = false;
bool screenEnabled = true;
uint8_t lastSecondDrawn = 255;
bool uiDirty = true;
bool displayReady = false;

// Hourly chime (double chirp at top of every hour) — non-blocking
const unsigned long CHIRP_GAP_MS = 80;
bool chimeActive = false;
uint8_t chimeRemaining = 0;
unsigned long nextChirpAt = 0;
int lastChimeHour = -1;

void startChirp(uint8_t count) {
  if (count == 0 || chimeActive) return;
  chimeRemaining = count;
  chimeActive = true;
  nextChirpAt = 0;
  buzzerStart();
  chimeRemaining--;
  nextChirpAt = millis() + BEEP_MS + CHIRP_GAP_MS;
}

void serviceChirp() {
  if (!chimeActive) return;
  if (!buzzing && millis() >= nextChirpAt) {
    if (chimeRemaining > 0) {
      buzzerStart();
      chimeRemaining--;
      nextChirpAt = millis() + BEEP_MS + CHIRP_GAP_MS;
    } else {
      chimeActive = false;
    }
  }
}

void maybeHourlyChirp() {
  if (minute == 0 && second == 0) {
    int h = hour;
    if (h != lastChimeHour) {
      startChirp(2);
      lastChimeHour = h;
    }
  }
}

// Buttons (MCP only)
// BTN1 -> GP0, BTN2 -> GP6, BTN3 -> GP1
// UP -> GP4, DOWN -> GP3, RIGHT -> GP2, LEFT -> GP5
#define BTN1 0xA1
#define BTN2 0xA2
#define BTN3 0xA3
#define BTN_UP 0xA4
#define BTN_DOWN 0xA5
#define BTN_RIGHT 0xA6
#define BTN_LEFT 0xA7

extern bool inMenu;
extern bool inPeriodicTable;
extern bool inDataView;
extern bool inAlarmSet;
extern bool inTimerSet;
extern bool inCalendar;   
extern bool inCalculator;  
extern bool inTone;        
bool inRadio = false;   
bool inCompass = false;

// Tuner APP (C3..C7 inclusive)
bool inTone = false;
bool tonePlaying = false;
int toneIndex = 0;  // 0..48 (C3..C7)

static const int TONE_MIDI_C3 = 48;  // MIDI note for C3
static const int TONE_COUNT = 49;    // C3..C7 inclusive

static const char* kNoteBase[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

static inline int toneIndexToHz(int idx) {  // idx 0..48 maps to MIDI 48..96 (C3..C7)
  int midi = TONE_MIDI_C3 + idx;
  float hz = 440.0f * powf(2.0f, (midi - 69) / 12.0f);
  return (int)(hz + 0.5f);
}

// Builds label like "C#4" into out (must be >= 6 bytes)
static inline void toneIndexToLabel(int idx, char* out, size_t outLen) {
  int octave = 3 + (idx / 12);
  const char* base = kNoteBase[idx % 12];  // base is "C" or "C#"
  snprintf(out, outLen, "%s%d", base, octave);
}

static inline void toneStop() {
  noTone(BUZZER_PIN);
  tonePlaying = false;
  buzzing = false;  // keep beep housekeeping consistent
}

static inline void toneStart() {
  int hz = toneIndexToHz(toneIndex);
  tone(BUZZER_PIN, hz);
  tonePlaying = true;
  buzzing = false;
}

static void drawPlayIcon(int cx, int cy) {  // small triangle
  display.fillTriangle(cx - 4, cy - 3, cx - 4, cy + 3, cx + 4, cy, WHITE);
}

static void drawPauseIcon(int cx, int cy) {  // small pause bars
  display.fillRect(cx - 5, cy - 3, 3, 7, WHITE);
  display.fillRect(cx + 2, cy - 3, 3, 7, WHITE);
}

void drawToneApp() {
  if (!displayReady) return;
  display.clearDisplay();
  display.setTextColor(WHITE);

  // Center note text
  display.setTextSize(2);
  char noteBuf[6];
  toneIndexToLabel(toneIndex, noteBuf, sizeof(noteBuf));
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(noteBuf, 0, 0, &x1, &y1, &w, &h);
  int nx = (SCREEN_WIDTH - (int)w) / 2;
  int ny = 22;
  display.setCursor(nx, ny);
  display.print(noteBuf);

  // Bottom center controls: "<" play/pause ">"
  int iconCy = 54;
  int iconCx = SCREEN_WIDTH / 2;
  display.setTextSize(1);

  const int CHAR_W = 6;        // default font width
  const int ARROW_OFFSET = 14; // spacing from center

  // Left arrow
  display.setCursor(iconCx - ARROW_OFFSET - CHAR_W / 2, iconCy - 3);
  display.print("<");

  // Right arrow
  display.setCursor(iconCx + ARROW_OFFSET - CHAR_W / 2, iconCy - 3);
  display.print(">");

  // Center icon
  if (tonePlaying) drawPauseIcon(iconCx, iconCy);
  else drawPlayIcon(iconCx, iconCy);

  // Bottom right EXIT
  const char* exitLbl = "EXIT";
  int exitW = 6 * 4;
  display.setCursor(SCREEN_WIDTH - exitW, 55);
  display.print(exitLbl);

  display.display();
}

inline void redrawToneNow() {
  if (screenEnabled && displayReady) {
    drawToneApp();
    uiDirty = false;
  }
}

// ---------------- METRONOME ----------------
bool inMetronome = false;

static int metBpm = 90;                 // 1..500, resets to 90 on enter
static bool metPlaying = false;

static unsigned long metNextTickMs = 0; // when the next click should happen
static unsigned long metBeepOffMs = 0;  // when to stop the click beep (short)
static const unsigned long MET_CLICK_MS = 25;  // click length
static const int MET_BPM_MIN = 1;
static const int MET_BPM_MAX = 500;

// Fast scroll
static unsigned long metNextRepeatDec = 0;
static unsigned long metNextRepeatInc = 0;
static const unsigned long MET_REPEAT_MS = 80;   // repeat rate while held

// Sensor (BME280)
Adafruit_BME280 bme;
bool bmeReady = false;

// BNO055 Compass Sensor
#define CAL_OFFSET_DEG 5.0
Adafruit_BNO055 bno = Adafruit_BNO055(55, 0x28);

// Normalize angle to 0–360°
float wrap360(float a) {
  while (a < 0) a += 360;
  while (a >= 360) a -= 360;
  return a;
}

#define BME_ADDR 0x76
#define SEALEVEL_HPA 1013.25f

// Humidity calibration (linear)
float g_humSlope = 1.0f; 
float g_humOffset = -10.1f; 

// Temperature calibration
float g_tempOffsetC = 6.4f; 
float g_seaLevel_hPa = 1013.25f;

bool inDataView = false;
unsigned long lastDataRefreshMs = 0;
const unsigned long DATA_REFRESH_MS = 1000;  // reduce self-heating
bool g_metricUnits = true;

// --- DATA temp mode ---
bool g_bodyMode = false;              // false=Ambient, true=Body (on-wrist)
float g_bodyExtraOffsetC = 1.3f;     

// Button handling
const unsigned long DEBOUNCE_MS = 25;
const unsigned long LONG_MS = 1000;
const unsigned long REPEAT_MS = 150;

BtnState b1{BTN1}, b2{BTN2}, b3{BTN3},
         bUp{BTN_UP}, bDown{BTN_DOWN},
         bRight{BTN_RIGHT}, bLeft{BTN_LEFT};

unsigned long nextRepeat1 = 0;
unsigned long nextRepeat3 = 0;
unsigned long nextToneRepeatL = 0;
unsigned long nextToneRepeatR = 0;

// faster than normal REPEAT_MS
const unsigned long TONE_REPEAT_MS = 80;

// Small guard to ignore residual BTN2 release right after returning to MENU
unsigned long menuEnableButtonsAt = 0;  // millis timestamp (0 = ready)

// Periodic Table
bool inPeriodicTable = false;

struct Element {
  uint8_t Z;
  const char* sym;
  const char* name;
  float amu;
};

const Element kElements[] = {
  { 1,"H","Hydrogen",1.008f},{ 2,"He","Helium",4.003f},{ 3,"Li","Lithium",6.941f},
  { 4,"Be","Beryllium",9.012f},{ 5,"B","Boron",10.810f},{ 6,"C","Carbon",12.011f},
  { 7,"N","Nitrogen",14.007f},{ 8,"O","Oxygen",15.999f},{ 9,"F","Fluorine",18.998f},
  {10,"Ne","Neon",20.180f},{11,"Na","Sodium",22.990f},{12,"Mg","Magnesium",24.305f},
  {13,"Al","Aluminium",26.982f},{14,"Si","Silicon",28.085f},{15,"P","Phosphorus",30.974f},
  {16,"S","Sulfur",32.060f},{17,"Cl","Chlorine",35.450f},{18,"Ar","Argon",39.948f},
  {19,"K","Potassium",39.098f},{20,"Ca","Calcium",40.078f},
  {21,"Sc","Scandium",44.956f},{22,"Ti","Titanium",47.867f},{23,"V","Vanadium",50.942f},
  {24,"Cr","Chromium",51.996f},{25,"Mn","Manganese",54.938f},{26,"Fe","Iron",55.845f},
  {27,"Co","Cobalt",58.933f},{28,"Ni","Nickel",58.693f},{29,"Cu","Copper",63.546f},
  {30,"Zn","Zinc",65.380f},{31,"Ga","Gallium",69.723f},{32,"Ge","Germanium",72.630f},
  {33,"As","Arsenic",74.922f},{34,"Se","Selenium",78.971f},{35,"Br","Bromine",79.904f},
  {36,"Kr","Krypton",83.798f},{37,"Rb","Rubunidium",85.468f},{38,"Sr","Strontium",87.620f},
  {39,"Y","Yttrium",88.906f},{40,"Zr","Zirconium",91.224f},
  {41,"Nb","Niobium",92.906f},{42,"Mo","Molybdenum",95.950f},{43,"Tc","Technetium",98.000f},
  {44,"Ru","Ruthenium",101.07f},{45,"Rh","Rhodium",102.905f},{46,"Pd","Palladium",106.42f},
  {47,"Ag","Silver",107.868f},{48,"Cd","Cadmium",112.414f},
  {49,"In","Indium",114.818f},{50,"Sn","Tin",118.710f},{51,"Sb","Antimony",121.760f},
  {52,"Te","Tellurium",127.60f},{53,"I","Iodine",126.904f},{54,"Xe","Xenon",131.293f},
  {55,"Cs","Cesium",132.905f},{56,"Ba","Barium",137.327f},
  {57,"La","Lanthanum",138.905f},{58,"Ce","Cerium",140.116f},{59,"Pr","Praseodymium",140.908f},
  {60,"Nd","Neodymium",144.242f},{61,"Pm","Promethium",145.0f},{62,"Sm","Samarium",150.360f},
  {63,"Eu","Europium",151.964f},{64,"Gd","Gadolinium",157.25f},
  {65,"Tb","Terbium",158.925f},{66,"Dy","Dysprosium",162.500f},{67,"Ho","Holmium",164.930f},
  {68,"Er","Erbium",167.259f},{69,"Tm","Thulium",168.934f},{70,"Yb","Ytterbium",173.045f},
  {71,"Lu","Lutetium",174.967f}
};

const int kElementCount = sizeof(kElements) / sizeof(kElements[0]);
int ptIndex = 0;

// Moon phase
double g_moonOffset = 0.0;

double julianDate(int y, int m, int d, int hh, int mm, int ss) {
  int A = (14 - m) / 12;
  int Y = y + 4800 - A;
  int M = m + 12 * A - 3;
  long JDN = d + (153 * M + 2) / 5 + 365L * Y + Y / 4 - Y / 100 + Y / 400 - 32045;
  double dayfrac = (hh - 12) / 24.0 + mm / 1440.0 + ss / 86400.0;
  return (double)JDN + dayfrac;
}

double rawMoonPhase01(int y, int m, int d, int hh, int mm, int ss) {
  const double SYNODIC = 29.530588853;
  double jd = julianDate(y, m, d, hh, mm, ss);
  double p = fmod((jd - 2451550.1) / SYNODIC, 1.0);
  if (p < 0) p += 1.0;
  return p;
}

double adjMoonPhase01(int y, int m, int d, int hh, int mm, int ss) {
  double p = rawMoonPhase01(y, m, d, hh, mm, ss) + g_moonOffset;
  p = fmod(p, 1.0);
  if (p < 0) p += 1.0;
  return p;
}

void drawMoonBrackets(int cx, int cy, int r) {
  int pad = 2;
  int x0 = cx - r - pad;
  int y0 = cy - r - pad;
  int x1 = cx + r + pad;
  int y1 = cy + r + pad;
  int len = 4;

  display.drawLine(x0, y0, x0 + len, y0, WHITE);
  display.drawLine(x0, y0, x0, y0 + len, WHITE);
  display.drawLine(x0, y1, x0 + len, y1, WHITE);
  display.drawLine(x0, y1, x0, y1 - len, WHITE);

  display.drawLine(x1, y0, x1 - len, y0, WHITE);
  display.drawLine(x1, y0, x1, y0 + len, WHITE);
  display.drawLine(x1, y1, x1 - len, y1, WHITE);
  display.drawLine(x1, y1, x1, y1 - len, WHITE);
}

void drawMoonIcon(int cx, int cy, int r) {
  if (!displayReady) return;

  double p = adjMoonPhase01(year, month, day, hour, minute, second);
  double k = cos(2.0 * M_PI * p);  // limb parameter

  display.drawCircle(cx, cy, r, WHITE);

  for (int yy = -r; yy <= r; ++yy) {
    for (int xx = -r; xx <= r; ++xx) {
      double x = (double)xx / r;
      double y = (double)yy / r;
      if (x * x + y * y > 1.0) continue;

      double inside = 1.0 - y * y;
      double chord = (inside > 0.0) ? sqrt(inside) : 0.0;
      bool lit;

      if (p <= 0.5) {
        lit = (x >= k * chord);      // waxing: bright on right
      } else {
        lit = (x <= -k * chord);     // waning: bright on left
      }

      if (lit) display.drawPixel(cx + xx, cy + yy, WHITE);
    }
  }
}

// Helpers
String two(int v) {
  return (v < 10) ? ("0" + String(v)) : String(v);
}

static inline String timeHHMMSS() {
  int h12 = hour % 12;
  if (h12 == 0) h12 = 12;
  return String(two(h12)) + ":" + two(minute) + ":" + two(second);
}

// --- Weekday helpers (used by watchface & calendar) ---
static inline int wdayFromYMD(int y, int m, int d) {
  // Sakamoto's algorithm: 0=Sunday..6=Saturday
  static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static const char* kWD3[7] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
static const char* kMon3[12] = {
  "JAN","FEB","MAR","APR","MAY","JUN",
  "JUL","AUG","SEP","OCT","NOV","DEC"
};

static inline void addDays(int n) {
  day += n;
  normalizeDateTime();  // adjusts month/year
}

// Drawing: CHEM
void drawPeriodicTableCard() {
  if (!displayReady) return;

  display.clearDisplay();
  const Element &e = kElements[ptIndex];

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Z:");
  display.print(e.Z);

  char mbuf[16];
  snprintf(mbuf, sizeof(mbuf), "%.3f", e.amu);
  int massW = 6 * (int)strlen(mbuf);
  display.setCursor(SCREEN_WIDTH - massW, 0);
  display.print(mbuf);

  const char* nm = e.name;
  int nameW = 6 * (int)strlen(nm);
  int nx = (SCREEN_WIDTH - nameW) / 2;
  if (nx < 0) nx = 0;
  display.setCursor(nx, 12);
  display.print(nm);

  display.setTextSize(2);
  int symW = 12 * (int)strlen(e.sym);
  int sx = (SCREEN_WIDTH - symW) / 2;
  if (sx < 0) sx = 0;
  display.setCursor(sx, 28);
  display.print(e.sym);

  display.setTextSize(1);
  display.setCursor(0, 54);
  display.print("prev");

  const char* exitLbl = "EXIT";
  int exitW = 6 * 4;
  display.setCursor((SCREEN_WIDTH - exitW) / 2, 54);
  display.print(exitLbl);

  int nextW = 6 * 4;
  display.setCursor(SCREEN_WIDTH - nextW, 54);
  display.print("next");

  display.display();
}

// Clock helpers
bool isLeap(int y) {
  return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

int daysInMonth(int y, int m) {
  static const int dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (m == 2) return dim[m - 1] + (isLeap(y) ? 1 : 0);
  return dim[m - 1];
}

void normalizeDateTime() {
  if (second >= 60) {
    minute += second / 60;
    second %= 60;
  }

  if (minute >= 60) {
    hour += minute / 60;
    minute %= 60;
  }

  if (hour >= 24) {
    day += hour / 24;
    hour %= 24;
  }

  while (day > daysInMonth(year, month)) {
    day -= daysInMonth(year, month);
    month++;
    if (month > 12) {
      month = 1;
      year++;
    }
  }

  while (day <= 0) {
    month--;
    if (month <= 0) {
      month = 12;
      year--;
    }
    day += daysInMonth(year, month);
  }

  if (month < 1) month = 1;
  if (month > 12) month = 12;
}

void tickOneSecond() {
  second++;
  if (second >= 60) {
    second = 0;
    minute++;
  }
  if (minute >= 60) {
    minute = 0;
    hour++;
  }
  if (hour >= 24) {
    hour = 0;
    day++;
  }
  normalizeDateTime();
}

// Display init
bool probeI2C(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void ssd1306Command(uint8_t cmd) {
  if (!displayReady) return;
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x00);
  Wire.write(cmd);
  Wire.endTransmission();
}

void oledOff() {
  if (displayReady) ssd1306Command(0xAE);
}

void oledOn() {
  if (displayReady) ssd1306Command(0xAF);
}

void ensureDisplayInit() {
  if (displayReady) return;
  if (!probeI2C(OLED_ADDR)) return;

  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    displayReady = true;
    display.setTextColor(WHITE);
    display.clearDisplay();
    display.fillScreen(SSD1306_BLACK);
    display.drawBitmap(0, 0, epd_bitmap_chipboy, 128, 64, SSD1306_WHITE);
    display.display();

    // Startup chirp 
    tone(BUZZER_PIN, 200);  delay(50); noTone(BUZZER_PIN); delay(40);
    tone(BUZZER_PIN, 400);  delay(50); noTone(BUZZER_PIN); delay(40);
    tone(BUZZER_PIN, 800);  delay(50); noTone(BUZZER_PIN); delay(40);
    tone(BUZZER_PIN, 1600); delay(60); noTone(BUZZER_PIN);

    if (!screenEnabled) {
      oledOff();
    }
  }
}

// MCP23008 (buttons on GP0..GP6)
#define MCP_ADDR 0x20
#define MCP_IODIR 0x00
#define MCP_GPPU  0x06
#define MCP_GPIO  0x09

uint8_t g_mcp_gpio = 0xFF;
bool g_mcp_ok = false;

bool mcpWriteReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MCP_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool mcpReadReg(uint8_t reg, uint8_t &val) {
  Wire.beginTransmission(MCP_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MCP_ADDR, (uint8_t)1) != 1) return false;
  val = Wire.read();
  return true;
}

// Button helpers + press beep routing (with "exit" low beep)
int readBtnLevelFromMCP(const BtnState* which) {
  if (!g_mcp_ok) return HIGH;  // default "released"

  uint8_t bit = 0xFF;
  if (which == &b1) bit = 0;            // GP0
  else if (which == &b3) bit = 1;       // GP1
  else if (which == &bRight) bit = 5;   // GP2
  else if (which == &bDown) bit = 4;    // GP3
  else if (which == &bUp) bit = 3;      // GP4
  else if (which == &bLeft) bit = 2;    // GP5
  else if (which == &b2) bit = 6;       // GP6
  else return HIGH;

  return (g_mcp_gpio & (1u << bit)) ? HIGH : LOW;  // HIGH=released, LOW=pressed
}

// --- exit detector (uses extern flags and BtnState instances) ---
static inline bool isExitPress(const BtnState* bptr) {
  extern bool inLocation;
  // MENU -> WATCHFACE (BTN3 short)
  if (inMenu && bptr == &b3) return true;

  // CHEM -> MENU (BTN2 short)
  if (inPeriodicTable && bptr == &b2) return true;

  // DATA -> MENU (BTN2 short)
  if (inDataView && bptr == &b2) return true;

  //Location
  if (inLocation && bptr == &b3) return true;

  // ALARM SET -> MENU (BTN2 short)
  if (inAlarmSet && bptr == &b2) return true;

  // CALENDAR -> MENU (BTN2 short)
  if (inCalendar && bptr == &b2) return true;

  // (NO SET → MENU EXIT HERE ANYMORE)

  // CALCULATOR -> MENU (BTN2 or BTN3 short)
  if (inCalculator && (bptr == &b2 || bptr == &b3)) return true;

  // TONE -> MENU (BTN3 short)
  if (inTone && bptr == &b3) return true;

  // METRONOME -> MENU (BTN3 short)
  if (inMetronome && bptr == &b3) return true;

  // SET -> MENU (BTN2 short)

  // Watchface (no other views): BTN2 toggles OLED on/off
  if (
    !setMode && 
    !inMenu && 
    !inPeriodicTable && 
    !inDataView && 
    !inAlarmSet && 
    !inTimerSet && 
    !inCalendar && 
    !inCalculator && 
    bptr == &b2) 
  return true;

  // Screen OFF: BTN2 wakes screen
  if (!screenEnabled && bptr == &b2) return true;

  return false;
}

void btnEdgeBeepIfPress(const BtnState* bptr, bool becameLow) {
  if (!becameLow) return;

  // In METRONOME: suppress all beeps except BTN3 exit.
  if (inMetronome) {
    if (bptr == &b3) buzzerStartLow();
    return;
  }

  // In OBSCURUS game: suppress left, right, up nav beeps (used for movement).
  // BTN1 (attack), BTN2 (jump), BTN3 (exit) still beep normally.
  if (obscurusIsActive()) {
    if (bptr == &bLeft || bptr == &bRight || bptr == &bUp) return;
  }

  // In TONE app: suppress all nav beeps, allow exit only.
  if (inTone && !isExitPress(bptr)) return;

  if (isExitPress(bptr)) buzzerStartLow();
  else buzzerStart();
}

void btnUpdate(BtnState &b) {
  bool r = (&b == &b1 || &b == &b2 || &b == &b3 || &b == &bUp || &b == &bDown || &b == &bRight || &b == &bLeft)
             ? readBtnLevelFromMCP(&b)
             : HIGH;

  if (r != b.lastRead) {
    bool becameLow = (r == LOW && b.lastRead == HIGH);
    b.lastRead = r;
    b.lastChange = millis();
    btnEdgeBeepIfPress(&b, becameLow);
  }

  if (millis() - b.lastChange > DEBOUNCE_MS && r != b.stable) {
    b.stable = r;
    if (b.stable == LOW) {
      b.pressStart = millis();
      b.longFired = false;
    } else {
      b.pressStart = 0;
      b.longFired = false;
    }
  }

  if (b.stable == LOW && !b.longFired && (millis() - b.pressStart >= LONG_MS)) {
    b.longFired = true;
  }
}

bool btnWasShortPressed(const BtnState &b_in) {
  BtnState &b = const_cast<BtnState&>(b_in);
  static bool prev1 = HIGH, prev2 = HIGH, prev3 = HIGH, prevUp = HIGH, prevDown = HIGH, prevRight = HIGH, prevLeft = HIGH;
  bool ret = false;

  auto handle = [&](bool &prev) {
    bool transitionRelease = (prev == LOW && b.stable == HIGH);
    if (b.suppressOnRelease && transitionRelease) {
      b.suppressOnRelease = false;
      prev = b.stable;
      return false;
    }
    bool shortPress = (transitionRelease && !b.longFired);
    prev = b.stable;
    return shortPress;
  };

  if (&b == &b1) ret = handle(prev1);
  else if (&b == &b2) ret = handle(prev2);
  else if (&b == &b3) ret = handle(prev3);
  else if (&b == &bUp) ret = handle(prevUp);
  else if (&b == &bDown) ret = handle(prevDown);
  else if (&b == &bRight) ret = handle(prevRight);
  else if (&b == &bLeft) ret = handle(prevLeft);

  return ret;
}

bool btnWasLongPressed(BtnState &b) {
  static bool prevLong1 = false, prevLong2 = false, prevLong3 = false, prevLongUp = false, prevLongDown = false, prevLongRight = false, prevLongLeft = false;
  bool ret = false;

  if (&b == &b1) {
    if (b.longFired && !prevLong1) ret = true;
    prevLong1 = b.longFired;
  } else if (&b == &b2) {
    if (b.longFired && !prevLong2) ret = true;
    prevLong2 = b.longFired;
  } else if (&b == &b3) {
    if (b.longFired && !prevLong3) ret = true;
    prevLong3 = b.longFired;
  } else if (&b == &bUp) {
    if (b.longFired && !prevLongUp) ret = true;
    prevLongUp = b.longFired;
  } else if (&b == &bDown) {
    if (b.longFired && !prevLongDown) ret = true;
    prevLongDown = b.longFired;
  } else if (&b == &bRight) {
    if (b.longFired && !prevLongRight) ret = true;
    prevLongRight = b.longFired;
  } else if (&b == &bLeft) {
    if (b.longFired && !prevLongLeft) ret = true;
    prevLongLeft = b.longFired;
  }

  if (ret) b.suppressOnRelease = true;
  return ret;
}

// 2-D SET navigation helpers (enum at top)
static inline int rowMaxCol(int row) {
  switch (row) {
    case 0: return 2;  // H, M, S
    case 1: return 0;  // AM/PM
    case 2: return 4;  // Month, Day, Year, Weekday, Moon
    default: return 0;
  }
}

static inline void fieldToRowCol(Field f, int &row, int &col) {
  switch (f) {
    case F_HOUR:  row = 0; col = 0; break;
    case F_MIN:   row = 0; col = 1; break;
    case F_SEC:   row = 0; col = 2; break;
    case F_AMPM:  row = 1; col = 0; break;
    case F_MONTH: row = 2; col = 0; break;
    case F_DAY:   row = 2; col = 1; break;
    case F_YEAR:  row = 2; col = 2; break;
    case F_WDAY:  row = 2; col = 3; break;
    case F_MOON:  row = 2; col = 4; break;
  }
}

static inline Field rowColToField(int row, int col) {
  if (row == 0) {
    if (col == 0) return F_HOUR;
    if (col == 1) return F_MIN;
    return F_SEC;
  } else if (row == 1) {
    return F_AMPM;
  } else {
    if (col == 0) return F_MONTH;
    if (col == 1) return F_DAY;
    if (col == 2) return F_YEAR;
    if (col == 3) return F_WDAY;
    return F_MOON;
  }
}

static inline void moveSetCursor(int dx, int dy) {
  int row, col;
  fieldToRowCol(currentField, row, col);

  int newRow = row + dy;
  if (newRow < 0) newRow = 0;
  if (newRow > 2) newRow = 2;

  int newCol = col;
  if (dy != 0) {
    // moving between rows: clamp column to that row's max
    int maxc = rowMaxCol(newRow);
    if (newCol > maxc) newCol = maxc;
  }

  int maxc2 = rowMaxCol(newRow);
  newCol += dx;
  if (newCol < 0) newCol = 0;
  if (newCol > maxc2) newCol = maxc2;

  currentField = rowColToField(newRow, newCol);
  uiDirty = true;
}

void adjustField(int delta) {
  switch (currentField) {
    case F_HOUR:
      hour += delta;
      while (hour < 0) hour += 24;
      hour %= 24;
      break;

    case F_MIN:
      minute += delta;
      while (minute < 0) minute += 60;
      minute %= 60;
      break;

    case F_SEC:
      second += delta;
      while (second < 0) second += 60;
      second %= 60;
      break;

    case F_AMPM:
      hour = (hour + 12) % 24;
      break;

    case F_MONTH:
      month += delta;
      if (month < 1) month = 12;
      if (month > 12) month = 1;
      if (day > daysInMonth(year, month)) day = daysInMonth(year, month);
      break;

    case F_DAY: {
      int maxd = daysInMonth(year, month);
      day += delta;
      if (day < 1) day = maxd;
      if (day > maxd) day = 1;
      break;
    }

    case F_YEAR:
      year += delta;
      if (year < 1970) year = 1970;
      if (day > daysInMonth(year, month)) day = daysInMonth(year, month);
      break;

    case F_WDAY:
      // cycle weekday by moving the date ±1 day
      addDays((delta > 0) ? +1 : -1);
      break;

    case F_MOON: {
      double step = (delta > 0 ? 1.0 : -1.0) / 8.0;
      g_moonOffset += step;
      g_moonOffset -= floor(g_moonOffset);
      break;
    }
  }

  uiDirty = true;
}

void printBlinkingColon(bool on) {
  if (!displayReady) return;

  // Tighten: step back 1px before the colon, then after drawing it
  // move the cursor 1px left to close the gap on the right side too.
  int cx = display.getCursorX();
  int cy = display.getCursorY();
  display.setCursor(cx - 1, cy);

  if (on) display.setTextColor(WHITE);
  else    display.setTextColor(BLACK, BLACK);

  display.write(':');
  display.setTextColor(WHITE);

  // Pull cursor back 1px to tighten the right-side gap
  display.setCursor(display.getCursorX() - 1, cy);
}

// Alarm flag forward (used by drawClock for footer)
bool alarmEnabled = false;  // shows "ALARM" on watchface when true

// Returns battery percentage (0-100) using a piecewise SoC curve
// matched to BatteryApp.cpp for consistent readings across the UI.
// 100k/100k divider on a 3.7V LiPo: ADC sees Vbatt/2.
// ESP32-S3 ADC is 12-bit (0-4095) at 3.3V reference.
static int readBatteryPercent() {
  // Average 16 samples to reduce ADC noise (matches BatteryApp)
  int32_t raw = 0;
  for (int i = 0; i < 16; i++) raw += analogRead(BATT_PIN);
  raw /= 16;

  // Vdivider = raw * 3.3 / 4095
  // Vbatt    = Vdivider * 2  (equal resistor divider)
  float vbatt = (raw / 4095.0f) * 3.3f * 2.0f;

  // Piecewise SoC lookup — matches BatteryApp.cpp SOC_TABLE exactly
  struct SoCPoint { float v; float soc; };
  static const SoCPoint tbl[] = {
    { 4.05f, 100.0f },
    { 3.85f,  90.0f },
    { 3.60f,  70.0f },
    { 3.40f,  40.0f },
    { 3.30f,  20.0f },
    { 3.00f,   5.0f },
    { 2.75f,   0.0f },
  };
  const uint8_t tblLen = sizeof(tbl) / sizeof(tbl[0]);

  if (vbatt >= tbl[0].v)          return 100;
  if (vbatt <= tbl[tblLen - 1].v) return 0;

  for (uint8_t i = 0; i < tblLen - 1; i++) {
    if (vbatt <= tbl[i].v && vbatt >= tbl[i + 1].v) {
      float t = (vbatt - tbl[i + 1].v) / (tbl[i].v - tbl[i + 1].v);
      return (int)(tbl[i + 1].soc + t * (tbl[i].soc - tbl[i + 1].soc));
    }
  }
  return 0;
}

// Drawing: clock (footer shows MENU; right shows ALARM if enabled)
void drawClock() {
  if (!displayReady) return;

  display.clearDisplay();
  display.setTextColor(WHITE);

  bool colonOn = (second % 2 == 0);
  int h12 = hour % 12;
  if (h12 == 0) h12 = 12;

  const int TIME_TEXT_SIZE = 2;
  const int AMPM_X_PAD_PX = 2;
  const int AMPM_ROW_OFFSET_SUB = 8;
  const int AMPM_ROW_OFFSET_LINE = 20;
  const int DATE_Y_NOTSET = 28;
  const int DATE_Y_SET = 36;

  // --- Time (hh:mm:ss) ---
  display.setTextSize(TIME_TEXT_SIZE);
  display.setCursor(0, 0);

  if (setMode && currentField == F_HOUR) display.print("[" + String(h12) + "]");
  else display.print(h12);

  printBlinkingColon(colonOn);

  if (setMode && currentField == F_MIN) display.print("[" + two(minute) + "]");
  else display.print(two(minute));

  printBlinkingColon(colonOn);

  if (setMode && currentField == F_SEC) display.print("[" + two(second) + "]");
  else display.print(two(second));

  // --- AM/PM ---
  int ampmX = display.getCursorX() + AMPM_X_PAD_PX;
  String ampm = (hour < 12) ? "AM" : "PM";
  display.setTextSize(1);

  if (setMode) {
    display.setCursor(0, AMPM_ROW_OFFSET_LINE);
    if (currentField == F_AMPM) display.print("[" + ampm + "]");
    else display.print(ampm);
  } else {
    display.setCursor(ampmX, AMPM_ROW_OFFSET_SUB);
    if (currentField == F_AMPM) display.print("[" + ampm + "]");
    else display.print(ampm);
  }

  // --- Date ---
  String dateStr;
  if (setMode && currentField == F_MONTH) dateStr += "[" + two(month) + "]";
  else dateStr += two(month);

  dateStr += "/";

  if (setMode && currentField == F_DAY) dateStr += "[" + two(day) + "]";
  else dateStr += two(day);

  dateStr += "/";

  if (setMode && currentField == F_YEAR) dateStr += "[" + String(year) + "]";
  else dateStr += String(year);

  int dateY = setMode ? DATE_Y_SET : DATE_Y_NOTSET;
  display.setCursor(0, dateY);
  display.print(dateStr);

  // --- Weekday (right of date) ---
  int w = wdayFromYMD(year, month, day);  // 0=SUN..6=SAT
  String w3 = kWD3[w];
  display.print(" ");

  if (setMode && currentField == F_WDAY) {
    display.print("[");
    display.print(w3);
    display.print("]");
  } else {
    display.print(w3);
  }

  // --- Battery percentage ---
  if (!setMode) {
    int battPct = readBatteryPercent();
    char battBuf[8];
    snprintf(battBuf, sizeof(battBuf), "%d%%", battPct);
    int battW = strlen(battBuf) * 6;
    display.setCursor(SCREEN_WIDTH - battW, 0);
    display.print(battBuf);
  }

  // --- Moon icon ---
  int moonR = 7;
  int moonCx = SCREEN_WIDTH - 1 - moonR - 5;
  int moonCy = dateY + 4;
  if (moonCx + moonR > SCREEN_WIDTH - 1) moonCx = SCREEN_WIDTH - 1 - moonR;

  drawMoonIcon(moonCx, moonCy, moonR);
  if (setMode && currentField == F_MOON) drawMoonBrackets(moonCx, moonCy, moonR);

  // --- Footer ---
  display.setTextSize(1);

  if (setMode) {
    display.setCursor(0, 54);
    display.print("-");

    const char* midLbl = "SET";
    int midW = 6 * 3;
    display.setCursor((SCREEN_WIDTH - midW) / 2, 54);
    display.print(midLbl);

    const char* plusLbl = "+";
    int plusW = 6 * 1;
    display.setCursor(SCREEN_WIDTH - plusW, 54);
    display.print(plusLbl);
  } else {
    display.setCursor(0, 54);
    display.print("MENU");

    if (alarmEnabled) {
      const char* a = "ALARM";
      int aw = 6 * 5;
      display.setCursor(SCREEN_WIDTH - aw, 54);
      display.print(a);
    }
  }

  display.display();
}

// Drawing — DATA (text mode) (GRAPH removed)
void drawDataView() {
  if (!displayReady) return;

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);

  auto drawFooter = [&]() {
    // Left: mode label
    display.setCursor(0, 55);
    display.print(g_bodyMode ? "BODY" : "AMB");

    // Center: EXIT
    const char* exitLbl = "EXIT";
    int exitW = 6 * 4;
    display.setCursor((SCREEN_WIDTH - exitW) / 2, 55);
    display.print(exitLbl);

    // Right: UNIT
    const char* unitLbl = "UNIT";
    int unitW = 6 * 4;
    display.setCursor(SCREEN_WIDTH - unitW, 55);
    display.print(unitLbl);
  };

  if (!bmeReady) {
    display.setCursor(0, 0);
    display.print("Sensor not found");
    drawFooter();
    display.display();
    return;
  }

  float T_C_raw = bme.readTemperature();
  float P_Pa = bme.readPressure();
  float H_pct = bme.readHumidity();

  H_pct = g_humSlope * H_pct + g_humOffset;
  if (H_pct < 0) H_pct = 0;
  if (H_pct > 100) H_pct = 100;

  float Alt_m = bme.readAltitude(g_seaLevel_hPa);
  float T_adjC = T_C_raw - g_tempOffsetC - (g_bodyMode ? g_bodyExtraOffsetC : 0.0f);

  float T_disp, P_disp, Alt_disp;
  const char* T_unit;
  const char* P_unit;
  const char* Alt_unit;

  if (g_metricUnits) {
    T_disp = T_adjC;
    T_unit = " C";
    P_disp = P_Pa;
    P_unit = " Pa";
    Alt_disp = Alt_m;
    Alt_unit = " m";
  } else {
    T_disp = T_adjC * 9.0f / 5.0f + 32.0f;
    T_unit = " F";
    P_disp = P_Pa / 3386.389f;
    P_unit = " inHg";
    Alt_disp = Alt_m * 3.28084f;
    Alt_unit = " ft";
  }

  int y = 0;
  display.setCursor(0, y);
  display.print("Temperature: ");
  display.print(T_disp, 2);
  display.print(T_unit);

  y += 12;
  display.setCursor(0, y);
  display.print("Pressure: ");
  if (g_metricUnits) display.print(P_disp, 0);
  else display.print(P_disp, 3);
  display.print(P_unit);

  y += 12;
  display.setCursor(0, y);
  display.print("Humidity: ");
  display.print(H_pct, 1);
  display.print(" %");

  y += 12;
  display.setCursor(0, y);
  display.print("Altitude: ");
  display.print(Alt_disp, 1);
  display.print(Alt_unit);

  drawFooter();
  display.display();
}

// Sea-level calibration
void calibrateSeaLevelFromElevation(float knownElevation_m) {
  float P_Pa = bme.readPressure();     // Pa
  float P_hPa = P_Pa / 100.0f;         // hPa
  float term = 1.0f - (knownElevation_m / 44330.0f);
  g_seaLevel_hPa = P_hPa / powf(term, 5.255f);
}

// MENU 
bool inMenu = false;
uint8_t menuIndex = 0; // selected item

// menu sizing 
static const uint8_t MENU_COUNT = 18;     // CLOCK..TUNER
static const uint8_t MENU_VISIBLE = 7;   // rows you already draw
uint8_t menuFirst = 0;  // NEW: only affects how drawMenu() renders

//CLOCK submenu
bool inClockMenu = false;
uint8_t clockMenuIndex = 0;  // 0=SET, 1=ALARM, 2=TIMER

void drawClockSubMenu() {
  if (!displayReady) return;

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);

  // ------------- MINI CLOCK (same as main menu) -------------
  String t = timeHHMMSS();
  int tpx = t.length() * 6;
  int tx = SCREEN_WIDTH - tpx;  // Print HH:MM:SS (right aligned)
  display.setCursor(tx, 0);
  display.print(t);

  // AM/PM under mini clock (right edge)
  String ap = (hour < 12) ? "AM" : "PM";
  int apW = 12;  // 2 chars * 6px
  display.setCursor(SCREEN_WIDTH - apW, 12);
  display.print(ap);

  // ------------- SUBMENU ITEMS (left side) -------------
  const char* items[] = { "SET", "ALARM", "TIMER" };
  int y = 0;
  for (int i = 0; i < 3; i++) {
    display.setCursor(0, y);
    display.print((clockMenuIndex == i) ? ">" : " ");
    display.print(items[i]);
    y += 9;
  }

  // ------------- EXIT FOOTER -------------
  const char* exitLbl = "EXIT";
  int exitW = 6 * 4;
  display.setCursor(SCREEN_WIDTH - exitW, 55);
  display.print(exitLbl);

  display.display();
}

bool cursorVisible = true;
unsigned long nextCursorToggle = 0;
const unsigned long CURSOR_BLINK_MS = 500;

void drawMenu() {
  if (!displayReady) return;

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);

  // right-aligned tiny clock
  String t = timeHHMMSS();
  int tpx = t.length() * 6;
  int tx = SCREEN_WIDTH - tpx;

  //Apps list in main menu
  static const char* items[18] = {
  "BATTERY","CALCULATOR","CALENDAR","CAMERA","CHEM","CLOCK",
  "COMPASS","DATA","DISTANCE","GAME","LOCATION",
  "METRONOME","MICROPHONE","NAVIGATION","RADIO","RECORDINGS","SKY","TUNER"
  };


  // Layout: 7 rows visible + footer
  const int ROW_STEP = 9;   // tighter so 7 items fit
  const int FOOTER_Y = 55;  // push EXIT down slightly
  const int START_Y = 0;

  // Decide which slice to render
  int first = menuFirst;
  int last  = min((int)MENU_COUNT - 1, first + (int)MENU_VISIBLE - 1);


  // ---- draw visible items ----
  int row = 0;
  for (int i = first; i <= last; i++) {
    int y = START_Y + row * ROW_STEP;

    // On the first row, we also draw the right-aligned tiny clock (same as before)
    if (row == 0) {
      // Left label (truncated to fit before clock)
      String label = ((menuIndex == i) ? ">" : " ");
      label += items[i];

      int labelPx = label.length() * 6;
      int maxPx = max(0, tx - 2);  // tx is right-aligned clock x
      if (labelPx > maxPx) {
        int maxChars = maxPx / 6;
        label = label.substring(0, maxChars);
      }

      display.setCursor(0, y);
      display.print(label);

      // time on right
      display.setCursor(tx, y);
      display.print(t);
    } else {
      display.setCursor(0, y);
      display.print((menuIndex == i) ? ">" : " ");
      display.print(items[i]);
    }

    row++;
  }

  // AM/PM under the mini clock
  String ap = (hour < 12) ? "AM" : "PM";
  int apW = 12;
  display.setCursor(SCREEN_WIDTH - apW, 12);
  display.print(ap);

  // footer EXIT
  const char* exitLbl = "EXIT";
  int exitW = 6 * 4;
  display.setCursor(SCREEN_WIDTH - exitW, FOOTER_Y);
  display.print(exitLbl);

  display.display();
}

// Instant redraw helpers
inline void redrawClockNow() {
  if (screenEnabled && displayReady) {
    drawClock();
    uiDirty = false;
    lastSecondDrawn = second;
  }
}

inline void redrawMenuNow() {
  if (screenEnabled && displayReady) {
    drawMenu();
    uiDirty = false;
    lastSecondDrawn = second;
  }
}

// ---------------- METRONOME: UI + AUDIO ----------------
static inline unsigned long metIntervalMsFromBpm(int bpm) {
  if (bpm < MET_BPM_MIN) bpm = MET_BPM_MIN;
  if (bpm > MET_BPM_MAX) bpm = MET_BPM_MAX;
  // interval = 60,000 ms / BPM
  return (unsigned long)(60000UL / (unsigned long)bpm);
}

static void drawMetronome() {
  if (!displayReady) return;

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);

  // Big BPM number
  display.setTextSize(2);
  String bpmStr = String(metBpm);
  int w = bpmStr.length() * 12;   // size 2 => 12 px per char
  int x = (SCREEN_WIDTH - w) / 2;
  int y = 20;
  display.setCursor(x, y);
  display.print(bpmStr);

  display.setTextSize(1);
  display.setCursor((SCREEN_WIDTH - 18) / 2, y + 20);
  display.print("BPM");

  // Bottom center controls: "<" play/pause ">"
  int iconCy = 54;
  int iconCx = SCREEN_WIDTH / 2;
  display.setTextSize(1);

  const int CHAR_W = 6;        // default font width
  const int ARROW_OFFSET = 14; // spacing from center

  // Left arrow
  display.setCursor(iconCx - ARROW_OFFSET - CHAR_W / 2, iconCy - 3);
  display.print("<");

  // Right arrow
  display.setCursor(iconCx + ARROW_OFFSET - CHAR_W / 2, iconCy - 3);
  display.print(">");

  // Center icon
  if (metPlaying) drawPauseIcon(iconCx, iconCy);
  else drawPlayIcon(iconCx, iconCy);

  // Bottom right EXIT
  display.setCursor(SCREEN_WIDTH - (6 * 4), 55);
  display.print("EXIT");

  display.display();
}

inline void redrawMetronomeNow() {
  if (screenEnabled && displayReady) {
    drawMetronome();
    uiDirty = false;
    lastSecondDrawn = second;
  }
}

static void metronomeStopClickNow() {
  // stop any tone immediately
  toneStop();
  metBeepOffMs = 0;
}

static void metronomeClickNow() {
  // Make one short click (using normal beep tone so it doesn't affect other beeps)
  buzzerStart();
  metBeepOffMs = millis() + MET_CLICK_MS;
}

static void metronomeTogglePlayPause() {
  metPlaying = !metPlaying;
  if (metPlaying) {
    unsigned long now = millis();
    metNextTickMs = now; // click immediately on start
  } else {
    metronomeStopClickNow();
  }
}

inline void redrawChemNow() {
  if (screenEnabled && displayReady) {
    drawPeriodicTableCard();
    uiDirty = false;
  }
}

inline void redrawDataNow() {
  if (screenEnabled && displayReady) {
    drawDataView();
    uiDirty = false;
  }
}

// Forward declarations for draw functions used by inline redraw helpers
void drawCalendar();
void drawCalculator();
void drawCompass();

inline void redrawCalendarNow() {  
  if (screenEnabled && displayReady) {
    drawCalendar();
    uiDirty = false;
  }
}

inline void redrawCalculatorNow() {  
  if (screenEnabled && displayReady) {
    drawCalculator();
    uiDirty = false;
  }
}

inline void redrawCompassNow() {
  if (screenEnabled && displayReady) {
    drawCompass();
    uiDirty = false;
  }
}

// ALARM app state & helpers
enum AppView {
  VIEW_WATCH,
  VIEW_MENU,
  VIEW_CHEM,
  VIEW_DATA,
  VIEW_SET,
  VIEW_ALARMSET,
  VIEW_CALENDAR,
  VIEW_CALC,
  VIEW_TONE,
  VIEW_RADIO,
  VIEW_TIMER
};

bool inAlarmSet = false;
int alarmHour = 7;  // 24h storage
int alarmMin = 0;
int alarmSec = 0;
AlarmField alarmField = A_HOUR;

bool alarmRinging = false;

uint8_t alarmBurstBeepIdx = 0;  
unsigned long alarmNextEventAt = 0;
const unsigned ALARM_BEEP_MS    = 70;   // beep on-time
const unsigned ALARM_INTER_BEEP = 60;   // gap between beeps in a burst
const unsigned ALARM_BURST_GAP  = 500;   // pause between bursts

bool alarmFlashOn = true;
unsigned long alarmFlashNext = 0;
const unsigned ALARM_FLASH_MS = 300;

bool alarmWokeScreen = false;  // true if we turned the screen on to show the alarm
AppView returnViewOnAlarm = VIEW_WATCH;

static inline bool timeMatchesAlarmNow() {
  return (hour == alarmHour) && (minute == alarmMin) && (second == alarmSec);
}

static inline void alarmMoveCursor(int dx, int dy) {
  int row, col;

  switch (alarmField) {
    case A_HOUR:   row = 0; col = 0; break;
    case A_MIN:    row = 0; col = 1; break;
    case A_SEC:    row = 0; col = 2; break;
    case A_AMPM:   row = 1; col = 0; break;
    case A_ENABLE: row = 2; col = 0; break;
  }

  row += dy;
  if (row < 0) row = 0;
  if (row > 2) row = 2;

  int maxc = (row == 0 ? 2 : 0);
  col += dx;
  if (col < 0) col = 0;
  if (col > maxc) col = maxc;

  if (row == 0) alarmField = (col == 0 ? A_HOUR : (col == 1 ? A_MIN : A_SEC));
  else if (row == 1) alarmField = A_AMPM;
  else alarmField = A_ENABLE;
}

static inline void alarmAdjust(int delta) {
  switch (alarmField) {
    case A_HOUR:
      alarmHour += delta;
      while (alarmHour < 0) alarmHour += 24;
      alarmHour %= 24;
      break;

    case A_MIN:
      alarmMin += delta;
      while (alarmMin < 0) alarmMin += 60;
      alarmMin %= 60;
      break;

    case A_SEC:
      alarmSec += delta;
      while (alarmSec < 0) alarmSec += 60;
      alarmSec %= 60;
      break;

    case A_AMPM:
      alarmHour = (alarmHour + 12) % 24;
      break;

    case A_ENABLE:
      alarmEnabled = !alarmEnabled;
      break;
  }
}

void drawAlarmSetView() {
  if (!displayReady) return;

  display.clearDisplay();
  display.setTextColor(WHITE);

  bool colonOn = (second % 2 == 0);
  int h12_alarm = alarmHour % 12;
  if (h12_alarm == 0) h12_alarm = 12;

  const int TIME_TEXT_SIZE = 2;

  display.setTextSize(TIME_TEXT_SIZE);
  display.setCursor(0, 0);

  if (alarmField == A_HOUR) display.print("[" + String(h12_alarm) + "]");
  else display.print(h12_alarm);

  
  { int cx = display.getCursorX(), cy = display.getCursorY();
    display.setCursor(cx - 1, cy);
    if (colonOn) display.setTextColor(WHITE);
    else         display.setTextColor(BLACK, BLACK);
    display.write(':');
    display.setTextColor(WHITE);
    display.setCursor(display.getCursorX() - 1, cy); }

  if (alarmField == A_MIN) display.print("[" + two(alarmMin) + "]");
  else display.print(two(alarmMin));

  
  { int cx = display.getCursorX(), cy = display.getCursorY();
    display.setCursor(cx - 1, cy);
    if (colonOn) display.setTextColor(WHITE);
    else         display.setTextColor(BLACK, BLACK);
    display.write(':');
    display.setTextColor(WHITE);
    display.setCursor(display.getCursorX() - 1, cy); }

  if (alarmField == A_SEC) display.print("[" + two(alarmSec) + "]");
  else display.print(two(alarmSec));

  // AM/PM row
  String ap = (alarmHour < 12) ? "AM" : "PM";
  display.setTextSize(1);
  display.setCursor(0, 20);
  if (alarmField == A_AMPM) display.print("[" + ap + "]");
  else display.print(ap);

  // ON/OFF row
  display.setCursor(0, 32);
  const char* onoff = alarmEnabled ? "ON" : "OFF";
  if (alarmField == A_ENABLE) {
    display.print("[");
    display.print(onoff);
    display.print("]");
  } else {
    display.print(onoff);
  }

  // Footer: - SET +
  display.setCursor(0, 54);
  display.print("-");

  const char* midLbl = "SET";
  int midW = 6 * 3;
  display.setCursor((SCREEN_WIDTH - midW) / 2, 54);
  display.print(midLbl);

  const char* plusLbl = "+";
  int plusW = 6 * 1;
  display.setCursor(SCREEN_WIDTH - plusW, 54);
  display.print(plusLbl);

  display.display();
}

void drawAlarmRinging() {
  if (!displayReady) return;

  display.clearDisplay();

  if (alarmFlashOn) {
    const char* A = "ALARM";
    int w = 6 * 5;
    int x = (SCREEN_WIDTH - w) / 2;
    int y = (SCREEN_HEIGHT - 8) / 2;

    display.setTextSize(1);
    display.setCursor(x, y);
    display.print(A);
  }

  display.display();
}

void serviceAlarmRinging() {
  if (!alarmRinging) return;

  unsigned long now = millis();

  // Flash toggle
  if (now >= alarmFlashNext) {
    alarmFlashOn = !alarmFlashOn;
    alarmFlashNext = now + ALARM_FLASH_MS;
    drawAlarmRinging();
  }

  // Beep scheduling — alarm owns the buzzer entirely while ringing
  if (now >= alarmNextEventAt) {
    if (buzzing) {
      noTone(BUZZER_PIN);
      buzzing = false;
      if (alarmBurstBeepIdx < 4) {
        alarmNextEventAt = now + ALARM_INTER_BEEP;
      } else {
        alarmNextEventAt = now + ALARM_BURST_GAP;
        alarmBurstBeepIdx = 0;
      }
    } else {
      tone(BUZZER_PIN, BUZZ_HZ);
      buzzing = true;
      alarmBurstBeepIdx++;
      alarmNextEventAt = now + ALARM_BEEP_MS;
    }
  }
}

void startAlarmRinging(int fromView /*AppView*/) {
  alarmRinging = true;
  returnViewOnAlarm = (AppView)fromView;

  alarmBurstBeepIdx = 0;
  alarmNextEventAt = 0;

  alarmFlashOn = true;
  alarmFlashNext = millis() + ALARM_FLASH_MS;

  // If the screen is off, wake it and remember we did
  alarmWokeScreen = !screenEnabled;
  if (alarmWokeScreen) {
    screenEnabled = true;
    oledOn();
  }

  drawAlarmRinging();
}

void stopAlarmRinging() {
  alarmRinging = false;
  if (buzzing) buzzerStop();

  // If we woke the screen for the alarm, put it back to sleep
  if (alarmWokeScreen) {
    alarmWokeScreen = false;
    if (displayReady) {
      display.clearDisplay();
      display.display();
    }
    oledOff();
    screenEnabled = false;
  }
}

// =====================================================================
// TIMER APP
// =====================================================================

// Timer fields (mirrors AlarmField layout but no am/pm or on/off)
enum TimerField { T_HOUR, T_MIN, T_SEC, T_STARTSTOP };

bool inTimerSet = false;

// The "set" time (what user configured — reset target)
int timerSetHour = 0;
int timerSetMin  = 0;
int timerSetSec  = 0;

// The live countdown
int timerHour = 0;
int timerMin  = 0;
int timerSec  = 0;

bool timerRunning = false;
bool timerRinging = false;
TimerField timerField = T_HOUR;

// Ringing state (re-use same beep/flash constants as alarm)
uint8_t timerBurstBeepIdx = 0;
unsigned long timerNextEventAt = 0;
bool timerFlashOn = true;
unsigned long timerFlashNext = 0;
bool timerWokeScreen = false;
AppView returnViewOnTimer = VIEW_WATCH;

// millis() timestamp of the last timer tick
unsigned long timerLastTickMs = 0;

static inline void timerMoveCursor(int dx, int dy) {
  int row, col;
  switch (timerField) {
    case T_HOUR:      row = 0; col = 0; break;
    case T_MIN:       row = 0; col = 1; break;
    case T_SEC:       row = 0; col = 2; break;
    case T_STARTSTOP: row = 1; col = 0; break;
  }
  row += dy;
  if (row < 0) row = 0;
  if (row > 1) row = 1;
  int maxc = (row == 0 ? 2 : 0);
  col += dx;
  if (col < 0) col = 0;
  if (col > maxc) col = maxc;
  if (row == 0) timerField = (col == 0 ? T_HOUR : (col == 1 ? T_MIN : T_SEC));
  else          timerField = T_STARTSTOP;
}

static inline void timerAdjust(int delta) {
  if (timerRunning) return;   // don't allow edit while running
  switch (timerField) {
    case T_HOUR:
      timerSetHour += delta;
      if (timerSetHour < 0)  timerSetHour = 99;
      if (timerSetHour > 99) timerSetHour = 0;
      timerHour = timerSetHour;
      break;
    case T_MIN:
      timerSetMin += delta;
      while (timerSetMin < 0)  timerSetMin += 60;
      timerSetMin %= 60;
      timerMin = timerSetMin;
      break;
    case T_SEC:
      timerSetSec += delta;
      while (timerSetSec < 0)  timerSetSec += 60;
      timerSetSec %= 60;
      timerSec = timerSetSec;
      break;
    case T_STARTSTOP:
      break;   // start/stop handled by select (b2 on that field)
  }
}

// Called every second from the main loop when timerRunning is true.
// Independent of tickOneSecond() — does NOT touch hour/minute/second globals.
static void timerTickDown() {
  if (!timerRunning) return;

  if (timerSec > 0) {
    timerSec--;
  } else if (timerMin > 0) {
    timerMin--;
    timerSec = 59;
  } else if (timerHour > 0) {
    timerHour--;
    timerMin = 59;
    timerSec = 59;
  } else {
    // Reached 00:00:00 — signal caller to fire the alarm
    timerRunning = false;
    // Caller (serviceTimerCountdown or timer set loop) will call startTimerRinging()
  }
}

void drawTimerSetView() {
  if (!displayReady) return;

  display.clearDisplay();
  display.setTextColor(WHITE);

  bool colonOn = (timerRunning) ? ((millis() / 500) % 2 == 0) : true;

  display.setTextSize(2);
  display.setCursor(0, 0);

  // Hours (2 digits, 00-99)
  String hStr = (timerHour < 10 ? "0" : "") + String(timerHour);
  if (timerField == T_HOUR) display.print("[" + hStr + "]");
  else display.print(hStr);

  //Cursors
  { int cx = display.getCursorX(), cy = display.getCursorY();
    display.setCursor(cx - 1, cy);
    if (colonOn) display.setTextColor(WHITE);
    else         display.setTextColor(BLACK, BLACK);
    display.write(':');
    display.setTextColor(WHITE);
    display.setCursor(display.getCursorX() - 1, cy); }

  String mStr = two(timerMin);
  if (timerField == T_MIN) display.print("[" + mStr + "]");
  else display.print(mStr);

  
  { int cx = display.getCursorX(), cy = display.getCursorY();
    display.setCursor(cx - 1, cy);
    if (colonOn) display.setTextColor(WHITE);
    else         display.setTextColor(BLACK, BLACK);
    display.write(':');
    display.setTextColor(WHITE);
    display.setCursor(display.getCursorX() - 1, cy); }

  String sStr = two(timerSec);
  if (timerField == T_SEC) display.print("[" + sStr + "]");
  else display.print(sStr);

  // START/STOP row (where AM/PM was)
  display.setTextSize(1);
  display.setCursor(0, 20);
  const char* ssLbl = timerRunning ? "STOP" : "START";
  if (timerField == T_STARTSTOP) {
    display.print("[");
    display.print(ssLbl);
    display.print("]");
  } else {
    display.print(ssLbl);
  }

  // Footer: - (empty center) +
  display.setCursor(0, 54);
  display.print("-");

  const char* exitLbl = "EXIT";
  int exitW = 6 * 4;
  display.setCursor((SCREEN_WIDTH - exitW) / 2, 54);
  display.print(exitLbl);

  const char* plusLbl = "+";
  display.setCursor(SCREEN_WIDTH - 6, 54);
  display.print(plusLbl);

  display.display();
}

void drawTimerRinging() {
  if (!displayReady) return;
  display.clearDisplay();
  if (timerFlashOn) {
    const char* T = "TIMER";
    int w = 6 * 5;
    int x = (SCREEN_WIDTH - w) / 2;
    int y = (SCREEN_HEIGHT - 8) / 2;
    display.setTextSize(1);
    display.setCursor(x, y);
    display.print(T);
  }
  display.display();
}

void serviceTimerRinging() {
  if (!timerRinging) return;

  unsigned long now = millis();

  // Flash toggle
  if (now >= timerFlashNext) {
    timerFlashOn = !timerFlashOn;
    timerFlashNext = now + ALARM_FLASH_MS;
    drawTimerRinging();
  }

  // Beep scheduling — timer owns the buzzer entirely while ringing
  if (now >= timerNextEventAt) {
    if (buzzing) {
      noTone(BUZZER_PIN);
      buzzing = false;
      if (timerBurstBeepIdx < 4) {
        timerNextEventAt = now + ALARM_INTER_BEEP;
      } else {
        timerNextEventAt = now + ALARM_BURST_GAP;
        timerBurstBeepIdx = 0;
      }
    } else {
      tone(BUZZER_PIN, BUZZ_HZ);
      buzzing = true;
      timerBurstBeepIdx++;
      timerNextEventAt = now + ALARM_BEEP_MS;
    }
  }
}

void startTimerRinging(int fromView) {
  timerRinging = true;
  returnViewOnTimer = (AppView)fromView;
  timerBurstBeepIdx = 0;
  timerNextEventAt = 0;
  timerFlashOn = true;
  timerFlashNext = millis() + ALARM_FLASH_MS;
  timerWokeScreen = !screenEnabled;
  if (timerWokeScreen) { screenEnabled = true; oledOn(); }
  drawTimerRinging();
}

void stopTimerRinging() {
  timerRinging = false;
  if (buzzing) buzzerStop();
  // Reset countdown back to the last set time
  timerHour = timerSetHour;
  timerMin  = timerSetMin;
  timerSec  = timerSetSec;
  timerRunning = false;
  if (timerWokeScreen) {
    timerWokeScreen = false;
    if (displayReady) { display.clearDisplay(); display.display(); }
    oledOff();
    screenEnabled = false;
  }
}

// Called from every app's background loop to keep the timer ticking independently.
static inline void serviceTimerCountdown() {
  if (!timerRunning || timerRinging) return;
  unsigned long now = millis();
  if (now - timerLastTickMs >= 1000UL) {
    unsigned long secs = (now - timerLastTickMs) / 1000UL;
    timerLastTickMs += secs * 1000UL;
    for (unsigned long i = 0; i < secs; ++i) {
      timerTickDown();
      if (!timerRunning) {
        // timerTickDown already set timerRinging = true; fire the full ring sequence
        startTimerRinging(VIEW_TIMER);
        break;
      }
    }
  }
}

// CALENDAR app state & drawing
bool inCalendar = false;
int calYear = 2025;
int calMonth = 10;  // 1..12

static inline void calPrevMonth() {
  calMonth--;
  if (calMonth < 1) {
    calMonth = 12;
    calYear--;
  }
}

static inline void calNextMonth() {
  calMonth++;
  if (calMonth > 12) {
    calMonth = 1;
    calYear++;
  }
}


// ── GAMES SUBMENU ────────────────────────────────────────────────────────────
void drawGamesMenu() {
  if (!displayReady) return;
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);

  // Mini clock top-right (same as clock submenu)
  String t = timeHHMMSS();
  int tpx = t.length() * 6;
  display.setCursor(SCREEN_WIDTH - tpx, 0);
  display.print(t);
  String ap = (hour < 12) ? "AM" : "PM";
  display.setCursor(SCREEN_WIDTH - 12, 12);
  display.print(ap);

  // Menu items left side with > cursor (same as clock submenu)
  const char* items[] = { "IRON TIDES", "BLACKJACK", "OBSCURUS", "NIGHT DRIVE", "SLOTS" };
  int y = 0;
  for (int i = 0; i < 5; i++) {
    display.setCursor(0, y);
    display.print((gamesMenuCursor == i) ? ">" : " ");
    display.print(items[i]);
    y += 9;
  }

  // EXIT bottom-right (same as clock submenu)
  display.setCursor(SCREEN_WIDTH - 24, 55);
  display.print("EXIT");

  display.display();
}

void gameExitToMenu() {
  gameplayRunning = false;  // back to menu — encoder free to control radio
  // Stop whichever game is active
  if (ironTidesIsActive()) ironTidesExit();
  if (blackjackIsActive()) blackjackExit();
  if (obscurusIsActive())  obscurusExit();
  if (skyIsActive())        skyExit();
  if (racingIsActive())     racingExit();
  if (slotsIsActive())      slotsExit();

  // Return to the games submenu (not the main menu)
  inGame = true;
  inGamesMenu = true;
  gamesMenuCursor = 0;
  menuEnableButtonsAt = millis() + 300;
  drawGamesMenu();
}


void drawCalendar() {
  if (!displayReady) return;

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);

  // Header: "<" MMM YYYY ">" all on the same row
  display.setCursor(0, 0);
  display.print("<");

  char hdr[16];
  snprintf(hdr, sizeof(hdr), "%s %04d", kMon3[calMonth - 1], calYear);
  int hdrW = (int)strlen(hdr) * 6;
  int hx = (SCREEN_WIDTH - hdrW) / 2;
  display.setCursor(hx, 0);
  display.print(hdr);

  display.setCursor(SCREEN_WIDTH - 6, 0);
  display.print(">");

  // Grid params
  const int startY = 12;   // rows start under header
  const int rowH = 8;      // 6 rows * 8 = 48px
  const int colW = 18;     // 7 cols * 18 = 126px
  const int startX = 0;

  // First weekday of the month and days in month
  int firstW = wdayFromYMD(calYear, calMonth, 1);  // 0=SUN..6=SAT
  int mdays = daysInMonth(calYear, calMonth);

  // Fill 6x7 cells (blank outside current month)
  for (int i = 0; i < 42; ++i) {
    int row = i / 7;
    int col = i % 7;
    int d = i - firstW + 1;  // day number we would show here

    if (d >= 1 && d <= mdays) {
      int x = startX + col * colW;
      int y = startY + row * rowH;

      char buf[4];
      if (d < 10) snprintf(buf, sizeof(buf), " %d", d);
      else snprintf(buf, sizeof(buf), "%d", d);

      // Highlight today: white box, black text
      bool isToday = (calYear == year && calMonth == month && d == day);
      if (isToday) {
        display.fillRect(x, y, colW - 1, rowH - 1, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      }

      display.setCursor(x, y);
      display.print(buf);

      if (isToday) {
        display.setTextColor(SSD1306_WHITE); // restore for rest of grid
      }
    }
  }

  // Footer: centered EXIT only
  const char* exitLbl = "EXIT";
  int exitW = 4 * 6;
  display.setCursor((SCREEN_WIDTH - exitW) / 2, 54);
  display.print(exitLbl);

  display.display();
}

// ========================= CALCULATOR APP ===================================
// State
bool inCalculator = false;

String calcExpr = "";
String calcResultStr = "";
String calcErrStr = "";

bool calcHasAns = false;
double calcAnsValue = 0.0;

// Keypad geometry (shifted left ~5px)
static const int KP_X = -1;  // moved left
static const int KP_Y = 28;
static const int KP_W = 20;
static const int KP_H = 8;
static const int KP_COLS = 6;
static const int KP_ROWS = 4;

// Grid (left->right, top->bottom)
static const char* CALC_KEYS[KP_ROWS][KP_COLS] = {
  {"7","8","9","DEL","AC","("},
  {"4","5","6","x","/",")"},  
  {"1","2","3","+","-","SQRT"},
  {"0",".","^","Ans","=","EXIT"}
};

int calcCursorRow = 0;
int calcCursorCol = 0;

// Tokenizer & parser types
enum TokType { T_NUM, T_OP, T_FUNC, T_LPAREN, T_RPAREN, T_ANS };

struct Tok {
  TokType t;
  double val;    // for T_NUM or T_ANS (val holds Ans value during RPN eval)
  char op;       // '+','-','*','/','^'
  String func;   // "SQRT"
};

static inline int opPrec(char op) {
  switch (op) {
    case '^': return 4;  // highest (right-assoc)
    case '*':
    case '/': return 3;
    case '+':
    case '-': return 2;
    default:  return 0;
  }
}

static inline bool opRightAssoc(char op) {
  return (op == '^');
}

static bool calcTokenize(const String& in, std::vector<Tok>& out, String &err, double ansVal, bool hasAns) {
  out.clear();

  int i = 0, n = in.length();

  auto pushOp = [&](char c) {
    Tok t;
    t.t = T_OP;
    t.op = c;
    t.val = 0;
    t.func = "";
    out.push_back(t);
  };

  auto pushFunc = [&](const String& f) {
    Tok t;
    t.t = T_FUNC;
    t.func = f;
    t.val = 0;
    t.op = 0;
    out.push_back(t);
  };

  auto pushNum = [&](double v) {
    Tok t;
    t.t = T_NUM;
    t.val = v;
    t.op = 0;
    t.func = "";
    out.push_back(t);
  };

  auto pushParen = [&](bool left) {
    Tok t;
    t.t = left ? T_LPAREN : T_RPAREN;
    t.val = 0;
    t.op = 0;
    t.func = "";
    out.push_back(t);
  };

  auto pushAns = [&]() {
    if (!hasAns) {
      err = "No Ans";
      return false;
    }
    Tok t;
    t.t = T_ANS;
    t.val = ansVal;
    t.op = 0;
    t.func = "";
    out.push_back(t);
    return true;
  };

  // expectValue == true means we're positioned where a value may start:
  // start of expression, after '(', or after an operator/function.
  bool expectValue = true;

  auto isDigitOrDot = [](char c) { return (c >= '0' && c <= '9') || c == '.'; };

  while (i < n) {
    char c = in[i];

    if (c == ' ') { i++; continue; }

    // ----- Number (unsigned) -----
    if (isDigitOrDot(c)) {
      int j = i + 1;
      bool sawDot = (c == '.');

      while (j < n) {
        char d = in[j];
        if (d == '.') {
          if (sawDot) break;
          sawDot = true;
          j++;
        } else if (d >= '0' && d <= '9') {
          j++;
        } else break;
      }

      double v = in.substring(i, j).toFloat();
      pushNum(v);
      expectValue = false;
      i = j;
      continue;
    }

    // ----- Unary minus handling -----
    if (c == '-' && expectValue) {
      // Look ahead:
      if (i + 1 < n && isDigitOrDot(in[i + 1])) {
        // Case: "-<number>" => fold sign into number
        int j = i + 2;
        bool sawDot = (in[i + 1] == '.');

        while (j < n) {
          char d = in[j];
          if (d == '.') {
            if (sawDot) break;
            sawDot = true;
            j++;
          } else if (d >= '0' && d <= '9') {
            j++;
          } else break;
        }

        double v = -in.substring(i + 1, j).toFloat();
        pushNum(v);
        expectValue = false;
        i = j;
        continue;
      } else {
        // Case: "-<non-number>" e.g. "-( ... )", "-Ans", "-SQRT( ... )"
        // Emit "0 - " and continue with the next token as the right operand.
        pushNum(0.0);
        pushOp('-');
        expectValue = true;  // still expecting a value next
        i++;                 // consume the '-'
        continue;
      }
    }

    // ----- Binary operators & parens -----
    if (c == '+') { pushOp('+'); expectValue = true; i++; continue; }
    if (c == '-') { pushOp('-'); expectValue = true; i++; continue; }  // binary '-'
    if (c == 'x' || c == 'X' || c == '*') { pushOp('*'); expectValue = true; i++; continue; }
    if (c == '/') { pushOp('/'); expectValue = true; i++; continue; }
    if (c == '^') { pushOp('^'); expectValue = true; i++; continue; }

    if (c == '(') { pushParen(true); expectValue = true; i++; continue; }
    if (c == ')') { pushParen(false); expectValue = false; i++; continue; }

    // ----- Identifiers: SQRT or Ans (must be letters) -----
    if (isalpha(c)) {
      int j = i + 1;
      while (j < n && isalpha(in[j])) j++;

      String word = in.substring(i, j);
      word.toUpperCase();

      if (word == "SQRT") {
        pushFunc("SQRT");
        expectValue = true;
        i = j;
        continue;
      }

      if (word == "ANS") {
        if (!pushAns()) return false;
        expectValue = false;
        i = j;
        continue;
      }

      err = "Unknown id: " + word;
      return false;
    }

    err = String("Bad char: ") + c;
    return false;
  }

  return true;
}
static bool calcToRPN(const std::vector<Tok>& in, std::vector<Tok>& out, String &err) {
  out.clear();
  std::vector<Tok> opstack;

  auto popOp = [&]() {
    if (opstack.empty()) return false;
    out.push_back(opstack.back());
    opstack.pop_back();
    return true;
  };

  for (size_t k = 0; k < in.size(); ++k) {
    const Tok &t = in[k];
    switch (t.t) {
      case T_NUM:
      case T_ANS:
        out.push_back(t);
        break;

      case T_FUNC:
        opstack.push_back(t);
        break;

      case T_OP: {
        char op = t.op;
        while (!opstack.empty()) {
          const Tok &top = opstack.back();
          if (top.t == T_OP) {
            int pTop = opPrec(top.op);
            int pCur = opPrec(op);
            if (!opRightAssoc(op) ? (pCur <= pTop) : (pCur < pTop)) {
              out.push_back(top);
              opstack.pop_back();
              continue;
            }
          } else if (top.t == T_FUNC) {
            out.push_back(top);
            opstack.pop_back();
            continue;
          }
          break;
        }
        opstack.push_back(t);
      } break;

      case T_LPAREN:
        opstack.push_back(t);
        break;

      case T_RPAREN: {
        bool matched = false;
        while (!opstack.empty()) {
          Tok top = opstack.back();
          opstack.pop_back();
          if (top.t == T_LPAREN) {
            matched = true;
            break;
          }
          out.push_back(top);
        }
        if (!matched) {
          err = "Mismatched )";
          return false;
        }
        if (!opstack.empty() && opstack.back().t == T_FUNC) {
          out.push_back(opstack.back());
          opstack.pop_back();
        }
      } break;
    }
  }

  while (!opstack.empty()) {
    if (opstack.back().t == T_LPAREN) {
      err = "Mismatched (";
      return false;
    }
    out.push_back(opstack.back());
    opstack.pop_back();
  }

  return true;
}

static bool calcEvalRPN(const std::vector<Tok>& rpn, double &val, String &err) {
  std::vector<double> st;
  st.reserve(16);

  auto need = [&](int n) -> bool {
    if ((int)st.size() < n) {
      err = "Syntax error";
      return false;
    }
    return true;
  };

  for (const Tok& t : rpn) {
    if (t.t == T_NUM) st.push_back(t.val);
    else if (t.t == T_ANS) st.push_back(t.val);
    else if (t.t == T_FUNC) {
      if (t.func == "SQRT") {
        if (!need(1)) return false;
        double a = st.back(); st.pop_back();
        if (a < 0) {
          err = "Domain error";
          return false;
        }
        st.push_back(sqrt(a));
      } else {
        err = "Func?";
        return false;
      }
    } else if (t.t == T_OP) {
      if (!need(2)) return false;
      double b = st.back(); st.pop_back();
      double a = st.back(); st.pop_back();

      switch (t.op) {
        case '+': st.push_back(a + b); break;
        case '-': st.push_back(a - b); break;
        case '*': st.push_back(a * b); break;
        case '/':
          if (b == 0.0) {
            err = "Divide by 0";
            return false;
          }
          st.push_back(a / b);
          break;
        case '^': st.push_back(pow(a, b)); break;
        default:
          err = "Op?";
          return false;
      }
    } else {
      err = "Tok?";
      return false;
    }
  }

  if (st.size() != 1) {
    err = "Syntax error";
    return false;
  }

  val = st.back();
  return true;
}

static String calcFormat(double v) {
  double av = fabs(v);
  if ((av > 0 && av < 1e-6) || (av >= 1e6)) {
    // scientific with 3 significant digits
    char buf[24];
    snprintf(buf, sizeof(buf), "%.2e", v);
    return String(buf);
  } else {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6f", v);
    return String(buf);
  }
}

void drawCalculator() {
  if (!displayReady) return;

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);

  // Top lines: expression, then result/error (if present)

  // Line 0: expression
  String expr = calcExpr;

  // Trim if too long
  int maxChars = SCREEN_WIDTH / 6;
  if ((int)expr.length() > maxChars) {
    expr = expr.substring(expr.length() - maxChars);  // keep tail
  }

  display.setCursor(0, 0);
  display.print(expr);

  // Line 1: result or error
  if (calcErrStr.length() > 0) {
    display.setCursor(0, 10);
    display.print(calcErrStr);
  } else if (calcResultStr.length() > 0) {
    display.setCursor(0, 10);
    display.print(calcResultStr);
  }

  // Draw keypad with brackets around selected key (shifted 2px outward)
  for (int r = 0; r < KP_ROWS; ++r) {
    for (int c = 0; c < KP_COLS; ++c) {
      int x = KP_X + c * KP_W;
      int y = KP_Y + r * KP_H;

      const char* label = CALC_KEYS[r][c];
      int w = strlen(label) * 6;
      int tx = x + ((KP_W - w) / 2);
      if (tx < x) tx = x;
      int ty = y;  // single-line

      if (r == calcCursorRow && c == calcCursorCol) {
        display.setCursor(tx - 6, ty);  // was -3 -> move 2px further left
        display.print("[");
        display.setCursor(tx, ty);
        display.print(label);
        display.setCursor(tx + w - 1, ty);  // was +1 -> move 2px further right
        display.print("]");
      } else {
        display.setCursor(tx, ty);
        display.print(label);
      }
    }
  }

  display.display();
}
void drawCompass() {
  sensors_event_t ori;
  bno.getEvent(&ori, Adafruit_BNO055::VECTOR_EULER);

  float rawHeading = wrap360(ori.orientation.x);
  float heading = wrap360(rawHeading - CAL_OFFSET_DEG);

  heading = wrap360(heading + 180.0f);

  float ang = (-200.0f - heading) * (M_PI / 180.0f);
  float displayHeading = wrap360(heading - 90.0f);

  float roll = ori.orientation.z;
  if (roll > 90) roll -= 180;
  if (roll < -90) roll += 180;
  roll = -roll;

  float pitch = ori.orientation.y;

  display.clearDisplay();

  // ==========================
  // Throttled B-field sampling
  // ==========================
  unsigned long now = millis();
  if (now - lastBmagRead >= BMAG_INTERVAL_MS) {
    sensors_event_t magEvent;
    bno.getEvent(&magEvent, Adafruit_BNO055::VECTOR_MAGNETOMETER);

    float Bx = magEvent.magnetic.x;
    float By = magEvent.magnetic.y;
    float Bz = magEvent.magnetic.z;

    cachedBmag = sqrt(Bx * Bx + By * By + Bz * Bz);
    lastBmagRead = now;
  }

  // ===== Display cached B-field =====
  display.setCursor(0, 0);
  display.print("B:");
  display.print(cachedBmag, 0);
  display.print("uT");

  // ===== Heading =====
  display.setCursor(0, 10);
  display.print("H:");
  display.print((int)displayHeading);
  display.write(247);  // degree symbol

  // ================================
  // Compass circle & pointer
  // ================================
  int cx = SCREEN_WIDTH / 2;
  int cy = ( SCREEN_HEIGHT / 2 ) - 3;
  int radius = 20;

  display.drawCircle(cx, cy, radius, WHITE);


  int tipX = cx + (int)(radius * cos(ang));
  int tipY = cy - (int)(radius * sin(ang));
  display.drawLine(cx, cy, tipX, tipY, WHITE);

  // ================================
  // N / S / E / W Labels
  // ================================
  int nX = cx;
  int nY = cy - radius - 6;
  int sX = cx;
  int sY = cy + radius + 4;
  int eX = cx + radius + 4;
  int eY = cy;
  int wX = cx - radius - 6;
  int wY = cy;

  display.setCursor(nX - 2, nY - 1);
  display.print("N");

  display.setCursor(sX - 2, sY - 3);
  display.print("S");

  display.setCursor(eX - 3, eY - 3);
  display.print("E");

  display.setCursor(wX + 1, wY - 3);
  display.print("W");

  // ================================
  // Roll bar (horizontal)
  // ================================
  roll = constrain(roll, -30, 30);

  int hBarY = SCREEN_HEIGHT - 6;
  int hBarCenterX = SCREEN_WIDTH / 2;
  int hBarMax = 50;
  float hLenF = (roll / 30.0f) * hBarMax;
  int hLen = (int)hLenF;

  display.drawFastHLine(
    hBarCenterX - hBarMax,
    hBarY,
    hBarMax * 2,
    WHITE
  );

  if (hLen > 0)
    display.fillRect(hBarCenterX, hBarY - 2, hLen, 4, WHITE);
  else
    display.fillRect(hBarCenterX + hLen, hBarY - 2, -hLen, 4, WHITE);

  // ================================
  // Pitch bar (vertical)
  // ================================
  pitch = constrain(pitch, -30, 30);

  int vBarX = SCREEN_WIDTH - 6;
  int vBarCenterY = SCREEN_HEIGHT / 2 - 2;
  int vBarMax = 20;

  int vLen = map((int)pitch, -30, 30, -vBarMax, vBarMax);

  display.drawFastVLine(
    vBarX,
    vBarCenterY - vBarMax,
    vBarMax * 2,
    WHITE
  );

  if (vLen > 0)
    display.fillRect(vBarX - 2, vBarCenterY, 4, vLen, WHITE);
  else
    display.fillRect(vBarX - 2, vBarCenterY + vLen, 4, -vLen, WHITE);

  display.display();
}

// Setup & Loop
void calibrateSeaLevelFromElevation(float);  // forward
void drawCalendar();                         // keep compiler happy if sections move

// END MICROPHONE APP
// ============================================================================
// ---- Distance app enter/exit prototypes ----
static void distanceEnter();
static void distanceExit();

static void distanceEnter() {
  inMenu = false;
  inDistance = true;
  uiDirty = true;

  // Try to init sensor if not ready yet
  if (!tofReady) {
    tofReady = tof.begin();
    if (tofReady) {
      tof.startRanging();
      tofRanging = true;
      tofLastReadMs = 0;
      tofDistanceMm = -1;
      distImperial = false;
      distUnitIdx = 0;
    }
  } else if (!tofRanging) {
    tof.startRanging();
    tofRanging = true;
  }
}

static void distanceExit() {
  // Stop sensor ranging if it was running
  if (tofReady && tofRanging) {
    tof.stopRanging();
    tofRanging = false;
  }

  inDistance = false;
  inMenu = true;
  uiDirty = true;
  lastSecondDrawn = 255;
  menuEnableButtonsAt = millis() + 120;
  redrawMenuNow();
}

static void handleDistance() {
  // BTN3 exits back to menu
  if (btnWasShortPressed(b3)) {
    buzzerStartLow();   
    distanceExit();
    return;
  }

  // BTN1 cycles units within the current system (MET or IMP)
  if (btnWasShortPressed(b1)) {
    distUnitIdx = (distUnitIdx + 1) % 3;
    uiDirty = true;
  }

  // BTN2 toggles MET <-> IMP, keeping the same index pairing
  if (btnWasShortPressed(b2)) {
    distImperial = !distImperial;
    uiDirty = true;
  }


  // If sensor init failed, show an error but still allow exit
  if (!tofReady) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("VL53L1X FAIL");
    display.println("Press B3 to exit");
    display.display();
    return;
  }

  // Non-blocking read ~20Hz (instead of delay(50))
  uint32_t now = millis();
  if (now - tofLastReadMs >= 50) {
    tofLastReadMs = now;

    if (tof.dataReady()) {
      tofDistanceMm = tof.distance();
      tof.clearInterrupt();
      uiDirty = true;
    }
  }

  if (!uiDirty) return;
  uiDirty = false;

  // Draw screen
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Title
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Distance:");
    // -----------------------------
    // Build valueStr + unitStr
    // distUnitIdx: 0,1,2
    // MET: mm, cm, m
    // IMP: in, ft, yd
    // -----------------------------
    String valueStr = "--";
    const char* unitStr = "";

    if (tofDistanceMm >= 0) {
      if (!distImperial) {
        // METRIC
        if (distUnitIdx == 0) {          // mm
          valueStr = String((int)tofDistanceMm);
          unitStr = "mm";
        } else if (distUnitIdx == 1) {   // cm
          float cm = tofDistanceMm / 10.0f;
          valueStr = String(cm, 1);
          unitStr = "cm";
        } else {                         // m
          float m = tofDistanceMm / 1000.0f;
          valueStr = String(m, 2);
          unitStr = "m";
        }
      } else {
        // IMPERIAL
        if (distUnitIdx == 0) {          // inches
          float inches = tofDistanceMm / 25.4f;
          valueStr = String(inches, 1);
          unitStr = "in";
        } else if (distUnitIdx == 1) {   // feet
          float ft = tofDistanceMm / 304.8f;
          valueStr = String(ft, 2);
          unitStr = "ft";
        } else {                         // yards
          float yd = tofDistanceMm / 914.4f;
          valueStr = String(yd, 2);
          unitStr = "yd";
        }
      }
    }

  // Big value (hard-left)
  display.setTextSize(2);
  display.setCursor(2, 24);
  display.print(valueStr);

  // Unit label (size 1, after the number)
  display.setTextSize(1);
  display.print(" ");
  display.print(unitStr);

  // EXIT bottom-right 
  display.setTextSize(1);
  display.setCursor(104, 55);
  display.print("EXIT");

  // UNIT bottom-left + MET/IMP bottom-center
  display.setTextSize(1);
  display.setCursor(0, 55);
  display.print("UNIT");

  display.setCursor(55, 55);
  display.print(distImperial ? "IMP" : "MET");

  display.display();

}


bool gpsSyncClockOnBoot(uint32_t timeoutMs);  // defined in Locationapp.cpp

void setup() {
  Serial.begin(115200);

  // Bring up DF UART early and issue stop so DFPlayer doesn't keep playing after reset
  DFSerial.begin(9600, SERIAL_8N1, D10, D9);

  // Flush any junk from DFPlayer -> ESP
  while (DFSerial.available()) DFSerial.read();

  // GUARANTEED stop: init DFPlayer once, then stop
  radioEnsureInit();
  if (radioReady) {
    dfp.stop();
    delay(50);
  }

  radioTrack = RADIO_TRACK_MIN;

  // Ensure radio stays "off" after boot/reset
  radioActive = false;
  radioPaused = false;


  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

 

  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);

  // initialize encoder state so first transition isn't garbage
  encPrevAB = (((digitalRead(ENC_A_PIN) == HIGH) ? 1 : 0) << 1) |
            ((digitalRead(ENC_B_PIN) == HIGH) ? 1 : 0);
  encAccum = 0;



  Wire.begin(D0, D1);
  Wire.setClock(400000);

  ensureDisplayInit();

  // Try GPS time sync at boot (e.g., wait up to 15 seconds)
  gpsSyncClockOnBoot(15000);


  // MCP23008 init: all inputs, pull-ups on GP0..GP6
  bool ok = mcpWriteReg(MCP_IODIR, 0xFF);
  ok = ok && mcpWriteReg(MCP_GPPU, 0xFF);  // pull-ups on GP0..GP7
  g_mcp_ok = ok;

  bmeReady = bme.begin(BME_ADDR, &Wire);
  if (bmeReady) {
    delay(300);
    const float KNOWN_ELEV_M = 22.0f;  // Woodland, CA ~22 m
    calibrateSeaLevelFromElevation(KNOWN_ELEV_M);
  }
  // BNO055 Init
  if (!bno.begin()) {
    Serial.println("BNO055 not detected!");
  }

  bno.setExtCrystalUse(true);
  delay(50);

  normalizeDateTime();
  lastTickMs = millis();
}

void loop() {

  // --- Buzzer housekeeping ---
  // Skip auto-stop while alarm or timer ringing owns the buzzer
  if (buzzing && !alarmRinging && !timerRinging && millis() >= buzzEnd) buzzerStop();
  serviceChirp();

  // --- Ensure display is ready ---
  if (!displayReady) ensureDisplayInit();

  // --- Read MCP GPIO + update all buttons ---
  uint8_t v;
  if (mcpReadReg(MCP_GPIO, v)) {
    g_mcp_gpio = v;
    g_mcp_ok = true;
  } else {
    g_mcp_ok = false;
    g_mcp_gpio = 0xFF;
  }

  btnUpdate(b1);
  btnUpdate(b2);
  btnUpdate(b3);
  btnUpdate(bUp);
  btnUpdate(bDown);
  btnUpdate(bRight);
  btnUpdate(bLeft);

  // While gameplay is running the main loop owns the encoder exclusively.
  // A press sets g_gamePauseRequest (game reads this) and clears gameplayRunning
  // so all subsequent presses go to radio control via encSwitchGestureService.
  if (gameplayRunning) {
    if (encSwitchPressedEvent()) {
      g_gamePauseRequest = true;
      gameplayRunning    = false;
    }
  } else {
    encSwitchGestureService();
  }

  radioService();

  // encoder rotation -> volume
  int8_t step = encoderReadStep();
  if (step != 0) {
    if (radioActive || inRecordings) {
      radioSetVolume(radioVolume + step);
    }
  }

  radioService();

  batteryBackgroundSample();
  checkLowBatteryAlert();   
  // ===== UNIVERSAL ALARM CHECK =====
  // Alarm should fire EVERYWHERE except when in ALARM SET or TIMER SET mode.
  bool allowAlarm = !inAlarmSet && !inTimerSet;

  if (
    allowAlarm &&
    !alarmRinging &&
    alarmEnabled &&
    timeMatchesAlarmNow()
  ) {
    AppView ret = VIEW_WATCH;

    if (inMenu) ret = VIEW_MENU;
    else if (inClockMenu) ret = VIEW_MENU;
    else if (setMode) ret = VIEW_MENU;
    else if (inPeriodicTable) ret = VIEW_CHEM;
    else if (inDataView) ret = VIEW_DATA;
    else if (inCalendar) ret = VIEW_CALENDAR;
    else if (inCalculator) ret = VIEW_CALC;
    else if (inTone) ret = VIEW_TONE;
    else if (inTimerSet) ret = VIEW_TIMER;

    if (inTone && tonePlaying) toneStop();

    startAlarmRinging(ret);
  }

  // ALARM RINGING (dominates everything)
  if (alarmRinging) {
    if (
      btnWasShortPressed(b1) ||
      btnWasShortPressed(b2) ||
      btnWasShortPressed(b3) ||
      btnWasShortPressed(bUp) ||
      btnWasShortPressed(bDown) ||
      btnWasShortPressed(bLeft) ||
      btnWasShortPressed(bRight)
    ) {
      stopAlarmRinging();

      inMenu = inPeriodicTable = inDataView =
      setMode = inAlarmSet = inCalendar =
      inCalculator = inTone = inTimerSet = false;

      switch (returnViewOnAlarm) {
        case VIEW_MENU:
          inMenu = true;
          redrawMenuNow();
          break;

        case VIEW_CHEM:
          inPeriodicTable = true;
          redrawChemNow();
          break;

        case VIEW_DATA:
          inDataView = true;
          redrawDataNow();
          break;

        case VIEW_SET:
          setMode = true;
          redrawClockNow();
          break;

        case VIEW_ALARMSET:
          inAlarmSet = true;
          drawAlarmSetView();
          break;

        case VIEW_CALENDAR:
          inCalendar = true;
          redrawCalendarNow();
          break;

        case VIEW_CALC:
          inCalculator = true;
          redrawCalculatorNow();
          break;

        case VIEW_TONE:
          inTone = true;
          toneStop();
          redrawToneNow();
          break;

        case VIEW_TIMER:
          inTimerSet = true;
          drawTimerSetView();
          break;

        default:
          redrawClockNow();
          break;
      }

      delay(1);
      return;
    }

    serviceAlarmRinging();
    serviceTimerRinging();

    unsigned long now = millis();
    if (!setMode && now - lastTickMs >= 1000UL) {
      unsigned long secs = (now - lastTickMs) / 1000UL;
      for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
      lastTickMs += secs * 1000UL;
      maybeHourlyChirp();
      serviceTimerCountdown();
    }

    delay(1);
    return;
  }
  // TIMER RINGING (dominates everything except alarm)
  if (timerRinging) {
    if (
      btnWasShortPressed(b1) ||
      btnWasShortPressed(b2) ||
      btnWasShortPressed(b3) ||
      btnWasShortPressed(bUp) ||
      btnWasShortPressed(bDown) ||
      btnWasShortPressed(bLeft) ||
      btnWasShortPressed(bRight)
    ) {
      stopTimerRinging();

      inMenu = inPeriodicTable = inDataView =
      setMode = inAlarmSet = inCalendar =
      inCalculator = inTone = inTimerSet = false;

      switch (returnViewOnTimer) {
        case VIEW_MENU:
          inMenu = true;
          redrawMenuNow();
          break;
        case VIEW_TIMER:
          inTimerSet = true;
          drawTimerSetView();
          break;
        default:
          redrawClockNow();
          break;
      }

      delay(1);
      return;
    }

    serviceTimerRinging();

    unsigned long nowTR = millis();
    if (nowTR - lastTickMs >= 1000UL) {
      unsigned long secs = (nowTR - lastTickMs) / 1000UL;
      for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
      lastTickMs += secs * 1000UL;
      maybeHourlyChirp();
      serviceTimerCountdown();
    }

    delay(1);
    return;
  }

  // SCREEN OFF — Only BTN2 wakes the screen
  if (!screenEnabled) {
    if (btnWasShortPressed(b2)) {
      screenEnabled = true;
      oledOn();

      b1.longFired = b3.longFired = false;
      b1.pressStart = b3.pressStart = 0;
      nextRepeat1 = nextRepeat3 = 0;

      lastSecondDrawn = 255;
      uiDirty = true;
    }

    // Keep internal clock ticking even with display OFF
    unsigned long nowOff = millis();
    if (nowOff - lastTickMs >= 1000UL) {
      unsigned long secs = (nowOff - lastTickMs) / 1000UL;
      for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
      lastTickMs += secs * 1000UL;

      // Alarm allowed to fire while screen is off
      if (!alarmRinging && alarmEnabled && timeMatchesAlarmNow()) {
        startAlarmRinging(VIEW_WATCH);
      }

      maybeHourlyChirp();
      serviceTimerCountdown();
    }

    delay(1);
    return;
  }

  // ENTER MENU from WATCHFACE
  if (
    !inMenu &&
    !inBattery &&
    !inPeriodicTable &&
    !inDataView &&
    !setMode &&
    !inAlarmSet &&
    !inTimerSet && 
    !inCalendar &&
    !inCalculator &&
    !inDistance &&
    !inRadio &&
    !inLocation &&
    !inNavigation && 
    !inGame &&
    !inMetronome &&
    !inSky &&
    !inCamera &&
    !inMic &&
    btnWasShortPressed(b1)
  ) {
    inMenu = true;
    uiDirty = true;
    redrawMenuNow();
    delay(1);
    return;
  }

  // MENU HANDLING
  if (inMenu) {
    bool menuReady = (millis() >= menuEnableButtonsAt);
    unsigned long nowM = millis();

    // Up — short press
    if (menuReady && btnWasShortPressed(bUp)) {
      if (menuIndex > 0) menuIndex--;
      if (menuIndex < menuFirst) menuFirst = menuIndex;
      uiDirty = true;
    }
    // Up — hold to fast scroll
    if (menuReady && btnWasLongPressed(bUp)) {
      if (menuIndex > 0) { menuIndex--; if (menuIndex < menuFirst) menuFirst = menuIndex; uiDirty = true; }
      nextRepeat1 = nowM + REPEAT_MS;
    }
    if (bUp.stable == LOW && bUp.longFired) {
      if (nextRepeat1 && nowM >= nextRepeat1) {
        if (menuIndex > 0) { menuIndex--; if (menuIndex < menuFirst) menuFirst = menuIndex; uiDirty = true; }
        nextRepeat1 += REPEAT_MS;
      }
    } else {
      nextRepeat1 = 0;
    }

    // Down — short press
    if (menuReady && btnWasShortPressed(bDown)) {
      if (menuIndex < MENU_COUNT - 1) menuIndex++;
      if (menuIndex >= menuFirst + MENU_VISIBLE) menuFirst = menuIndex - (MENU_VISIBLE - 1);
      uiDirty = true;
    }
    // Down — hold to fast scroll
    if (menuReady && btnWasLongPressed(bDown)) {
      if (menuIndex < MENU_COUNT - 1) { menuIndex++; if (menuIndex >= menuFirst + MENU_VISIBLE) menuFirst = menuIndex - (MENU_VISIBLE - 1); uiDirty = true; }
      nextRepeat3 = nowM + REPEAT_MS;
    }
    if (bDown.stable == LOW && bDown.longFired) {
      if (nextRepeat3 && nowM >= nextRepeat3) {
        if (menuIndex < MENU_COUNT - 1) { menuIndex++; if (menuIndex >= menuFirst + MENU_VISIBLE) menuFirst = menuIndex - (MENU_VISIBLE - 1); uiDirty = true; }
        nextRepeat3 += REPEAT_MS;
      }
    } else {
      nextRepeat3 = 0;
    }


    if (menuReady && btnWasShortPressed(b2)) {
      if (menuIndex == 0) { // BATTERY
        batteryEnter();
        return;
      } else if (menuIndex == 1) { // CALCULATOR
        inCalculator = true;
        inMenu = false;
        calcExpr = "";
        calcResultStr = "";
        calcErrStr = "";
        calcHasAns = false;
        calcCursorRow = 0;
        calcCursorCol = 0;
        redrawCalculatorNow();
        delay(1);
        return;
        
      } else if (menuIndex == 2) { // CALENDAR
        inCalendar = true;
        inMenu = false;
        calYear = year;
        calMonth = month;
        redrawCalendarNow();
        delay(1);
        return;
      } else if (menuIndex == 3) { // CAMERA
        inCamera = true;
        inMenu = false;
        startCameraApp();
        delay(1);
        return;
      } else if (menuIndex == 4) { // CHEM
        inPeriodicTable = true;
        inMenu = false;
        uiDirty = true;
        redrawChemNow();
        delay(1);
        return;
      } else if (menuIndex == 5) { // CLOCK
        inMenu = false;
        inClockMenu = true;
        clockMenuIndex = 0;
        drawClockSubMenu();
        delay(1);
        return;
      } else if (menuIndex == 6) { // COMPASS
        inCompass = true;
        inMenu = false;
        redrawCompassNow();
        delay(1);
        return;
      } else if (menuIndex == 7) { // DATA
        inDataView = true;
        inMenu = false;
        lastDataRefreshMs = 0;
        uiDirty = true;
        redrawDataNow();
        delay(1);
        return;
      } else if (menuIndex == 8) { // DISTANCE
        distanceEnter();
        return;
      } else if (menuIndex == 9) { // GAME
        inMenu = false;
        inGame = true;
        inGamesMenu = true;
        gamesMenuCursor = 0;
        drawGamesMenu();
        delay(1);
        return;
      } else if (menuIndex == 10) { // LOCATION
        locationEnter();
        return;
      } else if (menuIndex == 11) { // METRONOME
        inMenu = false;
        inMetronome = true;
        metBpm = 90;
        metPlaying = false;
        metNextTickMs = 0;
        metBeepOffMs = 0;
        metNextRepeatDec = 0;
        metNextRepeatInc = 0;
        redrawMetronomeNow();
        delay(1);
        return;
      } else if (menuIndex == 12) { // MICROPHONE
        inMic = true;
        inMenu = false;
        startMicApp();
        delay(1);
        return;
      } else if (menuIndex == 13) { // NAVIGATION
        navigationEnter();
        return;
      } else if (menuIndex == 14) { // RADIO
        radioEnsureInit();
        inMenu = false;
        inRadio = true;
        radioResetMarquee();
        uiDirty = true;
        redrawRadioNow();
        delay(1);
        return;
      } else if (menuIndex == 15) { // RECORDINGS  ← NEW
      recordingsEnter();
      delay(1);
      return;
      } else if (menuIndex == 16) { // SKY
        inMenu = false;
        inSky = true;
        skyEnter();
        delay(1);
        return;
      } else if (menuIndex == 17) { // TUNER
        inTone = true;
        inMenu = false;
        toneIndex = 0;
        toneStop();
        redrawToneNow();
        delay(1);
        return;
      }



    }

    // EXIT MENU via BTN3
    if (menuReady && btnWasShortPressed(b3)) {
      inMenu = false;
      uiDirty = true;
      lastSecondDrawn = 255;
      redrawClockNow();
      delay(1);
      return;
    }

    // Background ticking
    unsigned long nowMenu = millis();
    if (nowMenu - lastTickMs >= 1000UL) {
      unsigned long secs = (nowMenu - lastTickMs) / 1000UL;
      for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
      lastTickMs += secs * 1000UL;

      if (!alarmRinging && alarmEnabled && timeMatchesAlarmNow()) {
        startAlarmRinging(VIEW_MENU);
      }

      maybeHourlyChirp();
      serviceTimerCountdown();
    }

    // Redraw menu
    if (screenEnabled && displayReady) {
      if (second != lastSecondDrawn) {
        uiDirty = true;
      }

      if (uiDirty) {
        drawMenu();
        uiDirty = false;
        lastSecondDrawn = second;
      }
    }

    delay(1);
    return;
  }
  
  // CLOCK SUBMENU HANDLING
  if (inClockMenu) {
    if (btnWasShortPressed(bUp)) {
      if (clockMenuIndex > 0) clockMenuIndex--;
      drawClockSubMenu();
    }

    if (btnWasShortPressed(bDown)) {
      if (clockMenuIndex < 2) clockMenuIndex++;
      drawClockSubMenu();
    }

    if (btnWasShortPressed(b2)) {
      if (clockMenuIndex == 0) {
        inClockMenu = false;
        setMode = true;
        currentField = F_HOUR;
        redrawClockNow();
        return;
      }

      if (clockMenuIndex == 1) {
        inClockMenu = false;
        inAlarmSet = true;
        alarmField = A_HOUR;
        drawAlarmSetView();
        return;
      }

      if (clockMenuIndex == 2) {
        inClockMenu = false;
        inTimerSet = true;
        timerField = T_HOUR;
        drawTimerSetView();
        return;
      }
    }

    if (btnWasShortPressed(b3)) {
      inClockMenu = false;
      inMenu = true;
      redrawMenuNow();
      return;
    }

    // Background clock tick & auto-refresh
    unsigned long nowCS = millis();
    if (nowCS - lastTickMs >= 1000UL) {
      unsigned long secs = (nowCS - lastTickMs) / 1000UL;
      for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
      lastTickMs += secs * 1000UL;

      if (!alarmRinging && alarmEnabled && timeMatchesAlarmNow()) {
        startAlarmRinging(VIEW_MENU);
      }

      maybeHourlyChirp();
      serviceTimerCountdown();
    }

    if (second != lastSecondDrawn) {
      drawClockSubMenu();
      lastSecondDrawn = second;
    }

    delay(1);
    return;
  }

  if (inBattery) {
    if (btnWasShortPressed(b3)) {
        batteryExit();
        delay(1);
        return;
    }
    drawBatteryScreen();
    delay(1);
    return;
  }

  // CHEM VIEW
  if (inPeriodicTable) {
    if (btnWasShortPressed(b1)) {
      ptIndex = (ptIndex - 1 + kElementCount) % kElementCount;
      uiDirty = true;
    }

    if (btnWasShortPressed(b3)) {
      ptIndex = (ptIndex + 1) % kElementCount;
      uiDirty = true;
    }

    unsigned long now = millis();

    if (btnWasLongPressed(b1)) {
      ptIndex = (ptIndex - 1 + kElementCount) % kElementCount;
      uiDirty = true;
      nextRepeat1 = now + REPEAT_MS;
    }

    if (btnWasLongPressed(b3)) {
      ptIndex = (ptIndex + 1) % kElementCount;
      uiDirty = true;
      nextRepeat3 = now + REPEAT_MS;
    }

    if (b1.stable == LOW && b1.longFired) {
      if (nextRepeat1 && now >= nextRepeat1) {
        ptIndex = (ptIndex - 1 + kElementCount) % kElementCount;
        uiDirty = true;
        nextRepeat1 += REPEAT_MS;
      }
    } else {
      nextRepeat1 = 0;
    }

    if (b3.stable == LOW && b3.longFired) {
      if (nextRepeat3 && now >= nextRepeat3) {
        ptIndex = (ptIndex + 1) % kElementCount;
        uiDirty = true;
        nextRepeat3 += REPEAT_MS;
      }
    } else {
      nextRepeat3 = 0;
    }

    if (btnWasShortPressed(b2)) {
      inPeriodicTable = false;
      inMenu = true;
      uiDirty = true;
      lastSecondDrawn = 255;
      nextRepeat1 = nextRepeat3 = 0;
      menuEnableButtonsAt = millis() + 120;
      redrawMenuNow();
      delay(1);
      return;
    }

    unsigned long now2 = millis();
    if (now2 - lastTickMs >= 1000UL) {
      unsigned long secs = (now2 - lastTickMs) / 1000UL;
      for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
      lastTickMs += secs * 1000UL;

      if (!alarmRinging && alarmEnabled && timeMatchesAlarmNow()) {
        startAlarmRinging(VIEW_CHEM);
      }

      maybeHourlyChirp();
      serviceTimerCountdown();
    }

    if (screenEnabled && displayReady && uiDirty) {
      drawPeriodicTableCard();
      uiDirty = false;
    }

    delay(1);
    return;
  }

  // DATA VIEW
  if (inDataView) {
    if (btnWasShortPressed(b1)) {
      g_bodyMode = !g_bodyMode;
      uiDirty = true;
      lastDataRefreshMs = 0;
    }

    if (btnWasShortPressed(b3)) {
      g_metricUnits = !g_metricUnits;
      uiDirty = true;
      lastDataRefreshMs = 0;
    }

    if (btnWasShortPressed(b2)) {
      inDataView = false;
      inMenu = true;
      uiDirty = true;
      lastSecondDrawn = 255;
      menuEnableButtonsAt = millis() + 120;
      redrawMenuNow();
      delay(1);
      return;
    }

    unsigned long now = millis();
    if (now - lastTickMs >= 1000UL) {
      unsigned long secs = (now - lastTickMs) / 1000UL;
      for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
      lastTickMs += secs * 1000UL;

      if (!alarmRinging && alarmEnabled && timeMatchesAlarmNow()) {
        startAlarmRinging(VIEW_DATA);
      }

      maybeHourlyChirp();
      serviceTimerCountdown();
    }

    if (uiDirty || (millis() - lastDataRefreshMs >= DATA_REFRESH_MS)) {
      drawDataView();
      uiDirty = false;
      lastDataRefreshMs = millis();
    }

    delay(1);
    return;
  }

  // CALENDAR VIEW
  if (inCalendar) {
    if (btnWasShortPressed(b1)) {
      calPrevMonth();
      redrawCalendarNow();
    }

    if (btnWasShortPressed(b3)) {
      calNextMonth();
      redrawCalendarNow();
    }

    unsigned long nowC = millis();

    if (btnWasLongPressed(b1)) {
      calPrevMonth();
      redrawCalendarNow();
      nextRepeat1 = nowC + REPEAT_MS;
    }

    if (btnWasLongPressed(b3)) {
      calNextMonth();
      redrawCalendarNow();
      nextRepeat3 = nowC + REPEAT_MS;
    }

    if (b1.stable == LOW && b1.longFired && nextRepeat1 && nowC >= nextRepeat1) {
      calPrevMonth();
      redrawCalendarNow();
      nextRepeat1 += REPEAT_MS;
    } else if (b1.stable != LOW) {
      nextRepeat1 = 0;
    }

    if (b3.stable == LOW && b3.longFired && nextRepeat3 && nowC >= nextRepeat3) {
      calNextMonth();
      redrawCalendarNow();
      nextRepeat3 += REPEAT_MS;
    } else if (b3.stable != LOW) {
      nextRepeat3 = 0;
    }

    if (btnWasShortPressed(b2)) {
      inCalendar = false;
      inMenu = true;
      uiDirty = true;
      lastSecondDrawn = 255;
      menuEnableButtonsAt = millis() + 120;
      redrawMenuNow();
      delay(1);
      return;
    }

    if (nowC - lastTickMs >= 1000UL) {
      unsigned long secs = (nowC - lastTickMs) / 1000UL;
      for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
      lastTickMs += secs * 1000UL;

      if (!alarmRinging && alarmEnabled && timeMatchesAlarmNow()) {
        startAlarmRinging(VIEW_CALENDAR);
      }

      maybeHourlyChirp();
      serviceTimerCountdown();
    }

    delay(1);
    return;
  }

  // COMPASS VIEW
  if (inCompass) {
    if (
      btnWasShortPressed(b1) ||
      btnWasShortPressed(b2) ||
      btnWasShortPressed(b3)
    ) {
      inCompass = false;
      inMenu = true;
      uiDirty = true;
      lastSecondDrawn = 255;
      menuEnableButtonsAt = millis() + 120;
      redrawMenuNow();
      delay(1);
      return;
    }

    unsigned long now = millis();
    if (now - lastTickMs >= 1000UL) {
      unsigned long secs = (now - lastTickMs) / 1000UL;
      for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
      lastTickMs += secs * 1000UL;

      if (!alarmRinging && alarmEnabled && timeMatchesAlarmNow()) {
        startAlarmRinging(VIEW_WATCH);
      }

      maybeHourlyChirp();
      serviceTimerCountdown();
    }

    drawCompass();
    delay(1);
    return;
  }

  // CAMERA VIEW
  if (inCamera) {
    updateCameraApp();

    unsigned long now = millis();
    if (now - lastTickMs >= 1000UL) {
      unsigned long secs = (now - lastTickMs) / 1000UL;
      for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
      lastTickMs += secs * 1000UL;

      if (!alarmRinging && alarmEnabled && timeMatchesAlarmNow()) {
        startAlarmRinging(VIEW_WATCH);
      }

      maybeHourlyChirp();
      serviceTimerCountdown();
    }

    delay(1);
    return;
  }

  // MICROPHONE VIEW
  if (inMic) {
    updateMicApp();
    unsigned long now = millis();
    if (now - lastTickMs >= 1000UL) {
      unsigned long secs = (now - lastTickMs) / 1000UL;
      for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
      lastTickMs += secs * 1000UL;
      if (!alarmRinging && alarmEnabled && timeMatchesAlarmNow()) startAlarmRinging(VIEW_WATCH);
      maybeHourlyChirp();
      serviceTimerCountdown();
    }
    delay(1);
    return;
  }

  // TONE VIEW
  if (inTone) {
    if (btnWasShortPressed(b2)) {
      if (tonePlaying) toneStop();
      else toneStart();
      redrawToneNow();
    }

    if (btnWasShortPressed(bRight)) {
      if (toneIndex < (TONE_COUNT - 1)) toneIndex++;
      if (tonePlaying) toneStart();
      redrawToneNow();
    }

    if (btnWasShortPressed(bLeft)) {
      if (toneIndex > 0) toneIndex--;
      if (tonePlaying) toneStart();
      redrawToneNow();
    }

    unsigned long nowT = millis();

    if (btnWasLongPressed(bRight)) {
      if (toneIndex < (TONE_COUNT - 1)) toneIndex++;
      if (tonePlaying) toneStart();
      redrawToneNow();
      nextToneRepeatR = nowT + TONE_REPEAT_MS;
    }

    if (bRight.stable == LOW && bRight.longFired) {
      if (nextToneRepeatR && nowT >= nextToneRepeatR) {
        if (toneIndex < (TONE_COUNT - 1)) toneIndex++;
        if (tonePlaying) toneStart();
        redrawToneNow();
        nextToneRepeatR += TONE_REPEAT_MS;
      }
    } else {
      nextToneRepeatR = 0;
    }

    if (btnWasLongPressed(bLeft)) {
      if (toneIndex > 0) toneIndex--;
      if (tonePlaying) toneStart();
      redrawToneNow();
      nextToneRepeatL = nowT + TONE_REPEAT_MS;
    }

    if (bLeft.stable == LOW && bLeft.longFired) {
      if (nextToneRepeatL && nowT >= nextToneRepeatL) {
        if (toneIndex > 0) toneIndex--;
        if (tonePlaying) toneStart();
        redrawToneNow();
        nextToneRepeatL += TONE_REPEAT_MS;
      }
    } else {
      nextToneRepeatL = 0;
    }

    if (btnWasShortPressed(b3)) {
      toneStop();
      inTone = false;
      inMenu = true;
      uiDirty = true;
      lastSecondDrawn = 255;
      menuEnableButtonsAt = millis() + 120;
      redrawMenuNow();
      delay(1);
      return;
    }

    unsigned long nowT2 = millis();
    if (nowT2 - lastTickMs >= 1000UL) {
      unsigned long secs = (nowT2 - lastTickMs) / 1000UL;
      for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
      lastTickMs += secs * 1000UL;
      maybeHourlyChirp();
      serviceTimerCountdown();
    }

    delay(1);
    return;
  }

  // RADIO VIEW
  if (inRadio) {
    radioNavSkipService();

    // ---- Encoder hold 2s → toggle shuffle ----
    {
      bool held = encSwitchIsHeld();
      unsigned long nowH = millis();
      if (held && !encSwHoldArmed && !encSwHoldConsumed) {
        encSwHoldArmed   = true;
        encSwHoldStartMs = nowH;
      }
      if (!held) {
        encSwHoldArmed    = false;
        encSwHoldConsumed = false;
        encSwHoldStartMs  = 0;
      }
      if (encSwHoldArmed && !encSwHoldConsumed &&
          (nowH - encSwHoldStartMs) >= ENC_HOLD_SHUFFLE_MS) {
        encSwHoldConsumed = true;
        radioToggleShuffle();
        uiDirty = true;
        redrawRadioNow();
      }
    }

    // Continuous UI animation (marquee + wave)
    radioUiService();
    

    // Center button (BTN2) toggles play/pause (or starts if not active)
    if (btnWasShortPressed(b2)) {
      radioTogglePausePlay(true);
      redrawRadioNow();
      delay(1);
      return;
    }
        // BTN1 = STAT (next station / playlist)
    if (btnWasShortPressed(b1)) {
      radioToggleStation();   // toggles STAT1 <-> STAT2

      delay(1);
      return;
    }

    // EXIT back to menu (BTN3)
    if (btnWasShortPressed(b3)) {
      inRadio = false;
      inMenu = true;
      uiDirty = true;
      menuEnableButtonsAt = millis() + 120;
      redrawMenuNow();
      delay(1);
      return;
    }

    // Keep playback running + handle auto-advance
    radioService();

    // Tick clock
    unsigned long nowR = millis();
    if (nowR - lastTickMs >= 1000UL) {
      unsigned long secs = (nowR - lastTickMs) / 1000UL;
      for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
      lastTickMs += secs * 1000UL;
      maybeHourlyChirp();
      serviceTimerCountdown();
    }

    // Redraw when track changes (skip/auto-finish)
    if (uiDirty) {
    redrawRadioNow();
  }

    delay(1);
    return;
  }
  // RECORDINGS VIEW
  if (inRecordings) {
  recordingsService();  // handles all buttons, DFPlayer events, animation, UI

  // Tick clock in background
  unsigned long nowRec = millis();
  if (nowRec - lastTickMs >= 1000UL) {
    unsigned long secs = (nowRec - lastTickMs) / 1000UL;
    for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
    lastTickMs += secs * 1000UL;
    maybeHourlyChirp();
    serviceTimerCountdown();
  }

  delay(1);
  return;
}
  //DISTANCE VIEW//
  if (inDistance) {
  handleDistance();
  delay(1);
  return;
  }

  if (inLocation) {
    handleLocation();
    delay(1);
    return;
  }

  if (inNavigation) {
    handleNavigation();
    delay(1);
    return;
  }

  if (inGame) {
    // ── Games submenu ─────────────────────────────────────────────────────
    if (inGamesMenu) {
      if (btnWasShortPressed(bUp)) {
        if (gamesMenuCursor > 0) { gamesMenuCursor--; drawGamesMenu(); }
      }
      if (btnWasShortPressed(bDown)) {
        if (gamesMenuCursor < 4) { gamesMenuCursor++; drawGamesMenu(); }
      }
      if (btnWasShortPressed(b2)) {
        // Launch selected game
        inGamesMenu = false;
        gameplayRunning = true;  // game is now actively running
        if (gamesMenuCursor == 0) {
          ironTidesEnter();
        } else if (gamesMenuCursor == 1) {
          blackjackEnter();
        } else if (gamesMenuCursor == 2) {
          obscurusEnter();
        } else if (gamesMenuCursor == 3) {
          racingEnter();                                                    
        } else {
          slotsEnter();
        }
      }
      if (millis() >= menuEnableButtonsAt && btnWasShortPressed(b3)) {
        // Back to main menu
        inGame = false;
        inGamesMenu = false;
        inMenu = true;
        uiDirty = true;
        menuEnableButtonsAt = millis() + 120;
        redrawMenuNow();
      }
      delay(1);
      return;
    }

    // ── Running a game ────────────────────────────────────────────────────
    if (ironTidesIsActive()) {
      ironTidesUpdate();
    } else if (blackjackIsActive()) {
      blackjackUpdate();
    } else if (obscurusIsActive()) {
      obscurusUpdate();
    } else if (racingIsActive()) {
      racingUpdate();
    } else if (slotsIsActive()) {
      slotsUpdate();
    }
    // Re-arm gameplayRunning after BTN1 resumes from pause:
    // once the game is active again and encoder is released, next press pauses.
    if (!gameplayRunning &&
        (ironTidesIsActive() || blackjackIsActive() || obscurusIsActive() || racingIsActive() || slotsIsActive()) &&
        (!g_mcp_ok || (g_mcp_gpio & (1u << 7))))
      gameplayRunning = true;
    
    // If a game is still active after its update, and the encoder is not
    // currently being pressed, restore gameplayRunning so the NEXT encoder
    // press is treated as a pause (not a radio toggle). This handles the
    // case where BTN1 resumed the game from the pause menu.
    if ((ironTidesIsActive() || blackjackIsActive() || obscurusIsActive() || racingIsActive() || slotsIsActive()) &&
        (!g_mcp_ok || (g_mcp_gpio & (1u << 7))))
      gameplayRunning = true;
    
    delay(1);
    return;
  }

  // ---------------- SKY HANDLING ----------------
  if (inSky) {
    if (btnWasShortPressed(b3)) {
      inSky = false;
      skyExit();
      inMenu = true;
      uiDirty = true;
      redrawMenuNow();
      delay(1);
      return;
    }
    skyUpdate();
    delay(1);
    return;
  }


// ---------------- METRONOME HANDLING ----------------
if (inMetronome) {
  unsigned long now = millis();

  // EXIT (BTN3 short)
  if (btnWasShortPressed(b3)) {
    inMetronome = false;
    metPlaying = false;
    metronomeStopClickNow();

    inMenu = true;
    redrawMenuNow();
    delay(1);
    return;
  }

  // PLAY / PAUSE on BTN2 (center button)
  if (btnWasShortPressed(b2)) {
    metronomeTogglePlayPause();
    redrawMetronomeNow();
  }

  // ---------------- NAV LEFT = DECREASE BPM ----------------
  if (btnWasShortPressed(bLeft)) {
    metBpm--;
    if (metBpm < MET_BPM_MIN) metBpm = MET_BPM_MIN;
    redrawMetronomeNow();
  }

  if (btnWasLongPressed(bLeft)) {
    metNextRepeatDec = now + MET_REPEAT_MS;
  }

  if (bLeft.stable == LOW && bLeft.longFired) {
    if (metNextRepeatDec && now >= metNextRepeatDec) {
      metBpm--;
      if (metBpm < MET_BPM_MIN) metBpm = MET_BPM_MIN;
      metNextRepeatDec = now + MET_REPEAT_MS;
      redrawMetronomeNow();
    }
  } else {
    metNextRepeatDec = 0;
  }

  // ---------------- NAV RIGHT = INCREASE BPM ----------------
  if (btnWasShortPressed(bRight)) {
    metBpm++;
    if (metBpm > MET_BPM_MAX) metBpm = MET_BPM_MAX;
    redrawMetronomeNow();
  }

  if (btnWasLongPressed(bRight)) {
    metNextRepeatInc = now + MET_REPEAT_MS;
  }

  if (bRight.stable == LOW && bRight.longFired) {
    if (metNextRepeatInc && now >= metNextRepeatInc) {
      metBpm++;
      if (metBpm > MET_BPM_MAX) metBpm = MET_BPM_MAX;
      metNextRepeatInc = now + MET_REPEAT_MS;
      redrawMetronomeNow();
    }
  } else {
    metNextRepeatInc = 0;
  }

  // ---------------- METRONOME TICK ENGINE ----------------
  if (metPlaying) {
    if (metNextTickMs == 0) metNextTickMs = now;

    if ((long)(now - metNextTickMs) >= 0) {
      metronomeClickNow();
      metNextTickMs += metIntervalMsFromBpm(metBpm);
    }
  }

  // Stop short click tone if needed
  if (metBeepOffMs && (long)(now - metBeepOffMs) >= 0) {
    metronomeStopClickNow();
  }

  delay(1);
  return;
}

  // CALCULATOR VIEW
  if (inCalculator) {
    bool moved = false;

    if (btnWasShortPressed(bUp)) {
      if (calcCursorRow > 0) calcCursorRow--;
      moved = true;
    }

    if (btnWasShortPressed(bDown)) {
      if (calcCursorRow < KP_ROWS - 1) calcCursorRow++;
      moved = true;
    }

    if (btnWasShortPressed(bLeft)) {
      if (calcCursorCol > 0) calcCursorCol--;
      moved = true;
    }

    if (btnWasShortPressed(bRight)) {
      if (calcCursorCol < KP_COLS - 1) calcCursorCol++;
      moved = true;
    }

    if (moved) redrawCalculatorNow();

    if (btnWasShortPressed(b2)) {
      const char* key = CALC_KEYS[calcCursorRow][calcCursorCol];
      String k = String(key);
      k.trim();

      if (k == "DEL") {
        if (calcExpr.length() > 0)
          calcExpr.remove(calcExpr.length() - 1);
        calcResultStr = "";
        calcErrStr = "";
        redrawCalculatorNow();
      } else if (k == "AC") {
        calcExpr = "";
        calcResultStr = "";
        calcErrStr = "";
        redrawCalculatorNow();
      } else if (k == "EXIT") {
        calcExpr = "";
        calcResultStr = "";
        calcErrStr = "";
        calcHasAns = false;
        calcAnsValue = 0.0;
        inCalculator = false;
        inMenu = true;
        uiDirty = true;
        menuEnableButtonsAt = millis() + 120;
        redrawMenuNow();
        delay(1);
        return;
      } else if (k == "=") {
        std::vector<Tok> toks, rpn;
        double val = 0.0;
        String err;

        bool okTok = calcTokenize(
          calcExpr, toks, err, calcAnsValue, calcHasAns
        );
        if (okTok) okTok = calcToRPN(toks, rpn, err);

        bool ok = okTok && calcEvalRPN(rpn, val, err);

        if (ok) {
          calcErrStr = "";
          calcResultStr = calcFormat(val);
          calcHasAns = true;
          calcAnsValue = val;
        } else {
          calcResultStr = "";
          calcErrStr = err.length() ? err : "Syntax error";
        }

        redrawCalculatorNow();
      } else if (k == "SQRT") {
        calcExpr += "SQRT";
        calcResultStr = "";
        calcErrStr = "";
        redrawCalculatorNow();
      } else if (k == "Ans") {
        calcExpr += "Ans";
        calcResultStr = "";
        calcErrStr = "";
        redrawCalculatorNow();
      } else {
        if (k == "x") k = "*";
        if (k == "/ ") k = "/";
        calcExpr += k;
        calcResultStr = "";
        calcErrStr = "";
        redrawCalculatorNow();
      }
    }

    if (btnWasShortPressed(b3)) {
      calcExpr = "";
      calcResultStr = "";
      calcErrStr = "";
      calcHasAns = false;
      calcAnsValue = 0.0;
      inCalculator = false;
      inMenu = true;
      uiDirty = true;
      menuEnableButtonsAt = millis() + 120;
      redrawMenuNow();
      delay(1);
      return;
    }

    unsigned long nowC2 = millis();
    if (nowC2 - lastTickMs >= 1000UL) {
      unsigned long secs = (nowC2 - lastTickMs) / 1000UL;
      for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
      lastTickMs += secs * 1000UL;

      if (!alarmRinging && alarmEnabled && timeMatchesAlarmNow()) {
        startAlarmRinging(VIEW_CALC);
      }

      maybeHourlyChirp();
      serviceTimerCountdown();
    }

    delay(1);
    return;
  }

  // ALARM SET VIEW
  if (inAlarmSet) {
    bool moved = false;

    if (btnWasShortPressed(bUp)) {
      alarmMoveCursor(0, -1);
      moved = true;
    }

    if (btnWasShortPressed(bDown)) {
      alarmMoveCursor(0, +1);
      moved = true;
    }

    if (btnWasShortPressed(bLeft)) {
      alarmMoveCursor(-1, 0);
      moved = true;
    }

    if (btnWasShortPressed(bRight)) {
      alarmMoveCursor(+1, 0);
      moved = true;
    }

    if (moved) drawAlarmSetView();

    if (btnWasShortPressed(b1)) {
      alarmAdjust(-1);
      drawAlarmSetView();
    }

    if (btnWasShortPressed(b3)) {
      alarmAdjust(+1);
      drawAlarmSetView();
    }

    unsigned long nowA = millis();
    static unsigned long nextRepeatMinus = 0;
    static unsigned long nextRepeatPlus = 0;

    if (btnWasLongPressed(b1)) {
      alarmAdjust(-1);
      nextRepeatMinus = nowA + REPEAT_MS;
    }

    if (btnWasLongPressed(b3)) {
      alarmAdjust(+1);
      nextRepeatPlus = nowA + REPEAT_MS;
    }

    if (
      b1.stable == LOW &&
      b1.longFired &&
      nextRepeatMinus &&
      nowA >= nextRepeatMinus
    ) {
      alarmAdjust(-1);
      nextRepeatMinus += REPEAT_MS;
      drawAlarmSetView();
    } else if (b1.stable != LOW) {
      nextRepeatMinus = 0;
    }

    if (
      b3.stable == LOW &&
      b3.longFired &&
      nextRepeatPlus &&
      nowA >= nextRepeatPlus
    ) {
      alarmAdjust(+1);
      nextRepeatPlus += REPEAT_MS;
      drawAlarmSetView();
    } else if (b3.stable != LOW) {
      nextRepeatPlus = 0;
    }

    if (btnWasShortPressed(b2)) {
      inAlarmSet = false;
      inClockMenu = true;
      uiDirty = true;
      drawClockSubMenu();
      delay(1);
      return;
    }

    if (nowA - lastTickMs >= 1000UL) {
      unsigned long secs = (nowA - lastTickMs) / 1000UL;
      for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
      lastTickMs += secs * 1000UL;
      maybeHourlyChirp();
    }

    delay(1);
    return;
  }

  // TIMER SET VIEW
  if (inTimerSet) {
    bool moved = false;

    if (btnWasShortPressed(bUp)) {
      timerMoveCursor(0, -1);
      moved = true;
    }
    if (btnWasShortPressed(bDown)) {
      timerMoveCursor(0, +1);
      moved = true;
    }
    if (btnWasShortPressed(bLeft)) {
      timerMoveCursor(-1, 0);
      moved = true;
    }
    if (btnWasShortPressed(bRight)) {
      timerMoveCursor(+1, 0);
      moved = true;
    }
    if (moved) drawTimerSetView();

    // BTN1 = decrement
    if (btnWasShortPressed(b1)) {
      if (timerField == T_STARTSTOP) {
        // START/STOP via b1 too (same as select)
        if (timerRunning) {
          timerRunning = false;
          timerHour = timerSetHour;
          timerMin  = timerSetMin;
          timerSec  = timerSetSec;
        } else {
          if (timerSetHour > 0 || timerSetMin > 0 || timerSetSec > 0) {
            timerRunning = true;
            timerLastTickMs = millis();
          }
        }
      } else {
        timerAdjust(-1);
      }
      drawTimerSetView();
    }

    // BTN3 = increment
    if (btnWasShortPressed(b3)) {
      if (timerField == T_STARTSTOP) {
        if (timerRunning) {
          timerRunning = false;
          timerHour = timerSetHour;
          timerMin  = timerSetMin;
          timerSec  = timerSetSec;
        } else {
          if (timerSetHour > 0 || timerSetMin > 0 || timerSetSec > 0) {
            timerRunning = true;
            timerLastTickMs = millis();
          }
        }
      } else {
        timerAdjust(+1);
      }
      drawTimerSetView();
    }

    // Nav-switch SELECT also toggles START/STOP when on that field
    // Long-press repeat for b1 / b3 on time fields
    unsigned long nowT = millis();
    static unsigned long timerRepeatMinus = 0;
    static unsigned long timerRepeatPlus  = 0;

    if (btnWasLongPressed(b1) && timerField != T_STARTSTOP) {
      timerAdjust(-1);
      timerRepeatMinus = nowT + REPEAT_MS;
    }
    if (btnWasLongPressed(b3) && timerField != T_STARTSTOP) {
      timerAdjust(+1);
      timerRepeatPlus = nowT + REPEAT_MS;
    }

    if (b1.stable == LOW && b1.longFired && timerRepeatMinus && nowT >= timerRepeatMinus) {
      timerAdjust(-1);
      timerRepeatMinus += REPEAT_MS;
      drawTimerSetView();
    } else if (b1.stable != LOW) {
      timerRepeatMinus = 0;
    }

    if (b3.stable == LOW && b3.longFired && timerRepeatPlus && nowT >= timerRepeatPlus) {
      timerAdjust(+1);
      timerRepeatPlus += REPEAT_MS;
      drawTimerSetView();
    } else if (b3.stable != LOW) {
      timerRepeatPlus = 0;
    }

    // BTN2 = EXIT to clock submenu
    if (btnWasShortPressed(b2)) {
      inTimerSet = false;
      inClockMenu = true;
      uiDirty = true;
      drawClockSubMenu();
      delay(1);
      return;
    }

    // Background clock tick
    if (nowT - lastTickMs >= 1000UL) {
      unsigned long secs = (nowT - lastTickMs) / 1000UL;
      for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
      lastTickMs += secs * 1000UL;

      if (!alarmRinging && alarmEnabled && timeMatchesAlarmNow()) {
        startAlarmRinging(VIEW_TIMER);
      }

      maybeHourlyChirp();
    }

    // Timer countdown tick (independent)
    if (timerRunning && nowT - timerLastTickMs >= 1000UL) {
      unsigned long secs = (nowT - timerLastTickMs) / 1000UL;
      timerLastTickMs += secs * 1000UL;
      for (unsigned long i = 0; i < secs; ++i) {
        timerTickDown();
        if (!timerRunning) {
          // Fired — startTimerRinging already called inside timerTickDown
          startTimerRinging(VIEW_TIMER);
          break;
        }
      }
      drawTimerSetView();
    }

    delay(1);
    return;
  }


  if (setMode && btnWasShortPressed(b2)) {
    normalizeDateTime();
    lastTickMs = millis();

    setMode = false;
    inClockMenu = true;
    uiDirty = true;
    lastSecondDrawn = 255;
    drawClockSubMenu();
    delay(1);
    return;
  }

  if (!setMode && btnWasShortPressed(b2)) {
    screenEnabled = !screenEnabled;

    if (screenEnabled) {
      oledOn();
      lastSecondDrawn = 255;
      uiDirty = true;
    } else {
      if (displayReady) {
        display.clearDisplay();
        display.display();
      }
      oledOff();

      nextRepeat1 = nextRepeat3 = 0;
      b1.longFired = b3.longFired = false;
      b1.pressStart = b3.pressStart = 0;
    }
  }

  if (
    !setMode &&
    !inMenu &&
    !inPeriodicTable &&
    !inDataView &&
    !inAlarmSet &&
    !inCalendar &&
    !inCalculator &&
    !inRecordings &&
    !inNavigation 
  ) {
    if (btnWasShortPressed(b3)) {
      alarmEnabled = !alarmEnabled;
      redrawClockNow();
    }
  }

  if (setMode) {
    bool moved = false;

    if (btnWasShortPressed(bUp)) {
      moveSetCursor(0, -1);
      moved = true;
    }

    if (btnWasShortPressed(bDown)) {
      moveSetCursor(0, +1);
      moved = true;
    }

    if (btnWasShortPressed(bLeft)) {
      moveSetCursor(-1, 0);
      moved = true;
    }

    if (btnWasShortPressed(bRight)) {
      moveSetCursor(+1, 0);
      moved = true;
    }

    if (moved) redrawClockNow();

    if (btnWasShortPressed(b1)) adjustField(-1);
    if (btnWasShortPressed(b3)) adjustField(+1);

    unsigned long now = millis();

    if (btnWasLongPressed(b1)) {
      adjustField(-1);
      nextRepeat1 = now + REPEAT_MS;
    }

    if (btnWasLongPressed(b3)) {
      adjustField(+1);
      nextRepeat3 = now + REPEAT_MS;
    }

    if (b1.stable == LOW && b1.longFired) {
      if (nextRepeat1 && now >= nextRepeat1) {
        adjustField(-1);
        nextRepeat1 += REPEAT_MS;
      }
    } else {
      nextRepeat1 = 0;
    }

    if (b3.stable == LOW && b3.longFired) {
      if (nextRepeat3 && now >= nextRepeat3) {
        adjustField(+1);
        nextRepeat3 += REPEAT_MS;
      }
    } else {
      nextRepeat3 = 0;
    }
  } else {
    unsigned long now = millis();
    if (now - lastTickMs >= 1000UL) {
      unsigned long secs = (now - lastTickMs) / 1000UL;
      for (unsigned long i = 0; i < secs; ++i) tickOneSecond();
      lastTickMs += secs * 1000UL;

      if (!alarmRinging && alarmEnabled && timeMatchesAlarmNow()) {
        startAlarmRinging(VIEW_WATCH);
      }

      maybeHourlyChirp();
      serviceTimerCountdown();
    }
  }

  // SCREEN REDRAWS
  if (screenEnabled && displayReady) {
    if (!setMode) {
      if (second != lastSecondDrawn) {
        drawClock();
        lastSecondDrawn = second;
      }
    } else {
      if (uiDirty) {
        drawClock();
        uiDirty = false;
      }
    }
  }

  delay(1);
}