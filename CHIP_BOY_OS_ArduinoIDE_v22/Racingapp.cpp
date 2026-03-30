// ==============================
// NIGHT DRIVE — CHIP-BOY PORT
// Display : SSD1306 I2C 0x3C  (shared with OS)
// Buttons : MCP23008 at 0x20  (shared with OS)
// Buzzer  : D2                (shared with OS)
// Pause   : Encoder switch D6 (same as IronTides / Blackjack / Obscurus)
//
// Controls in game:
//   BTN1  (GP0) = Steer LEFT
//   BTN3  (GP1) = Steer RIGHT
//   ENC SW      = PAUSE
//
// Pause menu:
//   BTN1  = PLAY (resume)
//   BTN3  = EXIT (back to games menu)
// ==============================

#include "RacingApp.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// ============================================================================
// OS APP-WRAPPER STATE
// ============================================================================
static bool ra_active   = false;
static bool ra_setupRan = false;

bool racingIsActive() { return ra_active; }

extern void gameExitToMenu();   // defined in main chipboy .ino

// ============================================================================
// SHARED HARDWARE  (owned by OS — we just reference them)
// ============================================================================
#define RA_SCREEN_W  128
#define RA_SCREEN_H   64

extern Adafruit_SSD1306 display;
extern bool             displayReady;

// ============================================================================
// MCP23008 — own Wire read, same address as OS (0x20)
// Each app does its own read so it doesn't depend on OS timing.
// ============================================================================
#define RA_MCP_ADDR 0x20
#define RA_MCP_GPIO 0x09

// MCP GPIO bit assignments (identical to OS mapping)
#define RA_GP_BTN1   0   // GP0 — ACCELERATE
#define RA_GP_BTN3   1   // GP1 — BRAKE (gameplay) / EXIT (pause menu)
#define RA_GP_NAV_L  2   // GP5 — steer LEFT  (nav switch left)
#define RA_GP_NAV_R  5   // GP2 — steer RIGHT (nav switch right)

#define RA_BUZZER_PIN D2
//#define RA_ENC_SW_PIN D6

static uint8_t ra_mcpGpio = 0xFF;   // 0xFF = all released

static bool ra_mcpRead() {
  Wire.beginTransmission(RA_MCP_ADDR);
  Wire.write(RA_MCP_GPIO);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)RA_MCP_ADDR, 1) != 1) return false;
  ra_mcpGpio = Wire.read();
  return true;
}

// LOW = pressed (active-low pull-up)
static inline bool ra_btnDown(uint8_t gp) {
  return !(ra_mcpGpio & (1u << gp));
}

// ============================================================================
// ENCODER SWITCH  (same debounce pattern as ObscurusApp)
// ============================================================================
static const unsigned long RA_ENC_DEBOUNCE_MS = 35;
static bool          ra_encStable     = HIGH;
static bool          ra_encLastRead   = HIGH;
static unsigned long ra_encLastChange = 0;

// Returns true on the falling edge (press event), debounced.
static bool ra_encPressed() {
  bool r = (ra_mcpGpio & (1u << 7)) ? HIGH : LOW;
  if (r != ra_encLastRead) { ra_encLastRead = r; ra_encLastChange = millis(); }
  if ((millis() - ra_encLastChange) >= RA_ENC_DEBOUNCE_MS && r != ra_encStable) {
    bool fell = (ra_encStable == HIGH && r == LOW);
    ra_encStable = r;
    return fell;
  }
  return false;
}

// ============================================================================
// PAUSE STATE  (mirrors ObscurusApp exactly)
// ============================================================================
static bool          ra_paused            = false;
static bool          ra_pauseNeedsRelease = false;
static unsigned long ra_pauseIgnoreUntil  = 0;
static bool          ra_pauseBtn1WasDown  = false;
static bool          ra_pauseBtn3WasDown  = false;

// ============================================================================
// RNG  (simple LCG — no analogRead; seeded with millis() on each reset)
// ============================================================================
static uint32_t ra_seed = 0xDEADBEEF;

static uint32_t ra_rand32() {
  ra_seed = ra_seed * 1664525UL + 1013904223UL;
  return ra_seed;
}
// random integer in [lo, hi)
static int ra_rand(int lo, int hi) {
  if (hi <= lo) return lo;
  return lo + (int)(ra_rand32() % (uint32_t)(hi - lo));
}

// ============================================================================
// ROAD & PERSPECTIVE CONSTANTS
// ============================================================================
static const int   RA_HORIZON_Y   = RA_SCREEN_H / 2 - 5;
static const float RA_ROAD_W_H    = 40.0f;               // road width at horizon
static const float RA_ROAD_W_B    = 118.0f;              // road width at bottom
static const float RA_CURVE_SPEED = 0.02f;

// Two-lane road widths (approximately half the 4-lane values)
static const float RA_ROAD_W_H_NARROW = 20.0f;   // horizon width when 2-lane
static const float RA_ROAD_W_B_NARROW = 59.0f;   // bottom  width when 2-lane

// Animated road width lerp scalar: 1.0 = full 4-lane, 0.0 = full 2-lane
static float ra_roadLerp = 1.0f;

// Narrow-zone state machine
enum RaNarrowState { RA_NZ_IDLE, RA_NZ_NARROWING, RA_NZ_NARROW, RA_NZ_WIDENING };
static RaNarrowState ra_narrowState   = RA_NZ_IDLE;
static unsigned long ra_narrowTrigger = 0;   // track units remaining when zone fires
static unsigned long ra_narrowStart   = 0;   // millis() when current phase began
static const unsigned long RA_NZ_TRANSITION_MS = 2500; // morph in/out duration (ms)
static const unsigned long RA_NZ_HOLD_MS       = 7000; // hold at full narrow (ms)

// 2-lane fractions — only the inner two lanes (±0.25 of halfW)
static const float RA_LANE_FRACS_NARROW[2] = { -0.25f, 0.25f };

// ============================================================================
// CAR CONSTANTS  (must match sprite dimensions above)
// ============================================================================
static const int RA_HALF_BOTTOM = 8;   // half-width (sprite is 16px wide)
static const int RA_CAR_H      = 6;   // sprite height

// ============================================================================
// SCENERY ARRAYS
// ============================================================================
static const int RA_NUM_COPIES = 8;
static int ra_dashL[RA_NUM_COPIES];
static int ra_dashR[RA_NUM_COPIES];

static const int RA_NUM_STARS = 30;
static int ra_starX[RA_NUM_STARS];
static int ra_starY[RA_NUM_STARS];

static const int RA_NUM_MTN = 16;
static int ra_mtnX[RA_NUM_MTN];
static int ra_mtnY[RA_NUM_MTN];
static bool ra_mtnReady = false;

