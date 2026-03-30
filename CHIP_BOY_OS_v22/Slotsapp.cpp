// ==============================
// SLOTS — CHIP-BOY PORT
// Display: SSD1306 (I2C, addr 0x3C)
// Buttons: MCP23008 (BTN1/2/3 + nav switch)
// Pause:   Encoder switch D6 (same as other games)
//
// Controls:
//   BTN1  (GP0) = decrease bet
//   BTN2  (GP6) = confirm bank entry / SPIN
//   BTN3  (GP1) = increase bet
//   ENC SW      = PAUSE
//
// Flow:
//   Splash  → "Enter bank amount" with - PLAY + HUD, BTN2 confirms
//   Idle    → three reels shown, SPIN label, BTN2 spins
//   Spin    → reels animate left-to-right then stop on random symbols
//   Result  → WIN or LOSE shown, bank updated; bottom becomes - SPIN +
//             BTN2 spins again at current bet
//
// Symbols: 5 custom 9×9 sprites (edit freely)
// ==============================

#include "SlotsApp.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// OS APP WRAPPER STATE
// ============================================================================
static bool sl_active   = false;
static bool sl_setupRan = false;

bool slotsIsActive() { return sl_active; }

extern void gameExitToMenu();

static void slotsSetup_Internal();
static void slotsLoop_Internal();

// ============================================================================
// SHARED HARDWARE
// ============================================================================
#define SL_SCREEN_W  128
#define SL_SCREEN_H   64

extern Adafruit_SSD1306 display;
extern bool displayReady;

// ============================================================================
// MCP23008
// ============================================================================
#define SL_MCP_ADDR  0x20
#define SL_MCP_IODIR 0x00
#define SL_MCP_GPIO  0x09
#define SL_MCP_GPPU  0x06

static uint8_t sl_mcp_gpio = 0xFF;

static bool slMcpWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(SL_MCP_ADDR);
  Wire.write(reg); Wire.write(val);
  return Wire.endTransmission() == 0;
}
static bool slMcpRead(uint8_t &val) {
  Wire.beginTransmission(SL_MCP_ADDR);
  Wire.write(SL_MCP_GPIO);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)SL_MCP_ADDR, 1) != 1) return false;
  val = Wire.read();
  return true;
}

// ============================================================================
// BUTTON DEBOUNCE
// ============================================================================
struct SlBtnState {
  uint8_t pin;
  bool lastRead    = HIGH;
  bool stable      = HIGH;
  unsigned long lastChange = 0;
};

static const unsigned long SL_DEBOUNCE_MS = 25;

static inline bool slMcpBit(uint8_t bit) {
  return (sl_mcp_gpio & (1u << bit)) ? HIGH : LOW;
}
static void slBtnUpdate(SlBtnState &b) {
  bool r = slMcpBit(b.pin);
  if (r != b.lastRead) { b.lastRead = r; b.lastChange = millis(); }
  if ((millis() - b.lastChange) >= SL_DEBOUNCE_MS) b.stable = b.lastRead;
}
static inline bool slBtnDown(const SlBtnState &b) { return b.stable == LOW; }

static SlBtnState sl_b1 { .pin = 0 };
static SlBtnState sl_b2 { .pin = 6 };
static SlBtnState sl_b3 { .pin = 1 };

static bool sl_b1Prev = HIGH;
static bool sl_b2Prev = HIGH;
static bool sl_b3Prev = HIGH;

static inline bool sl_justPressed(SlBtnState &b, bool &prev) {
  bool cur  = b.stable;
  bool edge = (prev == HIGH && cur == LOW);
  prev = cur;
  return edge;
}

// ============================================================================
// ENCODER / BUZZER PINS
// ============================================================================
//#ifndef ENC_SW_PIN
//#define ENC_SW_PIN D6
//#endif
#ifndef BUZZER_PIN
#define BUZZER_PIN D2
#endif

