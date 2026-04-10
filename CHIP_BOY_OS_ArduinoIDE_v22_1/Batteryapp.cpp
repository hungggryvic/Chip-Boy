// ============================================================
//  BatteryApp.cpp — Battery voltage monitor
//
//  Sampling runs continuously in the background via
//  batteryBackgroundSample(), called from the main loop
//  unconditionally.  Entering/exiting the app never pauses
//  or resets the buffer — you always see history since boot.
//
//  The graph always shows ALL data from boot to present.
//  When the buffer fills, pairs of adjacent samples are averaged
//  (decimation) and the effective sample interval doubles.
//  This keeps memory fixed at GRAPH_SAMPLES floats while
//  covering the full elapsed time.
//
//  Effective time coverage (BATT_SAMPLE_MS = 2000, 128 slots):
//    Fill 1:  128 × 2s  =  ~4 min   (raw)
//    Fill 2:  128 × 4s  =  ~8 min   (1 decimation)
//    Fill 3:  128 × 8s  =  ~17 min  (2 decimations)
//    Fill 4:  128 × 16s =  ~34 min  (3 decimations)
//    ...each fill doubles coverage
// ============================================================

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---- Externals provided by the main sketch -----------------
extern Adafruit_SSD1306 display;
extern bool displayReady;
extern bool screenEnabled;
extern bool inMenu;
extern bool inBattery;
extern bool uiDirty;
extern unsigned long menuEnableButtonsAt;

void redrawMenuNow();

// ---- Pin / ADC constants -----------------------------------
#ifndef BATT_PIN
  #define BATT_PIN D3
#endif
#ifndef SCREEN_WIDTH
  #define SCREEN_WIDTH 128
#endif
#ifndef SCREEN_HEIGHT
  #define SCREEN_HEIGHT 64
#endif

// ---- Graph config ------------------------------------------
#define GRAPH_X       0
#define GRAPH_Y       20
#define GRAPH_W       128
#define GRAPH_H       34
#define GRAPH_SAMPLES 128

#define VBATT_MIN  3.0f
#define VBATT_MAX  4.05f

// ---- SoC piecewise lookup table ----------------------------
// Derived from the discharge curve (voltage left axis).
// Each entry is { voltage, SoC% }.  Table must be descending by voltage.
struct SoCPoint { float v; float soc; };
static const SoCPoint SOC_TABLE[] = {
  { 4.05f, 100.0f },
  { 3.85f,  90.0f },
  { 3.60f,  70.0f },
  { 3.40f,  40.0f },
  { 3.30f,  20.0f },
  { 3.00f,   5.0f },
  { 2.75f,   0.0f },
};
static const uint8_t SOC_TABLE_LEN =
    sizeof(SOC_TABLE) / sizeof(SOC_TABLE[0]);

// Returns estimated SoC 0–100 % for a given battery voltage.
static float batteryEstimateSoC(float vBatt) {
  // Above top of table
  if (vBatt >= SOC_TABLE[0].v) return 100.0f;
  // Below bottom of table
  if (vBatt <= SOC_TABLE[SOC_TABLE_LEN - 1].v) return 0.0f;
  // Find the bracketing segment and interpolate linearly
  for (uint8_t i = 0; i < SOC_TABLE_LEN - 1; i++) {
    const SoCPoint &hi = SOC_TABLE[i];
    const SoCPoint &lo = SOC_TABLE[i + 1];
    if (vBatt <= hi.v && vBatt >= lo.v) {
      float t = (vBatt - lo.v) / (hi.v - lo.v);   // 0…1
      return lo.soc + t * (hi.soc - lo.soc);
    }
  }
  return 0.0f;
}

// ---- Timing ------------------------------------------------
// Base sample interval. Each decimation doubles the effective interval.
//
//  2000  →  2 s/sample base  (default)
//  5000  →  5 s/sample base
// 10000  → 10 s/sample base
static const uint32_t BATT_SAMPLE_MS  = 2000UL;

// How often the screen redraws while the app is open.
static const uint32_t BATT_DISPLAY_MS = 1000UL;

// ---- Module-private state ----------------------------------
// Sampling
static uint32_t battLastSampleMs  = 0;

// graphBuf is a flat, chronological array — index 0 = oldest, [graphCount-1] = newest.
// No ring-buffer head pointer needed; decimation collapses in-place.
static float    graphBuf[GRAPH_SAMPLES];
static uint8_t  graphCount = 0;   // number of valid samples (0 … GRAPH_SAMPLES)

// Display
static uint32_t battLastDrawMs = 0;

// ---- Private helpers ---------------------------------------

static float battReadVoltage() {
  int32_t raw = 0;
  for (int i = 0; i < 16; i++) raw += analogRead(BATT_PIN);
  raw /= 16;
  // Voltage divider at ADC pin: Vbatt / 2 via 100k+100k
  return (raw / 4095.0f) * 3.3f;
}

// Decimate: average adjacent pairs → half as many samples, same buffer.
// Called only when graphCount == GRAPH_SAMPLES and a new sample arrives.
static void decimateGraph() {
  uint8_t newCount = GRAPH_SAMPLES / 2;
  for (uint8_t i = 0; i < newCount; i++) {
    graphBuf[i] = (graphBuf[i * 2] + graphBuf[i * 2 + 1]) * 0.5f;
  }
  graphCount = newCount;
}

