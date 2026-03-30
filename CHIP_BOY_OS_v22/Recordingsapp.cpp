// ============================================================================
// RecordingsApp.cpp
// Plays MP3s from folder "00" on the DFPlayer SD card.
// ============================================================================

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <DFRobotDFPlayerMini.h>

#include "RecordingsApp.h"

// ---- Externals already defined in main .ino file ---------------------------
extern Adafruit_SSD1306 display;
extern DFRobotDFPlayerMini dfp;
extern HardwareSerial DFSerial;
extern bool displayReady;
extern bool screenEnabled;
extern bool uiDirty;
extern bool inMenu;
extern int  radioVolume;            // we reuse the same volume knob
extern bool radioReady;             // we reuse the same DFPlayer instance
extern bool radioPaused;            // we reuse the same pause-state flag
extern bool radioActive;            // we reuse the same active flag
extern uint32_t radioIgnoreDfErrorsUntilMs;
extern unsigned long radioLastTrackChangeAt;
extern void radioEnsureInit();
extern void dfplayerHardStopAndFlush();
// shared helpers already in the main file
extern void radioEnsureInit();
extern void dfplayerHardStopAndFlush();
extern void redrawMenuNow();
extern void radioSetVolume(int v);

// Button helpers
struct BtnState;
extern BtnState b1, b2, b3, bLeft, bRight;
extern bool btnWasShortPressed(const BtnState &b);

// OLED constants
static const int16_t REC_SCREEN_WIDTH = 128;

// ---- Module state ----------------------------------------------------------
bool inRecordings = false;

static uint8_t recTrack       = 1;   // current track number (1-based)
static uint8_t recTrackTotal  = 0;   // how many files are in folder 00
static bool    recActive      = false; // true while we intend to be playing
static bool    recPaused      = false; // true while temporarily paused

// Marquee
static int16_t  recMarqueeX         = 0;
static unsigned long recMarqueeLastMs    = 0;
static unsigned long recMarqueeHoldUntil = 0;
static uint8_t  recMarqueePhase     = 0;

static const uint8_t  REC_CHAR_W           = 6;
static const unsigned long REC_MARQUEE_STEP_MS  = 250;
static const int16_t REC_MARQUEE_STEP_PX   = 6;
static const unsigned long REC_MARQUEE_START_HOLD_MS = 900;
static const unsigned long REC_MARQUEE_END_HOLD_MS   = 700;

// Sine-wave animation
static uint8_t  recWavePhase    = 0;
static unsigned long recWaveLastMs   = 0;
static const unsigned long REC_WAVE_STEP_MS  = 45;
static const int REC_WAVE_BASE_Y = 38;
static const int REC_WAVE_AMP_PX = 6;
static const uint8_t REC_WAVE_PERIOD = 32;

static const unsigned long REC_UI_FRAME_MS = 33;
static unsigned long recUiLastFrameMs = 0;

static const uint8_t REC_MAX_EVENTS_PER_LOOP = 8;
static const unsigned long REC_TRACK_GUARD_MS = 600;

// Lookup table: same sine values used by the radio app
static const int8_t kRecSin32[32] = {
   0,  25,  49,  71,  90, 106, 118, 126,
 127, 126, 118, 106,  90,  71,  49,  25,
   0, -25, -49, -71, -90,-106,-118,-126,
-127,-126,-118,-106, -90, -71, -49, -25
};

// Title string built from track number (e.g. "RECORDING 001")
static char recTitleBuf[32];

// ---- Helpers ---------------------------------------------------------------

static void recBuildTitle() {
  snprintf(recTitleBuf, sizeof(recTitleBuf), "RECORDING %03u", (unsigned)recTrack);
}

static void recResetMarquee() {
  recMarqueeX         = 0;
  recMarqueeLastMs    = 0;
  recMarqueeHoldUntil = 0;
  recMarqueePhase     = 0;
}

static inline uint8_t recNextTrack(uint8_t t) {
  if (recTrackTotal == 0) return 1;
  return (t < recTrackTotal) ? (t + 1) : 1;
}

static inline uint8_t recPrevTrack(uint8_t t) {
  if (recTrackTotal == 0) return 1;
  return (t > 1) ? (t - 1) : recTrackTotal;
}

// Ask DFPlayer how many files are in folder 00
static void recQueryTotal() {
  if (!radioReady) return;
  delay(20);
  int n = dfp.readFileCountsInFolder(0); // folder index 0 = "00"
  if (n > 0 && n <= 255) {
    recTrackTotal = (uint8_t)n;
  } else {
    recTrackTotal = 99; // safe fallback — DFPlayer will just skip missing files
  }
}

static void recPlayTrack(uint8_t track) {
  if (!radioReady) {
    radioEnsureInit();
    if (!radioReady) return;
  }

  dfp.stop();
  delay(30);
  dfplayerHardStopAndFlush();
  delay(20);

  dfp.playFolder(0, track);   // folder "00", track number

  radioLastTrackChangeAt = millis();
  radioIgnoreDfErrorsUntilMs = millis() + 2500;

  recPaused  = false;
  recActive  = true;

  // Keep the shared flags consistent so volume still works
  radioActive = true;
  radioPaused = false;
}