// ============================================================================
// PAUSE STATE
// ============================================================================
static bool sl_paused             = false;
static bool sl_pauseNeedsRelease  = false;
static unsigned long sl_pauseIgnoreUntil = 0;
static bool sl_pauseBtn1WasDown   = false;
static bool sl_pauseBtn3WasDown   = false;

// ============================================================================
// BETTING
// ============================================================================
// Bet denominations (max $100)
static const int SL_BET_DENOMS[]   = { 1, 2, 5, 10, 20, 50, 100 };
static const int SL_NUM_BET_DENOMS = 7;
static int  sl_betIndex            = 0;

static inline int slCurrentBet() { return SL_BET_DENOMS[sl_betIndex]; }

static void slBetDecrease() {
  if (sl_betIndex > 0) { sl_betIndex--; tone(BUZZER_PIN, 600, 25); }
}
static void slBetIncrease() {
  if (sl_betIndex < SL_NUM_BET_DENOMS - 1) { sl_betIndex++; tone(BUZZER_PIN, 900, 25); }
}

// Bank entry denominations (up to $500)
static const int SL_BANK_DENOMS[]   = { 1, 2, 5, 10, 20, 50, 100, 200, 300, 400, 500 };
static const int SL_NUM_BANK_DENOMS = 11;
static int  sl_bankIndex            = 6;   // default $100
static long sl_bank                 = 0;   // actual bank value; persists until power-off
static long sl_startBank            = 0;   // bank value at session start (set on splash confirm)

static inline int slCurrentBankEntry() { return SL_BANK_DENOMS[sl_bankIndex]; }

static void slBankDecrease() {
  if (sl_bankIndex > 0) { sl_bankIndex--; tone(BUZZER_PIN, 600, 25); }
}
static void slBankIncrease() {
  if (sl_bankIndex < SL_NUM_BANK_DENOMS - 1) { sl_bankIndex++; tone(BUZZER_PIN, 900, 25); }
}

// ============================================================================
// SLOT SYMBOLS  — 7 custom 18×18 sprites  (edit the 0s and 1s freely)
//   Row 0 = top, Row 17 = bottom.  Col 0 = left, Col 17 = right.
//   Symbols 0-4 are regular (win 1× bet on triple).
//   Symbol 5 = 2× multiplier (win 2× bet on triple).
//   Symbol 6 = 3× multiplier (win 3× bet on triple).
// ============================================================================
#define SL_SYM_W    18
#define SL_SYM_H    18
#define SL_NUM_SYMS  7
#define SL_SYM_2X    5   // index of the 2× multiplier symbol
#define SL_SYM_3X    6   // index of the 3× multiplier symbol

// Symbol 0 
static const uint8_t sl_sym0[18][18] PROGMEM = {
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0 },
  { 0,0,0,1,0,1,0,1,0,1,0,1,0,1,1,0,0,0 },
  { 0,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,0,0 },
  { 0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0 },
  { 0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,0 },
  { 0,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0 },
  { 0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0 },
  { 0,0,1,1,0,1,0,1,1,1,1,0,1,0,1,1,0,0 },
  { 0,0,0,1,1,0,1,0,1,1,0,1,0,1,1,0,0,0 },
  { 0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,0,0,0 },
  { 0,0,0,0,0,1,1,0,1,1,0,1,1,0,0,0,0,0 },
  { 0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
};

// Symbol 1 
static const uint8_t sl_sym1[18][18] PROGMEM = {
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0 },
  { 0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0 },
  { 0,0,0,1,1,1,1,1,0,0,0,0,1,0,0,0,0,0 },
  { 0,0,1,0,0,1,1,1,1,0,1,1,1,1,1,0,0,0 },
  { 0,1,0,1,1,1,1,1,0,1,0,0,1,1,1,1,0,0 },
  { 0,1,0,1,1,1,1,0,1,0,1,1,1,1,1,1,1,0 },
  { 0,1,0,1,1,1,1,0,1,0,1,1,1,1,1,1,1,0 },
  { 0,1,1,1,1,1,1,0,1,0,1,1,1,1,1,1,1,0 },
  { 0,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,0 },
  { 0,0,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,0 },
  { 0,0,0,1,1,1,1,1,0,1,1,1,1,1,1,1,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
};