// (tokens and hazards removed)

// ============================================================================
// TRAFFIC VEHICLE SPRITES  (16 wide × 6 tall, 1=ON 0=OFF)
// Edit ONLY the 0s and 1s inside each array.
// Row 0 = top (nose/front), Row 5 = bottom (tail).
// Col 0 = left edge, Col 15 = right edge.
// One of these three is picked at random for each vehicle that spawns.
// ============================================================================

// Sprite 0 
static const uint8_t ra_kTrafficSprite0[6][16] PROGMEM = {
  { 0,1,0,1,1,1,1,1,1,1,1,1,1,0,1,0 },  // row 0 
  { 0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0 },  // row 1
  { 0,1,0,0,1,1,1,1,1,1,1,1,0,0,1,0 },  // row 2  
  { 0,1,0,0,1,1,1,0,0,1,1,1,0,0,1,0 },  // row 3
  { 0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0 },  // row 4
  { 0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0 },  // row 5
};

// Sprite 1
static const uint8_t ra_kTrafficSprite1[6][16] PROGMEM = {
  { 0,1,0,1,1,1,1,1,1,1,1,1,1,0,1,0 },  // row 0  
  { 0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0 },  // row 1  
  { 0,1,0,0,1,1,1,1,1,1,1,1,0,0,1,0 },  // row 2  
  { 0,1,1,1,1,1,1,0,0,1,1,1,1,1,1,0 },  // row 3
  { 0,1,1,1,0,1,1,1,1,1,1,0,1,1,1,0 },  // row 4  
  { 0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0 },  // row 5  
};

// Sprite 2 
static const uint8_t ra_kTrafficSprite2[6][16] PROGMEM = {
  { 0,1,0,1,1,1,1,1,1,1,1,1,1,0,1,0 },  // row 0  
  { 0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0 },  // row 1 
  { 0,1,0,1,1,1,1,1,1,1,1,1,1,0,1,0 },  // row 2  
  { 0,1,0,1,1,1,1,0,0,1,1,1,1,0,1,0 },  // row 3  
  { 0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0 },  // row 4 
  { 0,1,1,1,0,0,0,0,0,0,0,0,1,1,1,0 },  // row 5 
};

// Pointer table for O(1) sprite lookup by index
static const uint8_t (* const ra_kTrafficSprites[3])[16] = {
  ra_kTrafficSprite0,
  ra_kTrafficSprite1,
  ra_kTrafficSprite2,
};

// ============================================================================
// TRAFFIC CARS
// ============================================================================
#define RA_MAX_TRAFFIC  4

// Lane offsets as a fraction of halfW — the 4 lane centres
// between the 3 lane lines at 0, ±halfW*0.5
// Lane centres sit at ±¼ and ±¾ of halfW
static const float RA_LANE_FRACS[4] = { -0.75f, -0.25f, 0.25f, 0.75f };

struct RaTraffic {
  float  laneF;       // lane fraction (one of RA_LANE_FRACS[])
  float  y;           // current screen Y (float for smooth movement)
  float  distSnap;    // value of ra_dist when this car spawned (for wave sync)
  bool   active;
  uint8_t spriteIdx;  // 0=sedan, 1=van, 2=sports — chosen at spawn
};

static RaTraffic     ra_traffic[RA_MAX_TRAFFIC];
static unsigned long ra_trafficTimer    = 0;
static unsigned long ra_trafficInterval = 0;

// ── Traffic burst state ───────────────────────────────────────────────────
// At the start, 50%, and 75% milestones a short dense burst fires, then
// traffic settles to a permanently slightly denser baseline than before.
static int           ra_trafficPhase    = 0;   // 0=pre-start,1=post-start,2=post-half,3=post-3q
static bool          ra_burstActive     = false;
static unsigned long ra_burstEnd        = 0;   // millis() when current burst expires
#define RA_BURST_MS        2500   // how long each burst lasts (ms)

// ============================================================================
// DRIFT
// ============================================================================
static float         ra_center       = 0.0f;   // road centre X offset
static float         ra_driftV       = 0.0f;   // drift velocity
static float         ra_maxDrift     = 0.8f;
static unsigned long ra_driftTimer   = 0;
static unsigned long ra_driftInterval= 0;

// ============================================================================
// EXPLOSION STATE  (player sprite explodes on last-life crash)
// ============================================================================
static bool          ra_exploding        = false;
static unsigned long ra_explodeStart     = 0;
static int           ra_explodeCX        = 0;    // screen X centre of car when crash happened
static int           ra_explodeCY        = 0;    // screen Y centre of car when crash happened
static const unsigned long RA_EXPLODE_MS = 1600; // total explosion duration (ms)

// ============================================================================
// GAME STATE
// ============================================================================
static float         ra_dist         = 0.0f;   // scroll distance accumulator
static float         ra_playerX     = 0.0f;   // player lateral offset from centre
static float         ra_speed        = 1.0f;   // scroll speed
static float         ra_curveAmp     = 10.0f;
static unsigned long ra_track        = 0;      // countdown score (distance remaining)
static unsigned long ra_trackMax     = 0;      // starting value of ra_track for this level
static int           ra_level        = 1;
static int           ra_lives        = 10;
static bool          ra_invincible   = false;
static unsigned long ra_invStart     = 0;
static bool          ra_gameOver     = false;

// Non-blocking traffic-hit blink state
// Mimics the 3×(off 130ms / on+beep 130ms) pattern of ra_carHit() without delay().
static bool          ra_trafficBlink      = false;  // true while blink sequence running
static unsigned long ra_trafficBlinkStep  = 0;      // millis() of last half-step change
static int           ra_trafficBlinkCount = 0;      // half-steps completed (0-5 = 3 full blinks)
static bool          ra_trafficBlinkShow  = false;  // current on/off state of the car
static const unsigned long RA_TRAFFIC_BLINK_HALF = 130; // ms per half-step (matches border hit)

// Session timer & distance (reset each level)
static unsigned long ra_levelStart   = 0;      // millis() when this level began
static float         ra_odometer     = 0.0f;   // miles accumulated this level

// ============================================================================
// COUNTDOWN / INTRO
// ============================================================================
static bool          ra_started      = false;
static int           ra_countdown    = 3;
static unsigned long ra_cdTimer      = 0;
static int           ra_cdLast       = -1;
static bool          ra_introActive  = false;
static unsigned long ra_introStart   = 0;

// ============================================================================
// FRAME TIMING
// ============================================================================
static unsigned long ra_lastFrame    = 0;
static const unsigned long RA_FRAME_MS = 30;  // ~33 FPS

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
static void ra_reset();
static void ra_genMountains();
static void ra_drawRoad();
static void ra_carHit();
static void ra_trafficHit();
static void ra_drawPause();
static void ra_updateAndDrawTraffic(bool drawOnly = false);
static void ra_drawFrozenScene();