// ---- Drawing ---------------------------------------------------------------

static void recDrawSineWave() {
  for (int x = 0; x < REC_SCREEN_WIDTH; x++) {
    uint8_t idx = (uint8_t)((x + recWavePhase) & (REC_WAVE_PERIOD - 1));
    int y = REC_WAVE_BASE_Y + (kRecSin32[idx] * REC_WAVE_AMP_PX) / 127;
    if (y < 0)  y = 0;
    if (y > 52) y = 52;
    display.drawPixel(x, y, SSD1306_WHITE);
  }
}

static void recDrawNowPlaying() {
  if (!displayReady) return;

  display.clearDisplay();
  display.setTextWrap(false);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // ---------- TITLE (marquee if too long) ----------
  const int16_t titleY = 0;
  int16_t titleW = (int16_t)strlen(recTitleBuf) * REC_CHAR_W;

  if (titleW <= REC_SCREEN_WIDTH) {
    display.setCursor(0, titleY);
    display.print(recTitleBuf);
  } else {
    display.setCursor(recMarqueeX, titleY);
    display.print(recTitleBuf);
  }

  // ---------- SINE WAVE ----------
  recDrawSineWave();

  // ---------- Controls (same layout as radio) ----------
  int iconCx = REC_SCREEN_WIDTH / 2;
  int iconCy = 56;

  display.setTextSize(1);
  const int ARROW_OFFSET = 14;
  const int CHAR_W = 6;

  display.setCursor(iconCx - ARROW_OFFSET - CHAR_W / 2, iconCy - 3);
  display.print("<");

  display.setCursor(iconCx + ARROW_OFFSET - CHAR_W / 2, iconCy - 3);
  display.print(">");

  if (recActive && !recPaused) {
    // Pause icon (two bars)
    display.fillRect(iconCx - 5, iconCy - 3, 3, 7, SSD1306_WHITE);
    display.fillRect(iconCx + 2, iconCy - 3, 3, 7, SSD1306_WHITE);
  } else {
    // Play icon (triangle)
    display.fillTriangle(iconCx - 4, iconCy - 3,
                         iconCx - 4, iconCy + 3,
                         iconCx + 4, iconCy,
                         SSD1306_WHITE);
  }

  // EXIT bottom-right
  display.setCursor(REC_SCREEN_WIDTH - (6 * 4), 55);
  display.print("EXIT");

  display.display();
}

void redrawRecordingsNow() {
  if (screenEnabled && displayReady) {
    recDrawNowPlaying();
    uiDirty = false;
  }
}

// ---- UI service (marquee + wave animation) ---------------------------------

static void recUiService() {
  if (!inRecordings || !displayReady || !screenEnabled) return;

  unsigned long now = millis();
  if (now - recUiLastFrameMs < REC_UI_FRAME_MS) return;
  recUiLastFrameMs = now;

  bool changed = false;

  // --- Marquee ---
  int16_t titleW = (int16_t)strlen(recTitleBuf) * (int16_t)REC_CHAR_W;

  if (titleW > REC_SCREEN_WIDTH) {
    const int16_t minX = (int16_t)REC_SCREEN_WIDTH - titleW;

    if (recMarqueePhase == 0) {
      recMarqueeX = 0;
      recMarqueeLastMs = now;
      recMarqueeHoldUntil = now + REC_MARQUEE_START_HOLD_MS;
      recMarqueePhase = 1;
      changed = true;
    }

    if (recMarqueePhase == 1) {
      if (now >= recMarqueeHoldUntil) {
        recMarqueePhase = 2;
        recMarqueeLastMs = now;
      }
    }

    if (recMarqueePhase == 2) {
      while (now - recMarqueeLastMs >= REC_MARQUEE_STEP_MS) {
        recMarqueeLastMs += REC_MARQUEE_STEP_MS;
        if (recMarqueeX > minX) {
          recMarqueeX -= REC_MARQUEE_STEP_PX;
          if (recMarqueeX < minX) recMarqueeX = minX;
          changed = true;
          if (recMarqueeX == minX) {
            recMarqueeHoldUntil = now + REC_MARQUEE_END_HOLD_MS;
            recMarqueePhase = 3;
            break;
          }
        }
      }
    }

    if (recMarqueePhase == 3) {
      if (now >= recMarqueeHoldUntil) {
        recMarqueeX = 0;
        recMarqueeHoldUntil = now + REC_MARQUEE_START_HOLD_MS;
        recMarqueePhase = 1;
        recMarqueeLastMs = now;
        changed = true;
      }
    }
  } else {
    if (recMarqueeX != 0 || recMarqueePhase != 0) {
      recMarqueeX = 0; recMarqueeLastMs = 0;
      recMarqueeHoldUntil = 0; recMarqueePhase = 0;
      changed = true;
    }
  }

  // --- Wave (only while actually playing) ---
  if (recActive && !recPaused) {
    if (recWaveLastMs == 0) {
      recWaveLastMs = now;
    } else {
      while (now - recWaveLastMs >= REC_WAVE_STEP_MS) {
        recWaveLastMs += REC_WAVE_STEP_MS;
        recWavePhase = (uint8_t)((recWavePhase + 1) & (REC_WAVE_PERIOD - 1));
        changed = true;
      }
    }
  } else {
    recWaveLastMs = 0;
  }

  if (changed) uiDirty = true;
}

