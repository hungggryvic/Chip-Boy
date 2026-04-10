#include "MicrophoneApp.h"
#include "RadioApp.h"
#include <ESP_I2S.h>
#include <SD.h>
#include <SPI.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// MICROPHONE APP IMPLEMENTATION
// ============================================================================

#define MIC_SAMPLE_RATE  16000
#define MIC_SAMPLE_BITS  16
#define MIC_WAV_HDR_SIZE 44
#define MIC_VOL_GAIN     2
#define MIC_MAX_SECONDS  120
#define MIC_CHUNK_BYTES  1024

// Globals from main
extern bool inMenu;
extern bool uiDirty;
extern uint8_t lastSecondDrawn;
extern unsigned long menuEnableButtonsAt;
extern bool screenEnabled;
extern bool displayReady;
extern Adafruit_SSD1306 display;

#define ENC_A_PIN D8
#define ENC_B_PIN D7
extern uint8_t encPrevAB;
extern int8_t encAccum;

struct BtnState;
extern BtnState b2, b3;
bool btnWasShortPressed(const BtnState &b_in);
void redrawMenuNow();
void radioRecoverAfterCamera();

// Sine wave constants come from RadioApp.h
static const int  RADIO_WAVE_BASE_Y  = 38;
static const int  RADIO_WAVE_AMP_PX  = 6;
static const uint8_t RADIO_WAVE_PERIOD = 32;

#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH 128
#endif

// ── WAV header builder ────────────────────────────────────────────────────

static void micBuildWavHeader(uint8_t *h, uint32_t dataBytes) {
  uint32_t fileSize = dataBytes + MIC_WAV_HDR_SIZE - 8;
  uint32_t byteRate = MIC_SAMPLE_RATE * MIC_SAMPLE_BITS / 8;
  const uint8_t hdr[] = {
    'R','I','F','F',
    (uint8_t)fileSize,(uint8_t)(fileSize>>8),(uint8_t)(fileSize>>16),(uint8_t)(fileSize>>24),
    'W','A','V','E','f','m','t',' ',
    0x10,0x00,0x00,0x00, 0x01,0x00, 0x01,0x00,
    (uint8_t)MIC_SAMPLE_RATE,(uint8_t)(MIC_SAMPLE_RATE>>8),
    (uint8_t)(MIC_SAMPLE_RATE>>16),(uint8_t)(MIC_SAMPLE_RATE>>24),
    (uint8_t)byteRate,(uint8_t)(byteRate>>8),
    (uint8_t)(byteRate>>16),(uint8_t)(byteRate>>24),
    0x02,0x00, 0x10,0x00,
    'd','a','t','a',
    (uint8_t)dataBytes,(uint8_t)(dataBytes>>8),
    (uint8_t)(dataBytes>>16),(uint8_t)(dataBytes>>24),
  };
  memcpy(h, hdr, MIC_WAV_HDR_SIZE);
}

// ── Module globals ────────────────────────────────────────────────────────

bool inMic = false;

static I2SClass  micI2S;
static bool      micRecording    = false;
static bool      micSdOk         = false;
static bool      micI2sOk        = false;
static int       micTrackCount   = 1;
static bool      micPausedRadio  = false;
static String    micStatusMsg    = "";
static uint8_t*  micBuf          = NULL;
static uint32_t  micBufSize      = 0;
static uint32_t  micBufUsed      = 0;
static unsigned long micRecStartMs = 0;
static uint8_t   micWavePhase    = 0;
static unsigned long micWaveLastMs = 0;
static const unsigned long MIC_WAVE_STEP_MS = 45;

// ── SD helpers ────────────────────────────────────────────────────────────

static int micFindNextTrack() {
  if (!micSdOk) return 1;
  File root = SD.open("/00");
  if (!root || !root.isDirectory()) return 1;
  int highest = 0;
  File f = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      const char* nm = f.name();
      if (strlen(nm) >= 3) { int n = atoi(nm); if (n > highest) highest = n; }
    }
    f = root.openNextFile();
  }
  return highest + 1;
}

// ── Draw ──────────────────────────────────────────────────────────────────

static void micDrawScreen() {
  if (!displayReady) return;
  display.clearDisplay();
  display.setTextWrap(false);
  display.setTextSize(1);
  display.setTextColor(WHITE);

  if (micRecording) {
    display.setCursor(0, 0); display.print("RECORDING");
    uint32_t elapsed = (millis() - micRecStartMs) / 1000;
    char tbuf[8]; sprintf(tbuf, "%lus", elapsed);
    int tw = strlen(tbuf) * 6;
    display.setCursor(SCREEN_WIDTH - tw, 0); display.print(tbuf);
  } else if (micStatusMsg.length() > 0) {
    String msg = micStatusMsg;
    if (msg.length() > 21) msg = msg.substring(0, 21);
    display.setCursor(0, 0); display.print(msg);
  }

  int prevY = -1;
  for (int x = 0; x < SCREEN_WIDTH; x++) {
    uint8_t idx = (uint8_t)((x + micWavePhase) & (RADIO_WAVE_PERIOD - 1));
    int y = RADIO_WAVE_BASE_Y + (kSin32[idx] * RADIO_WAVE_AMP_PX) / 127;
    if (y < 0) y = 0; if (y > 52) y = 52;
    if (x > 0 && prevY >= 0) display.drawLine(x - 1, prevY, x, y, WHITE);
    else display.drawPixel(x, y, WHITE);
    prevY = y;
  }

  int iconCx = SCREEN_WIDTH/2, iconCy = 56;
  if (micRecording) {
    display.fillRect(iconCx-5, iconCy-3, 3, 7, WHITE);
    display.fillRect(iconCx+2, iconCy-3, 3, 7, WHITE);
  } else {
    display.fillTriangle(iconCx-4,iconCy-3, iconCx-4,iconCy+3, iconCx+4,iconCy, WHITE);
  }
  display.setCursor(SCREEN_WIDTH-(6*4), 55); display.print("EXIT");
  display.display();
}