static void pushGraph(float vBatt) {
  if (graphCount >= GRAPH_SAMPLES) {
    // Buffer full — compress history before adding new sample
    decimateGraph();
  }
  graphBuf[graphCount++] = vBatt;
}

static void drawGraph() {
  display.drawRect(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H, SSD1306_WHITE);

  if (graphCount < 2) return;

  int innerW = GRAPH_W - 2;
  int innerH = GRAPH_H - 2;

  for (uint8_t i = 0; i < graphCount; i++) {
    float v = graphBuf[i];

    float norm = (v - VBATT_MIN) / (VBATT_MAX - VBATT_MIN);
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    // X spans full width; always covers boot → now regardless of sample count
    int px = GRAPH_X + 1 + (int)((float)i * innerW / (graphCount - 1));
    int py = GRAPH_Y + 1 + innerH - 1 - (int)(norm * (innerH - 1));

    display.drawPixel(px, py, SSD1306_WHITE);
  }
}

static void drawBatteryImpl(float vDiv, float vBatt) {
  if (!displayReady) return;

  float soc = batteryEstimateSoC(vBatt);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // ---- Top-left: voltage + SoC ------------------------------
  char buf[32];

  // Row 0: "Battery: 3.742V  87%"
  display.setCursor(0, 0);
  display.print("Battery:");
  snprintf(buf, sizeof(buf), "%.3fV", vBatt);
  display.print(buf);

  // SoC right-aligned on row 0 (3 digits + '%' = 4 chars = 24 px from right,
  // but leave room for the battery icon which starts at ~SCREEN_WIDTH-17)
  snprintf(buf, sizeof(buf), "%3d%%", (int)soc);
  display.setCursor(SCREEN_WIDTH - 17 - 6 * 4 - 2, 0);
  display.print(buf);

  // Row 1: ADC divider voltage (smaller interest detail)
  display.setCursor(0, 10);
  display.print("ADC:");
  snprintf(buf, sizeof(buf), "%.3fV", vDiv);
  display.print(buf);

  // ---- Top-right: small battery icon (fill driven by SoC) ---
  const int ICON_W     = 14;
  const int ICON_H     = 7;
  const int ICON_PAD   = 1;
  const int ICON_NUB_W = 2;
  const int ICON_NUB_H = 3;

  int sx = SCREEN_WIDTH - ICON_W - ICON_NUB_W - 1;
  int sy = 0;

  display.drawRect(sx, sy, ICON_W, ICON_H, SSD1306_WHITE);

  int nubX = sx + ICON_W;
  int nubY = sy + (ICON_H - ICON_NUB_H) / 2;
  display.fillRect(nubX, nubY, ICON_NUB_W, ICON_NUB_H, SSD1306_WHITE);

  // Use SoC (0–100) to drive fill so the icon matches the curve-corrected %
  float pct = soc / 100.0f;
  int fillW = (int)(pct * (ICON_W - 2 * ICON_PAD));
  if (fillW > 0) {
    display.fillRect(sx + ICON_PAD, sy + ICON_PAD,
                     fillW, ICON_H - 2 * ICON_PAD, SSD1306_WHITE);
  }

  // ---- Graph ------------------------------------------------
  drawGraph();

  // ---- Footer -----------------------------------------------
  display.setCursor(SCREEN_WIDTH - 6 * 4, 55);
  display.print("EXIT");

  display.display();
}

// ============================================================
//  Public API
// ============================================================

// Call this unconditionally from the main loop — it self-throttles
// via BATT_SAMPLE_MS and keeps running whether the app is open or not.
void batteryBackgroundSample() {
  uint32_t now = millis();
  if ((now - battLastSampleMs) < BATT_SAMPLE_MS) return;
  battLastSampleMs = now;

  float vDiv  = battReadVoltage();
  float vBatt = vDiv * 2.0f;
  pushGraph(vBatt);
}

void batteryEnter() {
  inMenu    = false;
  inBattery = true;
  uiDirty   = true;
  battLastDrawMs = 0;   // force immediate repaint on entry
  // graphBuf / graphCount intentionally NOT reset —
  // we keep all history since boot.
}

void batteryExit() {
  inBattery           = false;
  inMenu              = true;
  uiDirty             = true;
  menuEnableButtonsAt = millis() + 120;
  redrawMenuNow();
}

// Repaints the screen while the app is open. Does NOT sample —
// sampling is handled exclusively by batteryBackgroundSample().
void drawBatteryScreen() {
  if (!screenEnabled || !displayReady) return;

  uint32_t now = millis();
  if ((now - battLastDrawMs) < BATT_DISPLAY_MS) return;
  battLastDrawMs = now;

  // Most recent sample for the live readout.
  float vBatt = (graphCount > 0) ? graphBuf[graphCount - 1] : 0.0f;
  float vDiv  = vBatt / 2.0f;

  drawBatteryImpl(vDiv, vBatt);
}