// ---- Toggle play / pause ---------------------------------------------------

static void recTogglePausePlay() {
  if (!radioReady) {
    radioEnsureInit();
    if (!radioReady) return;
  }

  if (!recActive) {
    // First press — start playing
    recActive  = true;
    recPaused  = false;
    radioActive = true;
    radioPaused = false;
    recPlayTrack(recTrack);
    uiDirty = true;
    redrawRecordingsNow();
    return;
  }

  if (!recPaused) {
    dfp.pause();
    recPaused  = true;
    radioPaused = true;
  } else {
    dfp.start();
    recPaused  = false;
    radioPaused = false;
  }

  uiDirty = true;
  redrawRecordingsNow();
}

// ---- Skip next / prev ------------------------------------------------------

static void recSkipNext() {
  recTrack = recNextTrack(recTrack);
  recResetMarquee();
  recBuildTitle();

  if (recActive) {
    recPaused  = false;
    radioPaused = false;
    recPlayTrack(recTrack);
  }

  uiDirty = true;
  redrawRecordingsNow();
}

static void recSkipPrev() {
  recTrack = recPrevTrack(recTrack);
  recResetMarquee();
  recBuildTitle();

  if (recActive) {
    recPaused  = false;
    radioPaused = false;
    recPlayTrack(recTrack);
  }

  uiDirty = true;
  redrawRecordingsNow();
}

// ---- Encoder-switch gestures (called from main loop) -----------------------
void recordingsEncToggle()   { if (inRecordings) recTogglePausePlay(); }
void recordingsEncNext()     { if (inRecordings) recSkipNext(); }
void recordingsEncPrev()     { if (inRecordings) recSkipPrev(); }

// ---- Public: enter the app -------------------------------------------------

void recordingsEnter() {
  // Make sure DFPlayer is up
  radioEnsureInit();

  // Stop whatever was playing (radio, etc.)
  if (radioReady) {
    dfp.stop();
    delay(50);
    dfplayerHardStopAndFlush();
  }

  // Reset state
  recTrack   = 1;
  recActive  = false;
  recPaused  = false;
  recResetMarquee();
  recWavePhase  = 0;
  recWaveLastMs = 0;

  // We take over the shared flags — radio is effectively "off" while here
  radioActive = false;
  radioPaused = false;

  // Build title and query folder size
  recBuildTitle();
  recQueryTotal();

  inRecordings = true;
  inMenu       = false;
  uiDirty      = true;

  redrawRecordingsNow();
}

// ---- Public: service (call every loop while inRecordings == true) ----------

void recordingsService() {
  if (!inRecordings) return;

  // ---- Nav buttons (bLeft / bRight = prev/next, same as radio) ----
  if (btnWasShortPressed(bRight)) { recSkipNext(); delay(1); return; }
  if (btnWasShortPressed(bLeft))  { recSkipPrev(); delay(1); return; }

  // ---- BTN2 = play/pause ----
  if (btnWasShortPressed(b2)) {
    recTogglePausePlay();
    delay(1);
    return;
  }

  // ---- BTN1 = prev (same as radio's prev button) ----
  if (btnWasShortPressed(b1)) {
    recSkipPrev();
    delay(1);
    return;
  }

  // ---- BTN3 = EXIT → stop playback and return to menu ----
  if (btnWasShortPressed(b3)) {
    // Hard stop — this is the "tape deck" eject moment
    if (radioReady) {
      dfp.stop();
      delay(30);
      dfplayerHardStopAndFlush();
    }
    recActive   = false;
    recPaused   = false;
    radioActive = false;
    radioPaused = false;
    inRecordings = false;
    inMenu       = true;
    uiDirty      = true;
    redrawMenuNow();
    delay(1);
    return;
  }

  // ---- DFPlayer event loop: auto-advance on track finish ----
  if (radioReady) {
    uint8_t processed = 0;
    unsigned long now = millis();

    while (dfp.available() && processed < REC_MAX_EVENTS_PER_LOOP) {
      processed++;
      uint8_t type = dfp.readType();
      /*int value  =*/ dfp.read();  // consume value

      if (type == DFPlayerError) {
        if (millis() < radioIgnoreDfErrorsUntilMs) continue;
        if (millis() - radioLastTrackChangeAt < 1500) continue;
        radioReady = false;
        dfplayerHardStopAndFlush();
        break;
      }

      if (type == DFPlayerPlayFinished) {
        if (!recActive || recPaused) continue;
        if (now - radioLastTrackChangeAt < REC_TRACK_GUARD_MS) continue;

        recTrack = recNextTrack(recTrack);
        recResetMarquee();
        recBuildTitle();
        recPlayTrack(recTrack);
        uiDirty = true;
      }
    }
  }

  // ---- UI animation ----
  recUiService();

  if (uiDirty) redrawRecordingsNow();
}