// ============================================================================
// RESET GAME
// ============================================================================
static void ra_reset() {
  ra_seed    ^= (uint32_t)millis();   // re-seed for variety

  ra_dist     = 0.0f;
  ra_driftV   = 0.0f;
  ra_speed    = 1.0f;
  ra_curveAmp = 10.0f;
  ra_maxDrift = 0.8f;
  ra_lives    = 10;
  ra_gameOver = false;
  ra_invincible = false;
  ra_exploding  = false;
  ra_trafficBlink      = false;
  ra_trafficBlinkCount = 0;
  ra_trafficBlinkShow  = true;

  if (ra_level == 1) {} // nothing extra to reset on level 1

  // Align centre with the initial road wave so car starts on track
  float d0    = float((RA_SCREEN_H - 2) - RA_HORIZON_Y) / float(RA_SCREEN_H - RA_HORIZON_Y);
  ra_center   = -(sinf(d0 * 1.5f) * ra_curveAmp * d0);
  ra_playerX  = 0.0f;

  // Drift
  ra_driftTimer    = millis();
  ra_driftInterval = ra_rand(500, 2000);
  ra_driftV        = ra_rand(-int(ra_maxDrift * 10), int(ra_maxDrift * 10) + 1) / 10.0f;

  // Countdown
  ra_started   = false;
  ra_countdown = 3;
  ra_cdTimer   = millis();
  ra_cdLast    = -1;

  // Intro title ("NIGHT DRIVE") — only on level 1
  ra_introActive = (ra_level == 1);
  ra_introStart  = millis();

  // Mountains
  ra_mtnReady = false;

  // Dash spacing
  for (int i = 0; i < RA_NUM_COPIES; i++) {
    ra_dashL[i] = ra_rand(10, 25);
    ra_dashR[i] = ra_rand(10, 25);
  }

  // Stars
  for (int i = 0; i < RA_NUM_STARS; i++) {
    ra_starX[i] = ra_rand(0, RA_SCREEN_W);
    ra_starY[i] = ra_rand(0, RA_HORIZON_Y / 2);
  }

  // Track distance for this level
  if      (ra_level == 1) ra_track = 1000;
  else if (ra_level == 2) ra_track = 1500;
  else                    ra_track = 2000;
  ra_trackMax  = ra_track;

  // Schedule the one narrow-zone event for this level.
  // Fire somewhere between 20%–65% of the track elapsed so the full
  // transition + hold + widen fits before the end.
  ra_narrowState   = RA_NZ_IDLE;
  ra_roadLerp      = 1.0f;
  {
    unsigned long lo = ra_trackMax * 20 / 100;
    unsigned long hi = ra_trackMax * 65 / 100;
    // ra_narrowTrigger = track units REMAINING when we fire (so subtract from trackMax)
    unsigned long fireAt = lo + (ra_rand32() % (hi - lo + 1));
    ra_narrowTrigger = ra_trackMax - fireAt;   // remaining when zone starts
  }

  // Reset session stats
  ra_levelStart = millis();
  ra_odometer   = 0.0f;

  // Traffic cars
  for (int i = 0; i < RA_MAX_TRAFFIC; i++) ra_traffic[i].active = false;
  ra_trafficTimer    = millis();
  ra_trafficInterval = ra_rand(1500, 4000);
  ra_trafficPhase    = 0;
  ra_burstActive     = false;
  ra_burstEnd        = 0;
}

// ============================================================================
// GENERATE MOUNTAINS
// ============================================================================
static void ra_genMountains() {
  int minY = RA_HORIZON_Y / 2;
  int maxY = RA_HORIZON_Y - 1;
  for (int i = 0; i < RA_NUM_MTN; i++) {
    ra_mtnX[i] = map(i, 0, RA_NUM_MTN - 1, 0, RA_SCREEN_W - 1);
    ra_mtnY[i] = ra_rand(minY, maxY + 1);
  }
  ra_mtnReady = true;
}

// ============================================================================
// DRAW ROAD & SCENERY
// ============================================================================
static void ra_drawRoad() {
  display.clearDisplay();

  // Stars (upper half only)
  for (int i = 0; i < RA_NUM_STARS; i++)
    display.drawPixel(ra_starX[i], ra_starY[i], SSD1306_WHITE);

  // Mountains (silhouette between stars and horizon)
  if (ra_mtnReady)
    for (int i = 0; i < RA_NUM_MTN - 1; i++)
      display.drawLine(ra_mtnX[i], ra_mtnY[i], ra_mtnX[i+1], ra_mtnY[i+1], SSD1306_WHITE);

  // Horizon line
  display.drawLine(0, RA_HORIZON_Y, RA_SCREEN_W - 1, RA_HORIZON_Y, SSD1306_WHITE);

  int scrollOff = int(ra_dist * 2);

  for (int y = RA_HORIZON_Y; y < RA_SCREEN_H; y++) {
    float depth  = float(y - RA_HORIZON_Y) / float(RA_SCREEN_H - RA_HORIZON_Y);
    // Lerp between narrow and wide road widths based on ra_roadLerp (1=wide, 0=narrow)
    float wH     = RA_ROAD_W_H_NARROW + (RA_ROAD_W_H - RA_ROAD_W_H_NARROW) * ra_roadLerp;
    float wB     = RA_ROAD_W_B_NARROW + (RA_ROAD_W_B - RA_ROAD_W_B_NARROW) * ra_roadLerp;
    float halfW  = (wH / 2) * (1.0f - depth) + (wB / 2) * depth;
    float wave   = sinf(ra_dist * RA_CURVE_SPEED + depth * 1.5f) * ra_curveAmp * depth;
    float cx     = RA_SCREEN_W / 2.0f + ra_center + wave;
    int   x1     = int(cx - halfW);
    int   x2     = int(cx + halfW);

    // Road edges
    display.drawPixel(x1, y, SSD1306_WHITE);
    display.drawPixel(x2, y, SSD1306_WHITE);

    // Lane lines: 4-lane = centre + two side lines; 2-lane = centre only
    int la = int(halfW * 0.5f);
    int pl = (y - scrollOff) % 10; if (pl < 0) pl += 10;
    if (pl < 1) {
      if (ra_roadLerp > 0.3f) {
        display.drawPixel(int(cx) - la, y, SSD1306_WHITE);  // left line
        display.drawPixel(int(cx) + la, y, SSD1306_WHITE);  // right line
      }
      display.drawPixel(int(cx), y, SSD1306_WHITE);         // centre line always
    }

    // Rumble strip dots
    if (y % 5 == 0) {
      display.drawPixel(x1 - 2, y, SSD1306_WHITE);
      display.drawPixel(x2 + 2, y, SSD1306_WHITE);
    }

    // Roadside trees / posts
    for (int i = 1; i <= RA_NUM_COPIES; i++) {
      int sxL = x1 - i * 10;
      int spL = ra_dashL[i - 1];
      int pL  = (y - scrollOff) % spL; if (pL < 0) pL += spL;
      if (pL < 1) {
        display.drawLine(sxL, y, sxL, y - 3, SSD1306_WHITE);
        display.fillTriangle(sxL, y-8, sxL-2, y-3, sxL+2, y-3, SSD1306_WHITE);
      }
      int sxR = x2 + i * 10;
      int spR = ra_dashR[i - 1];
      int pR  = (y - scrollOff) % spR; if (pR < 0) pR += spR;
      if (pR < 1) {
        display.drawLine(sxR, y, sxR, y - 3, SSD1306_WHITE);
        display.fillTriangle(sxR, y-8, sxR-2, y-3, sxR+2, y-3, SSD1306_WHITE);
      }
    }
  }
}