// ── Buffer helpers ────────────────────────────────────────────────────────

static bool micAllocBuf() {
  micBufSize = (uint32_t)MIC_SAMPLE_RATE * (MIC_SAMPLE_BITS/8) * MIC_MAX_SECONDS;
  micBuf     = (uint8_t*)ps_malloc(micBufSize);
  micBufUsed = 0;
  return (micBuf != NULL);
}

static void micSaveAndFree() {
  if (!micBuf || micBufUsed == 0) {
    micStatusMsg = "Nothing recorded";
    if (micBuf) { free(micBuf); micBuf = NULL; }
    return;
  }
  for (uint32_t i = 0; i < micBufUsed; i += 2)
    (*(int16_t*)(micBuf + i)) <<= MIC_VOL_GAIN;

  char fn[32];
  sprintf(fn, "/00/%03d.mp3", micTrackCount);
  File f = SD.open(fn, FILE_WRITE);
  if (!f) { free(micBuf); micBuf = NULL; micStatusMsg = "SD open fail"; return; }
  uint8_t hdr[MIC_WAV_HDR_SIZE];
  micBuildWavHeader(hdr, micBufUsed);
  f.write(hdr, MIC_WAV_HDR_SIZE);
  f.write(micBuf, micBufUsed);
  f.close();
  free(micBuf); micBuf = NULL;
  micStatusMsg = "Saved track" + String(micTrackCount);
  micTrackCount++;
}

static void micServiceRecording() {
  if (!micRecording || !micBuf) return;
  if (micBufUsed >= micBufSize) {
    micRecording = false; micSaveAndFree(); micDrawScreen(); return;
  }
  uint32_t space = micBufSize - micBufUsed;
  uint32_t chunk = (space < MIC_CHUNK_BYTES) ? space : MIC_CHUNK_BYTES;
  size_t got = micI2S.readBytes((char*)(micBuf + micBufUsed), chunk);
  if (got > 0) micBufUsed += got;
}

// ── App lifecycle ─────────────────────────────────────────────────────────

void startMicApp() {
  micPausedRadio = false;
  if (radioActive && !radioPaused && radioReady) {
    dfp.pause(); radioPaused = true; micPausedRadio = true;
    radioPersistFromGlobals();
  }
  micRecording = false; micStatusMsg = "";
  micWavePhase = 0; micWaveLastMs = 0;
  micBuf = NULL; micBufUsed = 0; micBufSize = 0;

  if (!micSdOk) micSdOk = SD.begin(21) && (SD.cardType() != CARD_NONE);

  micI2S.setPinsPdmRx(42, 41);
  micI2sOk = micI2S.begin(I2S_MODE_PDM_RX, MIC_SAMPLE_RATE,
                           I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  if (!micI2sOk) micStatusMsg = "Mic not found";
  micDrawScreen();
}

void updateMicApp() {
  if (btnWasShortPressed(b3)) {
    if (micRecording) { micRecording = false; micSaveAndFree(); }
    else if (micBuf)  { free(micBuf); micBuf = NULL; }

    micI2S.end(); micI2sOk = false;
    if (micSdOk) { SD.end(); SPI.end(); micSdOk = false; }

    pinMode(ENC_A_PIN, INPUT_PULLUP);
    pinMode(ENC_B_PIN, INPUT_PULLUP);
    encAccum = 0;
    encPrevAB = (((digitalRead(ENC_A_PIN)==HIGH)?1:0)<<1)|
                ((digitalRead(ENC_B_PIN)==HIGH)?1:0);

    radioRecoverAfterCamera();
    if (micPausedRadio && radioActive) {
      radioPaused = false;
      radioPlayTrack(radioStation, radioTrack);
      radioPersistFromGlobals();
    }
    inMic = false; inMenu = true; uiDirty = true;
    lastSecondDrawn = 255; menuEnableButtonsAt = millis() + 120;
    redrawMenuNow(); return;
  }

  if (btnWasShortPressed(b2)) {
    if (!micRecording) {
      if (!micI2sOk) { micStatusMsg = "Mic not found"; micDrawScreen(); return; }
      if (!micSdOk)  { micSdOk = SD.begin(21)&&(SD.cardType()!=CARD_NONE); }
      if (!micSdOk)  { micStatusMsg = "No SD card";    micDrawScreen(); return; }
      if (!psramFound()) { micStatusMsg = "No PSRAM";  micDrawScreen(); return; }
      if (!micAllocBuf()) { micStatusMsg = "Buf alloc fail"; micDrawScreen(); return; }
      micTrackCount = micFindNextTrack();
      micRecStartMs = millis();
      micRecording  = true;
      micStatusMsg  = "";
    } else {
      micRecording = false;
      micSaveAndFree();
    }
    micDrawScreen(); return;
  }

  micServiceRecording();

  unsigned long now = millis();
  if (micRecording) {
    if (micWaveLastMs == 0) micWaveLastMs = now;
    while (now - micWaveLastMs >= MIC_WAVE_STEP_MS) {
      micWaveLastMs += MIC_WAVE_STEP_MS;
      micWavePhase = (uint8_t)((micWavePhase + 1) & 31);
    }
  } else {
    micWaveLastMs = 0;
  }

  static unsigned long micLastDrawMs = 0;
  if (now - micLastDrawMs >= 50) { micLastDrawMs = now; micDrawScreen(); }
}