// Symbol 2 
static const uint8_t sl_sym2[18][18] PROGMEM = {
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0 },
  { 0,0,0,1,0,0,1,1,0,0,1,1,0,0,1,0,0,0 },
  { 0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0 },
  { 0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0 },
  { 0,1,0,0,1,1,0,0,0,0,0,0,1,1,0,0,1,0 },
  { 0,1,0,0,1,0,0,0,0,0,0,0,0,1,0,0,1,0 },
  { 0,1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,0 },
  { 0,1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,0 },
  { 0,1,0,0,1,0,0,0,0,0,0,0,0,1,0,0,1,0 },
  { 0,1,0,0,1,0,0,0,0,0,0,0,0,1,0,0,1,0 },
  { 0,0,1,1,1,0,0,0,0,0,0,0,0,1,1,1,0,0 },
  { 0,0,0,1,1,1,0,0,0,0,0,0,1,1,1,0,0,0 },
  { 0,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,0 },
  { 0,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,0 },
  { 0,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
};

// Symbol 3 
static const uint8_t sl_sym3[18][18] PROGMEM = {
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,1,1,0 },
  { 0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,1,0 },
  { 0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,1,1,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0 },
  { 0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0 },
  { 0,0,1,1,0,1,1,1,1,1,1,0,1,1,1,0,0,0 },
  { 0,1,1,1,1,0,1,1,1,1,0,1,1,1,1,0,0,0 },
  { 0,0,0,0,1,1,0,0,0,0,1,1,1,1,0,0,0,0 },
  { 0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0 },
};

// Symbol 4 
static const uint8_t sl_sym4[18][18] PROGMEM = {
  { 0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0 },
  { 0,0,0,1,0,0,0,0,1,1,0,0,0,0,1,0,0,0 },
  { 0,0,0,0,1,0,0,0,1,1,0,0,0,1,0,0,0,0 },
  { 0,0,0,0,0,1,0,1,1,1,1,0,1,0,0,0,0,0 },
  { 0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0 },
  { 0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0 },
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
  { 0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0 },
  { 0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0 },
  { 0,0,0,0,0,1,0,1,1,1,1,0,1,0,0,0,0,0 },
  { 0,0,0,0,1,0,0,0,1,1,0,0,0,1,0,0,0,0 },
  { 0,0,0,1,0,0,0,0,1,1,0,0,0,0,1,0,0,0 },
  { 0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0 },
};

// Symbol 5 — 2× Multiplier  
static const uint8_t sl_sym5[18][18] PROGMEM = {
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0 },
  { 0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0 },
  { 0,1,0,1,1,1,1,1,0,0,1,0,0,0,0,0,0,0 },
  { 0,1,0,1,0,0,0,0,1,0,1,0,0,0,0,0,0,0 },
  { 0,1,1,1,0,0,0,0,1,0,1,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,0,1,0,0,1,0,0,0,1,1,0,0,0,0,1,1 },
  { 0,0,1,0,0,1,0,0,0,0,0,1,1,0,0,1,1,0 },
  { 0,1,0,0,1,1,1,1,1,1,1,0,1,1,1,1,0,0 },
  { 0,1,0,0,0,0,0,0,0,0,1,0,0,1,1,0,0,0 },
  { 0,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0 },
  { 0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1,1 },
};