// ============================================================================
// CAR SPRITE  (16 wide × 6 tall, 1=ON 0=OFF)
// ============================================================================
static const uint8_t ra_kCarSprite[6][16] PROGMEM = {
  { 0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0 },  // row 0
  { 1,0,1,0,0,0,0,0,0,0,0,0,0,1,0,1 },  // row 1
  { 0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0 },  // row 2
  { 1,0,0,1,0,1,1,1,1,1,1,0,1,0,0,1 },  // row 3
  { 1,0,0,1,1,1,1,1,1,1,1,1,1,0,0,1 },  // row 4  
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },  // row 5
};

// ============================================================================
// DRAW CAR (reusable)
// ============================================================================
static void ra_drawCar(bool visible) {
  if (!visible) return;
  // Sprite is 16 wide × 6 tall; anchor bottom-centre to screen bottom
  int carY = RA_SCREEN_H - 6;   // top-left Y of sprite
  int carX = int(RA_SCREEN_W / 2 + ra_playerX) - 8;  // top-left X (centred)
  for (int r = 0; r < 6; r++)
    for (int c = 0; c < 16; c++)
      if (pgm_read_byte(&ra_kCarSprite[r][c]))
        display.drawPixel(carX + c, carY + r, SSD1306_WHITE);
}

// ============================================================================
// DRAW LIVES (pixel row top-right)
// ============================================================================
static void ra_drawLives() {
  for (int i = 0; i < ra_lives; i++)
    display.drawPixel(RA_SCREEN_W - 1 - (i * 2), 0, SSD1306_WHITE);
}

// ============================================================================
// CAR HIT (edge or hazard)
// ============================================================================
static void ra_carHit() {
  ra_lives--;
  if (ra_lives <= 0) { ra_gameOver = true; return; }

  for (int i = 0; i < 3; i++) {
    ra_drawRoad();
    display.display();
    noTone(RA_BUZZER_PIN); delay(130);

    ra_drawRoad();
    ra_drawCar(true);
    display.display();
    tone(RA_BUZZER_PIN, 800, 100); delay(130);
  }
  noTone(RA_BUZZER_PIN);

  // Snap player back onto the road centre at car Y depth
  float depth   = float((RA_SCREEN_H - 2) - RA_HORIZON_Y) / float(RA_SCREEN_H - RA_HORIZON_Y);
  float wave    = sinf(ra_dist * RA_CURVE_SPEED + depth * 1.5f) * ra_curveAmp * depth;
  ra_playerX    = ra_center + wave;  // centre of road at car depth
  ra_invincible = true;
  ra_invStart   = millis();
}

// ============================================================================
// TRAFFIC HIT  (non-blocking — starts a 3-blink state machine, no delay())
// Replicates the exact off/on+beep cadence of ra_carHit() without freezing.
// ============================================================================
static void ra_trafficHit() {
  ra_lives--;
  if (ra_lives <= 0) { ra_gameOver = true; return; }

  // Kick off the blink state machine.
  // Step 0 = car OFF (silent), step 1 = car ON + beep, repeat × 3  (6 half-steps total)
  ra_trafficBlink      = true;
  ra_trafficBlinkCount = 0;
  ra_trafficBlinkShow  = false;          // start with car hidden (matches border hit)
  ra_trafficBlinkStep  = millis();
  noTone(RA_BUZZER_PIN);                 // silent on the first half-step

  // Grant invincibility so repeated hits can't stack during the blink
  ra_invincible = true;
  ra_invStart   = millis();
  // NO centre-snap: player keeps their current lateral position
}

// ============================================================================
// DRAW & UPDATE TRAFFIC CARS
// A traffic car's screen X is computed each frame from the road wave at its
// current depth, so it naturally follows curves ("rides the wave").
// ============================================================================
static void ra_updateAndDrawTraffic(bool drawOnly) {
  for (int i = 0; i < RA_MAX_TRAFFIC; i++) {
    if (!ra_traffic[i].active) continue;

    float y = ra_traffic[i].y;

    if (!drawOnly) {
      // Advance downward.  Traffic moves slower than the player scroll so it
      // appears to approach from ahead.  Add ra_speed so faster play = faster approach.
      ra_traffic[i].y += ra_speed * 0.6f + 0.5f;

      if (ra_traffic[i].y > RA_SCREEN_H + 8) {
        ra_traffic[i].active = false;
        continue;
      }
    }

    // Compute road geometry at this Y — same formula as drawRoad
    float depth = float(y - RA_HORIZON_Y) / float(RA_SCREEN_H - RA_HORIZON_Y);
    if (depth < 0.0f) depth = 0.0f;
    float wH    = RA_ROAD_W_H_NARROW + (RA_ROAD_W_H - RA_ROAD_W_H_NARROW) * ra_roadLerp;
    float wB    = RA_ROAD_W_B_NARROW + (RA_ROAD_W_B - RA_ROAD_W_B_NARROW) * ra_roadLerp;
    float halfW = (wH / 2) * (1.0f - depth) + (wB / 2) * depth;
    float wave  = sinf(ra_dist * RA_CURVE_SPEED + depth * 1.5f) * ra_curveAmp * depth;
    float roadCx = RA_SCREEN_W / 2.0f + ra_center + wave;

    // Lane centre X for this car
    float carCx = roadCx + ra_traffic[i].laneF * halfW;

    // Scale sprite: near the horizon it's tiny, at the bottom full size.
    // We use a simple 1×1 pixel dot near horizon scaling up to full 16×6 sprite.
    // depth 0 = horizon, depth 1 = bottom
    int sprW, sprH;
    if (depth < 0.0f) {
      // Far — draw as 4×2
      sprW = 4; sprH = 2;
    } else if (depth < 0.2f) {
      // Mid-distance 
      sprW = 5; sprH = 2;
    } else if (depth < 0.3f) {
      // Mid-distance 
      sprW = 6; sprH = 3;
    } else if (depth < 0.4f) {
      // Getting close — draw as 8×3
      sprW = 8; sprH = 4;
    } else {
      // Close — full sprite
      sprW = 16; sprH = 6;
    }


    int sx = int(carCx) - sprW / 2;
    int sy = int(y) - sprH;

    if (sprW < 16) {
      // Simple filled rectangle for distant cars
      display.fillRect(sx, sy, sprW, sprH, SSD1306_WHITE);
    } else {
      // Full sprite — select from the three vehicle types
      const uint8_t (*spr)[16] = ra_kTrafficSprites[ra_traffic[i].spriteIdx];
      for (int r = 0; r < 6; r++)
        for (int c = 0; c < 16; c++)
          if (pgm_read_byte(&spr[r][c]))
            display.drawPixel(sx + c, sy + r, SSD1306_WHITE);
    }

    // ── Collision with player ─────────────────────────────────────────────
    if (!drawOnly && !ra_invincible && depth >= 0.75f) {
      // Only check collision when the traffic car is large enough to matter
      float playerCx = RA_SCREEN_W / 2.0f + ra_playerX;
      int   playerY  = RA_SCREEN_H - RA_CAR_H;

      bool xOverlap = fabsf(playerCx - carCx) < (RA_HALF_BOTTOM + sprW / 2);
      bool yOverlap = (sy < playerY + RA_CAR_H) && (sy + sprH > playerY);

      if (xOverlap && yOverlap) {
        ra_traffic[i].active = false;
        ra_trafficHit();
      }
    }
  }
}
// ============================================================================
// DRAW FROZEN SCENE  (road + traffic only, no car, no HUD — used by explosion)
// ============================================================================
static void ra_drawFrozenScene() {
  ra_drawRoad();
  ra_updateAndDrawTraffic(true);   // draw-only: no movement, no collision
}

static void ra_drawPause() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(22, 22);
  display.print(" PAUSE");
  display.setTextSize(1);
  display.setCursor(0, 55);
  display.print("PLAY");
  display.setCursor(RA_SCREEN_W - 24, 55);
  display.print("EXIT");
  display.display();
}