// Symbol 6 — 3× Multiplier  
static const uint8_t sl_sym6[18][18] PROGMEM = {
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0 },
  { 0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0 },
  { 0,1,0,0,1,1,1,1,1,1,0,0,1,0,0,0,0,0 },
  { 0,1,0,1,0,0,0,0,0,1,0,0,1,0,0,0,0,0 },
  { 0,1,0,1,0,0,0,0,0,1,0,0,1,0,0,0,0,0 },
  { 0,1,1,1,0,0,0,0,0,1,0,0,1,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0 },
  { 0,1,1,1,0,0,0,0,1,0,0,1,0,0,0,0,0,0 },
  { 0,1,0,1,0,0,0,0,0,1,1,1,1,0,0,0,1,1 },
  { 0,1,0,1,0,0,0,0,0,1,0,1,1,0,0,1,1,0 },
  { 0,1,0,0,1,1,1,1,1,1,0,0,1,1,1,1,0,0 },
  { 0,0,1,0,0,0,0,0,0,0,0,1,0,1,1,0,0,0 },
  { 0,0,0,1,1,1,1,1,1,1,1,0,1,1,1,1,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0 },
  { 0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1,1 },
};

// Pointer table for easy indexed access
static const uint8_t* const sl_syms[SL_NUM_SYMS] = {
  (const uint8_t*)sl_sym0,
  (const uint8_t*)sl_sym1,
  (const uint8_t*)sl_sym2,
  (const uint8_t*)sl_sym3,
  (const uint8_t*)sl_sym4,
  (const uint8_t*)sl_sym5,  // 2×
  (const uint8_t*)sl_sym6,  // 3×
};

// ============================================================================
// REEL LAYOUT
// ============================================================================
// Three reels, each 20×20 px box (18px sprite + 1px padding each side)
#define SL_REEL_W   20
#define SL_REEL_H   20
#define SL_REEL_GAP  5   // gap between reel boxes

// Vertical: zone from y=9 (below top bar) to y=56 (above HUD). Centre boxes in that zone.
#define SL_REEL_Y   ((9 + 56 - SL_REEL_H) / 2)   // = (65 - 20) / 2 = 22

// X positions for left edges of each reel box
static inline int slReelX(int i) {
  int totalW = 3 * SL_REEL_W + 2 * SL_REEL_GAP;
  int startX = (SL_SCREEN_W - totalW) / 2;
  return startX + i * (SL_REEL_W + SL_REEL_GAP);
}

// ============================================================================
// GAME STATE
// ============================================================================
enum SlState {
  SLS_SPLASH,   // "Enter bank amount" — bet adjustment, BTN2 to confirm
  SLS_IDLE,     // reels shown blank/static, SPIN at bottom, BTN2 to spin
  SLS_RESULT    // outcome shown, - SPIN + at bottom, BTN2 spins again
};
static SlState sl_state;

// Final symbol on each reel (set after spin)
static uint8_t sl_reelResult[3];

// ============================================================================
// RNG
// ============================================================================
static uint32_t sl_rng = 54321;
static uint32_t slRand() {
  sl_rng = sl_rng * 1664525UL + 1013904223UL;
  return sl_rng;
}

// ============================================================================
// DRAW HELPERS
// ============================================================================
static void slDrawSymbol(uint8_t symIdx, int ox, int oy) {
  // Symbol is SL_SYM_W×SL_SYM_H; centre it inside reel box (2px padding each side)
  int px = ox + (SL_REEL_W - SL_SYM_W) / 2;
  int py = oy + (SL_REEL_H - SL_SYM_H) / 2;
  const uint8_t* src = sl_syms[symIdx];
  for (int r = 0; r < SL_SYM_H; r++)
    for (int c = 0; c < SL_SYM_W; c++)
      if (pgm_read_byte(src + r * SL_SYM_W + c))
        display.drawPixel(px + c, py + r, SSD1306_WHITE);
}

static void slDrawReel(int reelIdx, int symIdx, bool drawBox = true) {
  int rx = slReelX(reelIdx);
  int ry = SL_REEL_Y;
  if (drawBox) display.drawRect(rx, ry, SL_REEL_W, SL_REEL_H, SSD1306_WHITE);
  if (symIdx >= 0) slDrawSymbol((uint8_t)symIdx, rx, ry);
}

static void slDrawAllReels(int sym0, int sym1, int sym2) {
  slDrawReel(0, sym0);
  slDrawReel(1, sym1);
  slDrawReel(2, sym2);
}