// ============================================================================
// INTERNAL SETUP
// ============================================================================
static void racingSetup_Internal() {
  ra_level = 1;
  ra_paused            = false;
  ra_pauseNeedsRelease = false;
  ra_pauseBtn1WasDown  = false;
  ra_pauseBtn3WasDown  = false;
  ra_encStable         = HIGH;
  ra_encLastRead       = HIGH;

  ra_reset();

  // Splash screen
  if (!displayReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds("NIGHT", 0, 0, &bx, &by, &bw, &bh);
  display.setCursor((RA_SCREEN_W - bw) / 2, 8);  display.print("NIGHT");
  display.getTextBounds("DRIVE", 0, 0, &bx, &by, &bw, &bh);
  display.setCursor((RA_SCREEN_W - bw) / 2, 28); display.print("DRIVE");
  display.display();

  tone(RA_BUZZER_PIN, 523, 80); delay(100);
  tone(RA_BUZZER_PIN, 659, 80); delay(100);
  tone(RA_BUZZER_PIN, 784, 80); delay(100);
  tone(RA_BUZZER_PIN, 1047, 150); delay(500);
}

// ============================================================================
// INTERNAL LOOP
// ============================================================================
static void racingLoop_Internal() {
  unsigned long now = millis();
  if (now - ra_lastFrame < RA_FRAME_MS) return;
  ra_lastFrame = now;

  // Read MCP
  ra_mcpRead();

  // ── PAUSE TOGGLE (mirrors ObscurusApp) ────────────────────────────────────
  bool swNow = (ra_mcpGpio & (1u << 7)) ? HIGH : LOW;
  if (swNow == HIGH) ra_pauseNeedsRelease = false;

  // The OS sets g_gamePauseRequest when the encoder is pressed while
  // gameplayRunning is true. We consume it here.
  extern bool g_gamePauseRequest;
  if (g_gamePauseRequest) {
    g_gamePauseRequest = false;
    if (!ra_paused) {
      ra_paused           = true;
      ra_pauseBtn1WasDown = false;
      ra_pauseBtn3WasDown = false;
      noTone(RA_BUZZER_PIN);
    }
    ra_pauseNeedsRelease = true;
    ra_pauseIgnoreUntil  = millis() + 250;
  }

  // Also catch the encoder directly (in case the OS misses it)
  if (!ra_pauseNeedsRelease && millis() >= ra_pauseIgnoreUntil && ra_encPressed()) {
    if (!ra_paused) {
      ra_paused           = true;
      ra_pauseBtn1WasDown = false;
      ra_pauseBtn3WasDown = false;
      noTone(RA_BUZZER_PIN);
    }
    ra_pauseNeedsRelease = true;
    ra_pauseIgnoreUntil  = millis() + 250;
  }

  // ── PAUSE SCREEN ──────────────────────────────────────────────────────────
  if (ra_paused) {
    bool b1 = ra_btnDown(RA_GP_BTN1);
    bool b3 = ra_btnDown(RA_GP_BTN3);

    // BTN1 released → RESUME
    if (ra_pauseBtn1WasDown && !b1) {
      ra_pauseBtn1WasDown = false;
      ra_paused = false;
      ra_mcpRead();   // flush stale state
      return;
    } else { ra_pauseBtn1WasDown = b1; }

    // BTN3 released → EXIT to games menu
    if (ra_pauseBtn3WasDown && !b3) {
      ra_pauseBtn3WasDown = false;
      ra_paused = false;
      noTone(RA_BUZZER_PIN);
      gameExitToMenu();
      return;
    } else { ra_pauseBtn3WasDown = b3; }

    ra_drawPause();
    return;
  }

  // ── CRASH / EXPLOSION ─────────────────────────────────────────────────────
  if (ra_gameOver) {
    noTone(RA_BUZZER_PIN);

    // ── Phase 1: kick off the explosion on the first frame ──────────────────
    if (!ra_exploding) {
      ra_exploding    = true;
      ra_explodeStart = millis();

      // Capture the car's screen centre (bottom-centre of sprite)
      ra_explodeCX = int(RA_SCREEN_W / 2.0f + ra_playerX);
      ra_explodeCY = RA_SCREEN_H - RA_CAR_H / 2;   // vertical centre of sprite row

      // Play the IronTides-style explosion: 8 rapid low-frequency bursts
      for (int c = 0; c < 8; c++) {
        tone(RA_BUZZER_PIN, 50 + ra_rand(0, 75), 20);
        delay(30);
      }
      noTone(RA_BUZZER_PIN);
    }

    // ── Phase 2: animate the disintegrating pixel cloud ─────────────────────
    unsigned long elapsed = millis() - ra_explodeStart;

    // Draw frozen road scene (no HUD, no car)
    ra_drawFrozenScene();

    // Shrinking pixel cloud centred on the car — mirrors IronTides exactly:
    // starts at 35 pixels, loses one pixel every 25ms, reaches 0 at ~875ms
    int remaining = max(0, 35 - (int)(elapsed / 25));
    for (int p = 0; p < remaining; p++) {
      int rx = ra_rand(-4, 5);
      int ry = ra_rand(-4, 5);
      int px = ra_explodeCX + rx;
      int py = ra_explodeCY + ry;
      if (px >= 0 && px < RA_SCREEN_W && py >= 0 && py < RA_SCREEN_H)
        display.drawPixel(px, py, SSD1306_WHITE);
    }

    display.display();

    if (elapsed < RA_EXPLODE_MS) return;   // keep animating

    // ── Phase 3: explosion done — show "YOU CRASHED" then reset ─────────────
    ra_exploding = false;
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    int16_t bx, by; uint16_t w1, h1, w2, h2;
    display.getTextBounds("YOU",     0, 0, &bx, &by, &w1, &h1);
    display.getTextBounds("CRASHED", 0, 0, &bx, &by, &w2, &h2);
    int x1p = (RA_SCREEN_W - w1) / 2;
    int x2p = (RA_SCREEN_W - w2) / 2;
    int yM  = RA_SCREEN_H / 2;
    for (int i = 0; i < 3; i++) {
      display.clearDisplay();
      display.setCursor(x1p, yM - h1); display.print("YOU");
      display.setCursor(x2p, yM);      display.print("CRASHED");
      display.display(); tone(RA_BUZZER_PIN, 600, 150); delay(150);
      display.clearDisplay(); display.display(); noTone(RA_BUZZER_PIN); delay(150);
    }
    // Hold final frame
    display.clearDisplay();
    display.setCursor(x1p, yM - h1); display.print("YOU");
    display.setCursor(x2p, yM);      display.print("CRASHED");
    display.display(); delay(1500);

    ra_level = 1;
    ra_reset();
    return;
  }

  // ── MOUNTAINS (generated once per reset) ──────────────────────────────────
  if (!ra_mtnReady) ra_genMountains();

  // ── NIGHT DRIVE TITLE CARD (3 s on level 1 start) ────────────────────────
  if (ra_introActive) {
    if (now - ra_introStart < 3000) {
      ra_drawRoad();
      display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);
      int16_t bx, by; uint16_t w1, h1, w2, h2;
      display.getTextBounds("NIGHT", 0, 0, &bx, &by, &w1, &h1);
      display.getTextBounds("DRIVE", 0, 0, &bx, &by, &w2, &h2);
      int startY = (RA_SCREEN_H - int(h1 + h2 + 4)) / 2;
      display.setCursor((RA_SCREEN_W - w1) / 2, startY);         display.print("NIGHT");
      display.setCursor((RA_SCREEN_W - w2) / 2, startY + h1 + 4); display.print("DRIVE");
      display.display();
      return;
    }
    ra_introActive = false;
    ra_cdTimer     = now;
    ra_cdLast      = -1;
  }

  // ── COUNTDOWN (3 … 2 … 1 … GO) ───────────────────────────────────────────
  if (!ra_started) {
    ra_drawRoad();
    ra_drawCar(true);
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(RA_SCREEN_W / 2 - 6, RA_SCREEN_H / 2 - 8);
    if (ra_countdown > 0) display.print(ra_countdown);
    else                  display.print("GO");
    display.display();

    if (ra_cdLast != ra_countdown) {
      ra_cdLast = ra_countdown;
      tone(RA_BUZZER_PIN, ra_countdown > 0 ? 500 : 800, 200);
    }
    if (now - ra_cdTimer >= 1000) {
      ra_cdTimer = now;
      ra_countdown--;
      if (ra_countdown < 0) ra_started = true;
    }
    return;
  }

  // ── STEERING (nav switch left / right) ───────────────────────────────────
  if (ra_btnDown(RA_GP_NAV_L)) ra_playerX -= 3.0f;
  if (ra_btnDown(RA_GP_NAV_R)) ra_playerX += 3.0f;

  // ── SPEED  (BTN1 = accelerate; BTN3 = brake; release = coast to cruise) ──
  static const float RA_SPEED_CRUISE = 1.0f;   // idle / coasting speed
  static const float RA_SPEED_MAX    = 5.5f + (ra_level - 1) * 1.0f;
  static const float RA_ACCEL        = 0.05f;  // speed gained per frame while held
  static const float RA_DECEL        = 0.02f;  // speed lost per frame when released
  static const float RA_BRAKE        = 0.12f;  // speed lost per frame when braking

  bool accel = ra_btnDown(RA_GP_BTN1);
  bool brake = ra_btnDown(RA_GP_BTN3) && !accel;  // no brake+gas together

  if (accel) {
    ra_speed += RA_ACCEL;
    if (ra_speed > RA_SPEED_MAX) ra_speed = RA_SPEED_MAX;
  } else if (brake) {
    ra_speed -= RA_BRAKE;
    if (ra_speed < RA_SPEED_CRUISE) ra_speed = RA_SPEED_CRUISE;
  } else {
    if (ra_speed > RA_SPEED_CRUISE) {
      ra_speed -= RA_DECEL;
      if (ra_speed < RA_SPEED_CRUISE) ra_speed = RA_SPEED_CRUISE;
    }
  }

  // Continuous engine drone (replaced every frame — non-blocking)
  tone(RA_BUZZER_PIN, int(10 + ra_speed * 40));

  ra_playerX = constrain(ra_playerX,
    -(RA_SCREEN_W / 2.0f - RA_HALF_BOTTOM),
     (RA_SCREEN_W / 2.0f - RA_HALF_BOTTOM));

  // ── DRIFT ─────────────────────────────────────────────────────────────────
  if (now - ra_driftTimer >= ra_driftInterval) {
    ra_driftTimer    = now;
    ra_driftInterval = ra_rand(500, 2000);
    ra_driftV        = ra_rand(-int(ra_maxDrift * 10), int(ra_maxDrift * 10) + 1) / 10.0f;
  }
  ra_center = constrain(ra_center + ra_driftV,
    -(RA_SCREEN_W / 2.0f - RA_ROAD_W_B / 2),
     (RA_SCREEN_W / 2.0f - RA_ROAD_W_B / 2));

  ra_dist += ra_speed;

  // Gradual difficulty ramp
  if (ra_curveAmp < 30.0f)  ra_curveAmp += 0.0005f;
  if (ra_maxDrift < 3.0f)   ra_maxDrift += 0.00005f;

  // ── NARROW ZONE STATE MACHINE ────────────────────────────────────────────
  // Trigger once per level when ra_track counts down to ra_narrowTrigger.
  // IDLE → NARROWING (lerp 1→0 over 2.5s) → NARROW (hold 7s) → WIDENING (lerp 0→1 over 2.5s) → IDLE
  {
    if (ra_narrowState == RA_NZ_IDLE && ra_track <= ra_narrowTrigger && ra_narrowTrigger > 0) {
      ra_narrowState = RA_NZ_NARROWING;
      ra_narrowStart = now;
      ra_narrowTrigger = 0;   // prevent re-trigger
    }
    if (ra_narrowState == RA_NZ_NARROWING) {
      float t = float(now - ra_narrowStart) / float(RA_NZ_TRANSITION_MS);
      if (t >= 1.0f) { t = 1.0f; ra_narrowState = RA_NZ_NARROW; ra_narrowStart = now; }
      ra_roadLerp = 1.0f - t;   // wide → narrow
    }
    if (ra_narrowState == RA_NZ_NARROW) {
      ra_roadLerp = 0.0f;
      if (now - ra_narrowStart >= RA_NZ_HOLD_MS) {
        ra_narrowState = RA_NZ_WIDENING;
        ra_narrowStart = now;
      }
    }
    if (ra_narrowState == RA_NZ_WIDENING) {
      float t = float(now - ra_narrowStart) / float(RA_NZ_TRANSITION_MS);
      if (t >= 1.0f) { t = 1.0f; ra_narrowState = RA_NZ_IDLE; }
      ra_roadLerp = t;   // narrow → wide
    }
  }

  // ── SPAWN TRAFFIC ─────────────────────────────────────────────────────────
  // Three milestone bursts (start, 50%, 75%) each fire a dense wave for
  // RA_BURST_MS ms, then settle to a permanently denser calm baseline.
  //
  //  Phase 0 → 1 (start burst):   burst 300-600 ms,  calm 1800-3500 ms
  //  Phase 1 → 2 (50% burst):     burst 250-500 ms,  calm 1200-2500 ms
  //  Phase 2 → 3 (75% burst):     burst 200-400 ms,  calm  800-1800 ms
  {
    unsigned long elapsed = ra_trackMax - ra_track;

    // Detect milestone transitions and start a burst
    if (ra_trafficPhase == 0) {
      // Immediate burst at level start
      ra_trafficPhase = 1;
      ra_burstActive  = true;
      ra_burstEnd     = now + RA_BURST_MS;
    } else if (ra_trafficPhase == 1 && elapsed >= ra_trackMax / 2) {
      ra_trafficPhase = 2;
      ra_burstActive  = true;
      ra_burstEnd     = now + RA_BURST_MS;
    } else if (ra_trafficPhase == 2 && elapsed >= ra_trackMax * 3 / 4) {
      ra_trafficPhase = 3;
      ra_burstActive  = true;
      ra_burstEnd     = now + RA_BURST_MS;
    }

    // Check if burst window has expired
    if (ra_burstActive && now >= ra_burstEnd) ra_burstActive = false;
  }

  // Pick spawn interval based on phase + whether we're mid-burst
  unsigned long trafficLo, trafficHi;
  if (ra_burstActive) {
    // Dense burst intervals
    if      (ra_trafficPhase == 1) { trafficLo = 300;  trafficHi = 600;  }
    else if (ra_trafficPhase == 2) { trafficLo = 250;  trafficHi = 500;  }
    else                           { trafficLo = 200;  trafficHi = 400;  }
  } else {
    // Calm baseline — each phase permanently denser than the previous
    if      (ra_trafficPhase <= 1) { trafficLo = 1800; trafficHi = 3500; }
    else if (ra_trafficPhase == 2) { trafficLo = 1200; trafficHi = 2500; }
    else                           { trafficLo = 800;  trafficHi = 1800; }
  }
  if (now - ra_trafficTimer >= ra_trafficInterval) {
    // Find a free slot
    for (int i = 0; i < RA_MAX_TRAFFIC; i++) {
      if (!ra_traffic[i].active) {
        // During narrow zone use only the 2 inner lanes; otherwise all 4
        float laneF;
        if (ra_narrowState != RA_NZ_IDLE) {
          laneF = RA_LANE_FRACS_NARROW[ra_rand(0, 2)];
        } else {
          laneF = RA_LANE_FRACS[ra_rand(0, 4)];
        }
        ra_traffic[i].laneF     = laneF;
        ra_traffic[i].y         = float(RA_HORIZON_Y);
        ra_traffic[i].distSnap  = ra_dist;
        ra_traffic[i].active    = true;
        ra_traffic[i].spriteIdx = (uint8_t)ra_rand(0, 3);  // 0, 1, or 2
        break;
      }
    }
    ra_trafficTimer    = now;
    ra_trafficInterval = ra_rand(trafficLo, trafficHi);
  }

  // ── DRAW ROAD ─────────────────────────────────────────────────────────────
  ra_drawRoad();

  // ── UPDATE & DRAW TRAFFIC ─────────────────────────────────────────────────
  ra_updateAndDrawTraffic();

  // ── DRAW CAR (blink while invincible) ────────────────────────────────────
  // Traffic hit: advance the 3-blink state machine each half-step (130ms).
  //   Even half-steps (0,2,4) → car hidden, silent.
  //   Odd  half-steps (1,3,5) → car visible, beep 600Hz.
  // After 6 half-steps the sequence ends; normal invincibility blink takes over.
  if (ra_trafficBlink) {
    if (now - ra_trafficBlinkStep >= RA_TRAFFIC_BLINK_HALF) {
      ra_trafficBlinkStep = now;
      ra_trafficBlinkCount++;
      if (ra_trafficBlinkCount >= 6) {
        // All 3 blinks done — also clear invincibility so traffic hits register again
        ra_trafficBlink = false;
        ra_invincible   = false;
        noTone(RA_BUZZER_PIN);
        ra_trafficBlinkShow = true;   // leave car visible when sequence ends
      } else {
        ra_trafficBlinkShow = (ra_trafficBlinkCount % 2 != 0);  // odd = visible
        if (ra_trafficBlinkShow) {
          tone(RA_BUZZER_PIN, 600, 100);
        } else {
          noTone(RA_BUZZER_PIN);
        }
      }
    }
  }
  bool carVisible;
  if (ra_trafficBlink) {
    carVisible = ra_trafficBlinkShow;
  } else {
    carVisible = !ra_invincible || ((now / 200) % 2) == 0;  // border-hit / normal blink
  }
  ra_drawCar(carVisible);

  // ── ROAD EDGE BARRIER (hard clamp — no damage, just stops the player) ────
  {
    float depth   = float((RA_SCREEN_H - 2) - RA_HORIZON_Y) / float(RA_SCREEN_H - RA_HORIZON_Y);
    float wH      = RA_ROAD_W_H_NARROW + (RA_ROAD_W_H - RA_ROAD_W_H_NARROW) * ra_roadLerp;
    float wB      = RA_ROAD_W_B_NARROW + (RA_ROAD_W_B - RA_ROAD_W_B_NARROW) * ra_roadLerp;
    float halfW   = (wH / 2) * (1.0f - depth) + (wB / 2) * depth;
    float wave    = sinf(ra_dist * RA_CURVE_SPEED + depth * 1.5f) * ra_curveAmp * depth;
    float roadCx  = RA_SCREEN_W / 2.0f + ra_center + wave;
    // Clamp player so the car sprite never crosses either edge
    float minX = (roadCx - halfW) - RA_SCREEN_W / 2.0f + RA_HALF_BOTTOM;
    float maxX = (roadCx + halfW) - RA_SCREEN_W / 2.0f - RA_HALF_BOTTOM;
    if (ra_playerX < minX) ra_playerX = minX;
    if (ra_playerX > maxX) ra_playerX = maxX;
    if (ra_invincible && !ra_trafficBlink && now - ra_invStart > 1500) ra_invincible = false;
  }

  // ── TRACK COUNTDOWN & LEVEL PROGRESSION ──────────────────────────────────
  if (ra_track > 0) {
    ra_track--;
  } else {
    noTone(RA_BUZZER_PIN);
    if (ra_level >= 3) {
      // FINISH!
      display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);
      int16_t bx, by; uint16_t bw, bh;
      display.getTextBounds("FINISH!", 0, 0, &bx, &by, &bw, &bh);
      int xp = (RA_SCREEN_W - bw) / 2, yp = (RA_SCREEN_H - bh) / 2;
      for (int i = 0; i < 3; i++) {
        display.clearDisplay();
        display.setCursor(xp, yp); display.print("FINISH!");
        display.display(); tone(RA_BUZZER_PIN, 1000, 200); delay(200);
        display.clearDisplay(); display.display(); noTone(RA_BUZZER_PIN); delay(200);
      }
      display.clearDisplay();
      display.setTextSize(2); display.setCursor(xp, yp); display.print("FINISH!");
      display.display(); delay(3000);

      ra_level = 1;
      ra_reset();
    } else {
      // TRACK CLEARED
      display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);
      int16_t bx, by; uint16_t tw1, th1, tw2, th2;
      display.getTextBounds("TRACK",   0, 0, &bx, &by, &tw1, &th1);
      display.getTextBounds("CLEARED", 0, 0, &bx, &by, &tw2, &th2);
      int yMid = RA_SCREEN_H / 2;
      for (int i = 0; i < 3; i++) {
        display.clearDisplay();
        display.setCursor((RA_SCREEN_W - tw1) / 2, yMid - th1 - 2); display.print("TRACK");
        display.setCursor((RA_SCREEN_W - tw2) / 2, yMid + 2);       display.print("CLEARED");
        display.display(); tone(RA_BUZZER_PIN, 1000, 200); delay(200);
        display.clearDisplay(); display.display(); noTone(RA_BUZZER_PIN); delay(200);
      }
      display.clearDisplay();
      display.setCursor((RA_SCREEN_W - tw1) / 2, yMid - th1 - 2); display.print("TRACK");
      display.setCursor((RA_SCREEN_W - tw2) / 2, yMid + 2);       display.print("CLEARED");
      display.display(); delay(3000);

      ra_level++;
      ra_speed    += 1.0f;
      ra_curveAmp += 10.0f;
      ra_maxDrift += 0.5f;
      ra_reset();
    }
    return;
  }

  // Accumulate distance (scale ra_speed → mph range 40–120 → miles per frame)
  // ra_speed range: 1.0 (cruise) to 6.5 (max lvl3).  Map to 40–120 mph.
  float mph = 40.0f + (ra_speed - 1.0f) / (6.5f - 2.0f) * 80.0f;
  if (mph < 40.0f)  mph = 30.0f;
  if (mph > 120.0f) mph = 120.0f;
  // miles per frame: mph / 3600 / (1000/RA_FRAME_MS)  ≈ mph * RA_FRAME_MS / 3600000
  ra_odometer += mph * RA_FRAME_MS / 3600000.0f;
  if (ra_odometer > 99.9f) ra_odometer = 99.9f;

  // ── HUD ───────────────────────────────────────────────────────────────────
  // Lives (top-right pixel dots)
  ra_drawLives();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // ── TOP LEFT: speedometer ─────────────────────────────────────────────────
  char spdBuf[8];
  snprintf(spdBuf, sizeof(spdBuf), "%3d MPH", int(mph));
  display.setCursor(0, 0);
  display.print(spdBuf);

  // ── BELOW SPEEDO: timer in sss:ms format ──────────────────────────────────
  unsigned long elapsed = millis() - ra_levelStart;
  unsigned long secs    = elapsed / 1000;
  unsigned long ms2     = (elapsed % 1000) / 10;   // 2-digit centiseconds
  if (secs > 999) secs = 999;
  char timeBuf[12];
  snprintf(timeBuf, sizeof(timeBuf), "T%03lu:%02lu", secs, ms2);
  display.setCursor(0, 9);
  display.print(timeBuf);

  // ── TOP RIGHT: distance ───────────────────────────────────────────────────
  // "DIST:" on first row, "xx.x mi" below — right-aligned
  char distLabel[] = "DIST:";
  char distVal[10];
  snprintf(distVal, sizeof(distVal), "%4.1fmi", ra_odometer);
  int labelX = RA_SCREEN_W - int(strlen(distLabel)) * 6;
  int valX   = RA_SCREEN_W - int(strlen(distVal))   * 6;
  display.setCursor(labelX, 0); display.print(distLabel);
  display.setCursor(valX,   9); display.print(distVal);

  display.display();
}

// ============================================================================
// PUBLIC WATCHOS API
// ============================================================================

void racingEnter() {
  ra_active = true;
  // Always do a full reset when entering — fresh race every time.
  racingSetup_Internal();
  ra_setupRan = true;
}

void racingUpdate() {
  if (!ra_active) return;
  racingLoop_Internal();
}

void racingExit() {
  ra_active = false;
  noTone(RA_BUZZER_PIN);
}