static void slPrintCentered(const char* msg, int y) {
  int len = strlen(msg);
  int x   = (SL_SCREEN_W - len * 6) / 2;
  if (x < 0) x = 0;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(x, y);
  display.print(msg);
}

// Draw bottom HUD bar
// mode "bet"    → "- $X"  centreLabel  "+ $X"
// mode "spin"   → "- $X"  "SPIN"  "+ $X"   (same as bet but label differs)
// mode "spinonly" → just centred "SPIN" (during idle with no bet controls)
static void slDrawHUD(const char* centreLabel, bool showBetControls) {
  int y = SL_SCREEN_H - 7;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (showBetControls) {
    char decBuf[10], incBuf[10];
    snprintf(decBuf, sizeof(decBuf), "- $%d", slCurrentBet());
    snprintf(incBuf, sizeof(incBuf), "+ $%d", slCurrentBet());
    display.setCursor(0, y);
    display.print(decBuf);
    int rw = strlen(incBuf) * 6;
    display.setCursor(SL_SCREEN_W - rw, y);
    display.print(incBuf);
  }

  if (centreLabel) {
    int len = strlen(centreLabel);
    int cx  = (SL_SCREEN_W - len * 6) / 2;
    display.setCursor(cx, y);
    display.print(centreLabel);
  }
}

// Top-left: BANK $X (no sign if positive, - if negative)
// Top-right: NET +/-$X (current bank minus starting bank this session)
static void slDrawTopBar() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Left: bank balance — no sign when positive, minus sign when negative
  char bankBuf[14];
  if (sl_bank >= 0)
    snprintf(bankBuf, sizeof(bankBuf), "BANK $%ld", sl_bank);
  else
    snprintf(bankBuf, sizeof(bankBuf), "BANK -$%ld", -sl_bank);
  display.setCursor(0, 0);
  display.print(bankBuf);

  // Right: net gain/loss vs session start
  long net = sl_bank - sl_startBank;
  char netBuf[12];
  if (net >= 0)
    snprintf(netBuf, sizeof(netBuf), "NET +$%ld", net);
  else
    snprintf(netBuf, sizeof(netBuf), "NET -$%ld", -net);
  int nw = strlen(netBuf) * 6;
  display.setCursor(SL_SCREEN_W - nw, 0);
  display.print(netBuf);
}

// ============================================================================
// SCREEN RENDERS
// ============================================================================
static void slRenderSplash() {
  display.clearDisplay();
  slPrintCentered("SLOTS", 3);

  // Draw three empty reel boxes
  slDrawAllReels(-1, -1, -1);

  // "Enter bank amount" label above HUD
  slPrintCentered("Enter bank amount", SL_SCREEN_H - 17);

  // Bottom HUD: "- $X"  PLAY  "+ $X"  using bank entry denominations
  int y = SL_SCREEN_H - 7;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  char decBuf[14], incBuf[14];
  snprintf(decBuf, sizeof(decBuf), "- $%d", slCurrentBankEntry());
  snprintf(incBuf, sizeof(incBuf), "+ $%d", slCurrentBankEntry());
  display.setCursor(0, y);
  display.print(decBuf);
  int rw = strlen(incBuf) * 6;
  display.setCursor(SL_SCREEN_W - rw, y);
  display.print(incBuf);
  slPrintCentered("PLAY", y);

  display.display();

  // Startup jingle
  tone(BUZZER_PIN, 784,  80); delay(90);
  tone(BUZZER_PIN, 1047, 80); delay(90);
  tone(BUZZER_PIN, 1319, 120);
}

static void slRenderResult(const char* msg);   // forward declaration

static void slRenderIdle() {
  display.clearDisplay();
  slDrawTopBar();
  slDrawAllReels(sl_reelResult[0], sl_reelResult[1], sl_reelResult[2]);
  slDrawHUD("SPIN", true);
  display.display();
}

static void slRenderResult(const char* msg) {
  display.clearDisplay();
  slDrawTopBar();
  slDrawAllReels(sl_reelResult[0], sl_reelResult[1], sl_reelResult[2]);
  if (msg) slPrintCentered(msg, SL_REEL_Y - 10);
  slDrawHUD("SPIN", true);
  display.display();
}

// ============================================================================
// SPIN ANIMATION + OUTCOME
// ============================================================================
static void slDoSpin() {
  sl_rng ^= (uint32_t)millis();

  // Pick final results
  sl_reelResult[0] = slRand() % SL_NUM_SYMS;
  sl_reelResult[1] = slRand() % SL_NUM_SYMS;
  sl_reelResult[2] = slRand() % SL_NUM_SYMS;

  // Animate each reel: rapid cycling, then slow to final symbol left→right
  static const uint8_t spinCounts[3] = { 15, 15, 15 };

  for (int reel = 0; reel < 3; reel++) {
    uint8_t ticks = spinCounts[reel];
    uint8_t cur = slRand() % SL_NUM_SYMS;
    for (uint8_t t = 0; t < ticks; t++) {
      cur = (cur + 1) % SL_NUM_SYMS;
      display.clearDisplay();
      slDrawTopBar();
      for (int r2 = 0; r2 < 3; r2++) {
        if      (r2 < reel)  slDrawReel(r2, sl_reelResult[r2]);
        else if (r2 == reel) slDrawReel(r2, cur);
        else                 slDrawReel(r2, -1);
      }
      slDrawHUD("SPIN", true);
      display.display();
      int ms = (t < ticks - 3) ? 80 : 150;
      tone(BUZZER_PIN, 150 + cur * 80, 20);
      delay(ms);
    }
    // Settle this reel on its true result
    display.clearDisplay();
    slDrawTopBar();
    for (int r2 = 0; r2 < 3; r2++) {
      if (r2 <= reel) slDrawReel(r2, sl_reelResult[r2]);
      else            slDrawReel(r2, -1);
    }
    slDrawHUD("SPIN", true);
    display.display();
    delay(200);
  }

  uint8_t r0 = sl_reelResult[0];
  uint8_t r1 = sl_reelResult[1];
  uint8_t r2 = sl_reelResult[2];

  bool triple = (r0 == r1 && r1 == r2);
  // Double: exactly two match (any pair among three)
  bool doublePair = (!triple) && (r0 == r1 || r1 == r2 || r0 == r2);

  if (triple) {
    // Determine payout multiplier
    int mult = 1;
    if (r0 == SL_SYM_2X) mult = 2;
    if (r0 == SL_SYM_3X) mult = 3;

    long payout = (long)slCurrentBet() * mult;
    sl_bank += payout;

    // Win jingle — fancier for multipliers
    tone(BUZZER_PIN, 1047, 80); delay(90);
    tone(BUZZER_PIN, 1319, 80); delay(90);
    tone(BUZZER_PIN, 1568, 120); delay(130);
    if (mult >= 2) { tone(BUZZER_PIN, 1760, 80); delay(90); }
    if (mult >= 3) { tone(BUZZER_PIN, 2093, 120); delay(130); }

    char winMsg[14];
    if (mult == 1) snprintf(winMsg, sizeof(winMsg), "WIN!");
    else           snprintf(winMsg, sizeof(winMsg), "WIN! x%d", mult);

    // Blink all three reels 3 times on a triple win
    for (int blink = 0; blink < 6; blink++) {
      display.clearDisplay();
      slDrawTopBar();
      if (blink % 2 == 0) {
        slDrawAllReels(r0, r1, r2);
      } else {
        slDrawReel(0, -1); slDrawReel(1, -1); slDrawReel(2, -1);
      }
      slPrintCentered(winMsg, SL_REEL_Y - 10);
      slDrawHUD("SPIN", true);
      display.display();
      tone(BUZZER_PIN, (blink % 2 == 0) ? 1568 : 1319, 40);
      delay(300);
    }
    sl_state = SLS_RESULT;
    slRenderResult(winMsg);

  } else if (doublePair) {
    // Determine which symbol is the matching pair
    uint8_t pairSym = (r0 == r1) ? r0 : (r1 == r2) ? r1 : r0;

    // Which reels are part of the match
    bool matchReel[3];
    matchReel[0] = (r0 == r1 || r0 == r2);
    matchReel[1] = (r0 == r1 || r1 == r2);
    matchReel[2] = (r1 == r2 || r0 == r2);

    int freeSpins = 1;
    const char* freeMsg = "ONE FREE SPIN!";
    if (pairSym == SL_SYM_2X) { freeSpins = 2; freeMsg = "TWO FREE SPINS!"; }
    else if (pairSym == SL_SYM_3X) { freeSpins = 3; freeMsg = "THREE FREE SPINS!"; }

    // Blink the matching reels N times while showing the free spin message
    for (int blink = 0; blink < freeSpins * 2; blink++) {
      display.clearDisplay();
      slDrawTopBar();
      // Draw matching reels only on even blink (visible), hide on odd (blank)
      for (int ri = 0; ri < 3; ri++) {
        int sym = (ri == 0) ? r0 : (ri == 1) ? r1 : r2;
        if (matchReel[ri] && (blink % 2 == 1)) {
          slDrawReel(ri, -1);  // blank (blink off)
        } else {
          slDrawReel(ri, sym);
        }
      }
      slPrintCentered(freeMsg, SL_REEL_Y - 10);
      slDrawHUD("SPIN", true);
      display.display();
      tone(BUZZER_PIN, (blink % 2 == 0) ? 800 : 1600, 100);
      delay(200);
    }

    for (int i = 0; i < freeSpins; i++) {
      slDoSpin();
    }

  } else {
    // No match — lose the bet
    sl_bank -= slCurrentBet();
    tone(BUZZER_PIN, 300, 200);
    sl_state = SLS_RESULT;
    slRenderResult("LOSE");
  }
}

// ============================================================================
// PAUSE SCREEN
// ============================================================================
static void slDrawPauseScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(34, 22);
  display.print("PAUSE");
  display.setTextSize(1);
  display.setCursor(0, 55);
  display.print("PLAY");
  display.setCursor(SL_SCREEN_W - 24, 55);
  display.print("EXIT");
  display.display();
}

// ============================================================================
// INTERNAL SETUP
// ============================================================================
static void slotsSetup_Internal() {
  sl_rng ^= (uint32_t)millis();
  sl_state = SLS_SPLASH;
  sl_paused = false;
  sl_pauseNeedsRelease = false;
  sl_b1Prev = HIGH;
  sl_b2Prev = HIGH;
  sl_b3Prev = HIGH;
  sl_betIndex  = 0;
  sl_bankIndex = 6;   // default to $100 on bank entry screen
  sl_reelResult[0] = sl_reelResult[1] = sl_reelResult[2] = 0;
  // sl_bank intentionally NOT reset — persists until power-off
  slRenderSplash();
}

// ============================================================================
// INTERNAL LOOP
// ============================================================================
static void slotsLoop_Internal() {
  slMcpRead(sl_mcp_gpio);
  slBtnUpdate(sl_b1);
  slBtnUpdate(sl_b2);
  slBtnUpdate(sl_b3);

  // ── PAUSE TOGGLE ──────────────────────────────────────────────────────────
  extern bool g_gamePauseRequest;
  if (g_gamePauseRequest) {
    g_gamePauseRequest = false;
    if (!sl_paused) {
      sl_paused = true;
      sl_pauseBtn1WasDown = false;
      sl_pauseBtn3WasDown = false;
      sl_b1Prev = HIGH; sl_b2Prev = HIGH; sl_b3Prev = HIGH;
    }
  }

  // ── PAUSE SCREEN ──────────────────────────────────────────────────────────
  if (sl_paused) {
    bool b1c = slBtnDown(sl_b1);
    bool b3c = slBtnDown(sl_b3);

    if (sl_pauseBtn1WasDown && !b1c) {
      sl_pauseBtn1WasDown = false;
      sl_paused = false;
      sl_b1Prev = HIGH; sl_b2Prev = HIGH; sl_b3Prev = HIGH;
      slMcpRead(sl_mcp_gpio);
      slBtnUpdate(sl_b1); slBtnUpdate(sl_b2); slBtnUpdate(sl_b3);
      sl_b1Prev = HIGH; sl_b2Prev = HIGH; sl_b3Prev = HIGH;
      switch (sl_state) {
        case SLS_SPLASH: slRenderSplash(); break;
        case SLS_IDLE:   slRenderIdle();   break;
        case SLS_RESULT: slRenderResult(nullptr); break;  // re-render last result (no label refresh needed)
      }
      return;
    } else { sl_pauseBtn1WasDown = b1c; }

    if (sl_pauseBtn3WasDown && !b3c) {
      sl_pauseBtn3WasDown = false;
      sl_paused = false;
      sl_b1Prev = HIGH; sl_b2Prev = HIGH; sl_b3Prev = HIGH;
      gameExitToMenu();
      return;
    } else { sl_pauseBtn3WasDown = b3c; }

    slDrawPauseScreen();
    delay(30);
    return;
  }

  // ── EDGE DETECTION ────────────────────────────────────────────────────────
  bool dec  = sl_justPressed(sl_b1, sl_b1Prev);
  bool spin = sl_justPressed(sl_b2, sl_b2Prev);
  bool inc  = sl_justPressed(sl_b3, sl_b3Prev);

  // ── STATE MACHINE ─────────────────────────────────────────────────────────
  switch (sl_state) {

    case SLS_SPLASH:
      // BTN1/BTN3 adjust bank entry amount; BTN2 confirms and enters idle
      if (dec)  { slBankDecrease(); slRenderSplash(); break; }
      if (inc)  { slBankIncrease(); slRenderSplash(); break; }
      if (spin) {
        // Confirm bank: set sl_bank to chosen entry amount
        sl_bank = slCurrentBankEntry();
        sl_startBank = sl_bank;   // record baseline for NET calculation
        sl_betIndex = 0;   // reset bet to minimum for new session
        sl_state = SLS_IDLE;
        sl_reelResult[0] = sl_reelResult[1] = sl_reelResult[2] = 0;
        slRenderIdle();
      }
      break;

    case SLS_IDLE:
      // BTN1/BTN3 adjust bet before spinning; BTN2 spins
      if (dec)  { slBetDecrease(); slRenderIdle(); break; }
      if (inc)  { slBetIncrease(); slRenderIdle(); break; }
      if (spin) {
        slDoSpin();
      }
      break;

    case SLS_RESULT:
      // BTN1/BTN3 adjust bet for next spin; BTN2 spins again
      if (dec)  { slBetDecrease(); slRenderResult(nullptr); break; }
      if (inc)  { slBetIncrease(); slRenderResult(nullptr); break; }
      if (spin) {
        slDoSpin();
      }
      break;
  }

  delay(30);
}

// ============================================================================
// PUBLIC API
// ============================================================================
void slotsEnter() {
  sl_active = true;
  if (!sl_setupRan) {
    slotsSetup_Internal();
    sl_setupRan = true;
  } else {
    sl_paused = false;
    sl_pauseNeedsRelease = false;
    sl_b1Prev = HIGH; sl_b2Prev = HIGH; sl_b3Prev = HIGH;
    sl_betIndex  = 0;
    sl_bankIndex = 6;   // default to $100 on re-entry
    sl_rng ^= (uint32_t)millis();
    sl_reelResult[0] = sl_reelResult[1] = sl_reelResult[2] = 0;
    sl_state = SLS_SPLASH;
    slRenderSplash();
  }
}

void slotsUpdate() {
  if (!sl_active) return;
  slotsLoop_Internal();
}

void slotsExit() {
  sl_active = false;
  noTone(BUZZER_PIN);
}