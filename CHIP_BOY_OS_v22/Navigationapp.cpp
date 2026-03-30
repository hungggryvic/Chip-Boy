#include "NavigationApp.h"
#include "LocationApp.h"
#include <Adafruit_SSD1306.h>
#include <math.h>
#include <Preferences.h>

// ============================================================================
// NAVIGATION APP IMPLEMENTATION
// ============================================================================

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Globals from main
extern bool inMenu;
extern bool uiDirty;
extern unsigned long menuEnableButtonsAt;
extern bool screenEnabled;
extern bool displayReady;
extern Adafruit_SSD1306 display;

// BNO055
#define sensor_t adafruit_sensor_t
#include <Adafruit_BNO055.h>
#undef sensor_t
extern Adafruit_BNO055 bno;
#define CAL_OFFSET_DEG 5.0
float wrap360(float a);

extern bool alarmRinging;
extern bool chimeActive;
extern bool tonePlaying;
extern bool buzzing;
void buzzerStartNav();

struct BtnState;
extern BtnState b1, b2, b3, bUp, bDown, bRight, bLeft;
bool btnWasShortPressed(const BtnState &b_in);

void redrawMenuNow();

#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH 128
#endif
#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT 64
#endif

// ── Module globals ────────────────────────────────────────────────────────

bool inNavigation = false;

enum NavStage {
  NAV_ENTER_LAT,
  NAV_ENTER_LON,
  NAV_ROUND_TRIP_PROMPT,   // "Round trip?" confirmation screen
  NAV_RUNNING,
  NAV_RETURN_PROMPT        // "Return now?" confirmation screen
};
static NavStage navStage = NAV_ENTER_LAT;

static bool navTripActive  = false;
static bool navEndPrompt   = false;
static bool navImperial    = false;   // false = metric (km/m), true = imperial (mi/ft)

// Round-trip state
static bool   navRoundTrip      = false;  // true while outbound leg is active
static double navSavedLat       = 0.0;   // origin coords saved at round-trip start
static double navSavedLon       = 0.0;
static bool   navOriginCaptured = false;  // true if we got a real GPS fix for origin

static String navLatStr = "";
static String navLonStr = "";

static double navTargetLat = 0.0;
static double navTargetLon = 0.0;

static const int NAV_KP_X    = 0;
static const int NAV_KP_Y    = 28;
static const int NAV_KP_W    = 32;
static const int NAV_KP_H    = 9;
static const int NAV_KP_COLS = 4;
static const int NAV_KP_ROWS = 4;

static const char* NAV_KEYS[NAV_KP_ROWS][NAV_KP_COLS] = {
  {"7","8","9","DEL"},
  {"4","5","6","CLR"},
  {"1","2","3","+/-"},
  {"0",".","ENT","EXT"}
};

static int      navCursorRow   = 0;
static int      navCursorCol   = 0;
static uint32_t navLastDrawMs  = 0;
static uint32_t navNextBeepAt  = 0;
static uint32_t navBeepPeriodMs = 20000;

// ── NVS persistence ───────────────────────────────────────────────────────
static Preferences navPrefs;

static void navSavePrefs() {
  navPrefs.begin("nav", false);
  navPrefs.putDouble("targetLat",  navTargetLat);
  navPrefs.putDouble("targetLon",  navTargetLon);
  navPrefs.putDouble("savedLat",   navSavedLat);
  navPrefs.putDouble("savedLon",   navSavedLon);
  navPrefs.putBool("tripActive",   navTripActive);
  navPrefs.putBool("roundTrip",    navRoundTrip);
  navPrefs.putBool("imperial",     navImperial);
  navPrefs.putBool("origCaptured", navOriginCaptured);
  navPrefs.end();
}

static void navLoadPrefs() {
  navPrefs.begin("nav", true);
  navTargetLat      = navPrefs.getDouble("targetLat",  0.0);
  navTargetLon      = navPrefs.getDouble("targetLon",  0.0);
  navSavedLat       = navPrefs.getDouble("savedLat",   0.0);
  navSavedLon       = navPrefs.getDouble("savedLon",   0.0);
  navTripActive     = navPrefs.getBool("tripActive",   false);
  navRoundTrip      = navPrefs.getBool("roundTrip",    false);
  navImperial       = navPrefs.getBool("imperial",     false);
  navOriginCaptured = navPrefs.getBool("origCaptured", false);
  navPrefs.end();
}

static void navClearPrefs() {
  navPrefs.begin("nav", false);
  navPrefs.clear();
  navPrefs.end();
}

// ── Helpers ───────────────────────────────────────────────────────────────

static inline double deg2rad(double d) { return d * (M_PI / 180.0); }
static inline double rad2deg(double r) { return r * (180.0 / M_PI); }

double navDistanceMeters(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;
  double p1 = deg2rad(lat1), p2 = deg2rad(lat2);
  double dp = deg2rad(lat2 - lat1), dl = deg2rad(lon2 - lon1);
  double a = sin(dp/2)*sin(dp/2) + cos(p1)*cos(p2)*sin(dl/2)*sin(dl/2);
  return R * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

double navBearingDeg(double lat1, double lon1, double lat2, double lon2) {
  double p1 = deg2rad(lat1), p2 = deg2rad(lat2);
  double dl = deg2rad(lon2 - lon1);
  double y  = sin(dl) * cos(p2);
  double x  = cos(p1)*sin(p2) - sin(p1)*cos(p2)*cos(dl);
  double brng = rad2deg(atan2(y, x));
  while (brng <   0) brng += 360.0;
  while (brng >= 360) brng -= 360.0;
  return brng;
}

// Metric thresholds (meters)
static uint32_t navBeepPeriodMetric(double distM) {
  if (distM <=    10.0) return     50;
  if (distM <=    25.0) return    125;
  if (distM <=    50.0) return    250;
  if (distM <=   100.0) return    500;
  if (distM <=   250.0) return    750;
  if (distM <=   500.0) return   1000;
  if (distM <=  1000.0) return   1500;
  if (distM <=  2500.0) return   2500;
  if (distM <=  5000.0) return   5000;
  if (distM <= 10000.0) return  10000;
  return 20000;
}

// Imperial thresholds
static uint32_t navBeepPeriodImperial(double distM) {
  double distFt = distM * 3.28084;
  if (distFt <=    30.0) return     50;
  if (distFt <=    80.0) return    125;
  if (distFt <=   165.0) return    250;
  if (distFt <=   330.0) return    500;
  if (distFt <=   825.0) return    750;
  if (distFt <=  1650.0) return   1000;
  if (distFt <=  5280.0) return   1500;
  if (distFt <= 13200.0) return   2500;
  if (distFt <= 26400.0) return   5000;
  if (distFt <= 52800.0) return  10000;
  return 20000;
}

static uint32_t navBeepPeriodForDistance(double distM) {
  return navImperial ? navBeepPeriodImperial(distM)
                     : navBeepPeriodMetric(distM);
}

static void serviceNavigationBeep() {
  if (!inNavigation || navStage != NAV_RUNNING || navEndPrompt) return;
  if (alarmRinging || chimeActive || tonePlaying) return;
  if (!gps.location.isValid()) return;

  double distM = navDistanceMeters(
    gps.location.lat(), gps.location.lng(), navTargetLat, navTargetLon);

  uint32_t desired = navBeepPeriodForDistance(distM);
  uint32_t now = millis();

  if (desired != navBeepPeriodMs) {
    navBeepPeriodMs  = desired;
    navNextBeepAt    = now + navBeepPeriodMs;
  }
  if (navNextBeepAt == 0) navNextBeepAt = now + navBeepPeriodMs;
  if (!buzzing && now >= navNextBeepAt) {
    buzzerStartNav();
    navNextBeepAt = now + navBeepPeriodMs;
  }
}

// ── Draw ──────────────────────────────────────────────────────────────────

static void drawNavKeypadAndInput(const char* title, const String& value) {
  if (!displayReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(title);
  display.setCursor(0, 12);
  display.print(value);

  for (int r = 0; r < NAV_KP_ROWS; r++) {
    for (int c = 0; c < NAV_KP_COLS; c++) {
      int x = NAV_KP_X + c * NAV_KP_W;
      int y = NAV_KP_Y + r * NAV_KP_H;
      const char* label = NAV_KEYS[r][c];
      int w = strlen(label) * 6;
      int tx = x + ((NAV_KP_W - w) / 2);
      if (r == navCursorRow && c == navCursorCol) {
        display.setCursor(tx - 6, y); display.print("[");
        display.setCursor(tx, y);     display.print(label);
        display.setCursor(tx + w - 1, y); display.print("]");
      } else {
        display.setCursor(tx, y); display.print(label);
      }
    }
  }
  display.display();
}

// "Round trip?" prompt
// BTN1 = YES (bottom-left), BTN2 = NO (bottom-right)
// Also shows the captured origin coords to verify before confirming
static void drawNavRoundTripPrompt() {
  if (!displayReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 12); display.print("Round trip?");
  // Show captured origin so the user can verify it looks right
  display.setCursor(0, 24);
  if (navOriginCaptured) {
    display.print("C:");
    display.print(navSavedLat, 5);
    display.print(",");
    display.print(navSavedLon, 5);
    display.print("T:");
    display.print(navTargetLat, 5);
    display.print(",");
    display.print(navTargetLon, 5);
  } else {
    display.print("No GPS fix yet");
  }
  display.setCursor(0,  55); display.print("YES");
  display.setCursor(110,55); display.print("NO");
  display.display();
}

static void drawNavigationRunning() {
  if (!displayReady) return;
  while (GPSSerial.available()) gps.encode(GPSSerial.read());

  sensors_event_t ori;
  bno.getEvent(&ori, Adafruit_BNO055::VECTOR_EULER);
  float rawHeading = wrap360(ori.orientation.x);
  float heading    = wrap360(rawHeading - CAL_OFFSET_DEG);
  heading          = wrap360(heading + 180.0f);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  bool   gpsOk  = gps.location.isValid();
  double curLat = gpsOk ? gps.location.lat() : 0.0;
  double curLon = gpsOk ? gps.location.lng() : 0.0;
  double distM  = 0.0, brng = 0.0;

  if (gpsOk) {
    distM = navDistanceMeters(curLat, curLon, navTargetLat, navTargetLon);
    brng  = navBearingDeg(curLat, curLon, navTargetLat, navTargetLon);
  }

  // ── Distance display ─────────────────────────────────────────────────
  display.setCursor(0, 0);
  display.print("Dist:");
  if (gpsOk) {
    if (!navImperial) {
      if (distM >= 1000.0) {
        display.print(distM / 1000.0, 2);
        display.print("km");
      } else {
        display.print((int)distM);
        display.print("m");
      }
    } else {
      double distFt = distM * 3.28084;
      double distMi = distM / 1609.344;
      if (distFt >= 1056.0) {
        display.print(distMi, 2);
        display.print("mi");
      } else {
        display.print((int)distFt);
        display.print("ft");
      }
    }
  } else {
    display.print("----");
  }

  // ── Current / target coords ──────────────────────────────────────────
  display.setCursor(0, 10);
  if (gpsOk) {
    display.print("C:");
    display.print(curLat, 5);
    display.print(",");
    display.print(curLon, 5);
  } else {
    display.print("C: waiting GPS");
  }

  display.setCursor(0, 18);
  display.print("T:");
  display.print(navTargetLat, 5);
  display.print(",");
  display.print(navTargetLon, 5);

  // ── Compass rose ─────────────────────────────────────────────────────
  int cx = SCREEN_WIDTH / 2, cy = 45, radius = 16;
  display.drawCircle(cx, cy, radius, WHITE);
  int o = radius + 2;
  display.setCursor(cx - 3, cy - o + 3);
  display.print("N");

  float ang  = (-200.0f - heading) * (M_PI / 180.0f);
  int   tipX = cx + (int)(radius * cos(ang));
  int   tipY = cy - (int)(radius * sin(ang));
  display.drawLine(cx, cy, tipX, tipY, WHITE);

  if (gpsOk) {
    double rel = brng - (double)heading;
    while (rel < -180.0) rel += 360.0;
    while (rel >  180.0) rel -= 360.0;
    double theta = deg2rad(3.0 - rel);
    int ax = cx + (int)(radius * cos(theta));
    int ay = cy - (int)(radius * sin(theta));
    display.fillCircle(ax, ay, 2, WHITE);
  }

  // ── Footer ────────────────────────────────────────────────────────────
  // BTN1 = ARRIVE (round-trip outbound) or END (normal/return leg)
  // BTN2 = unit toggle (silent)
  // BTN3 = EXIT to main menu
  display.setCursor(0,  55);
  display.print(navRoundTrip ? "RTN" : "END");
  display.setCursor(104,55); display.print("EXIT");

  display.display();
}

static void drawNavEndPrompt() {
  if (!displayReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0,  26); display.print("End navigation?");
  display.setCursor(0,  55); display.print("YES");
  display.setCursor(110,55); display.print("NO");
  display.display();
}

// "Return now?" prompt — BTN1 = YES, BTN3 = NO
static void drawNavReturnPrompt() {
  if (!displayReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 20); display.print("Return now?");
  display.setCursor(0,  55); display.print("YES");
  display.setCursor(110,55); display.print("NO");
  display.display();
}

// ── App lifecycle ─────────────────────────────────────────────────────────

void navigationEnter() {
  // Load persisted state first — navTripActive must be correct before branching
  navLoadPrefs();

  inMenu       = false;
  inNavigation = true;
  uiDirty      = true;
  navCursorRow = 0;
  navCursorCol = 0;
  navLastDrawMs   = 0;
  navNextBeepAt   = 0;
  navBeepPeriodMs = 20000;
  navEndPrompt    = false;

  if (!navTripActive) {
    // No saved trip — start fresh
    navStage          = NAV_ENTER_LAT;
    navLatStr         = "";
    navLonStr         = "";
    navTargetLat      = 0.0;
    navTargetLon      = 0.0;
    navRoundTrip      = false;
    navSavedLat       = 0.0;
    navSavedLon       = 0.0;
    navOriginCaptured = false;
  } else {
    // Resume mid-trip exactly where we left off
    navStage = NAV_RUNNING;
  }

  gps = TinyGPSPlus();
  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
}

void navigationExit() {
  GPSSerial.end();
  navNextBeepAt = 0;
  inNavigation  = false;
  inMenu        = true;
  uiDirty       = true;
  menuEnableButtonsAt = millis() + 120;
  redrawMenuNow();
}

static bool navParseAndClamp(const String& s, bool isLat, double &outVal) {
  if (s.length() == 0) return false;
  double v = s.toFloat();
  if (isLat) { if (v < -90.0  || v > 90.0)  return false; }
  else        { if (v < -180.0 || v > 180.0) return false; }
  outVal = v;
  return true;
}

static void navAppendSignToggle(String &s) {
  if (s.startsWith("-")) s.remove(0, 1);
  else s = "-" + s;
}

void handleNavigation() {
  bool p1 = btnWasShortPressed(b1);
  bool p2 = btnWasShortPressed(b2);
  bool p3 = btnWasShortPressed(b3);

  // BTN3 exits to main menu from anywhere (cancels prompts too)
  if (p3) {
    if (navEndPrompt)                      { navEndPrompt = false; uiDirty = true; return; }
    if (navStage == NAV_RETURN_PROMPT)     { navStage = NAV_RUNNING; uiDirty = true; return; }
    if (navStage == NAV_ROUND_TRIP_PROMPT) { /* fall through to exit */ }
    navigationExit();
    return;
  }

  // ── Round-trip prompt ─────────────────────────────────────────────────
  if (navStage == NAV_ROUND_TRIP_PROMPT) {
    // Keep draining GPS while prompt is showing — origin fix can only improve
    while (GPSSerial.available()) gps.encode(GPSSerial.read());
    if (gps.location.isValid()) {
      navSavedLat       = gps.location.lat();
      navSavedLon       = gps.location.lng();
      navOriginCaptured = true;
      uiDirty           = true;  // refresh display to show updated coords
    }

    if (p1) {
      // YES — start round-trip navigation with the best origin we have
      navRoundTrip  = true;
      navTripActive = true;
      navStage      = NAV_RUNNING;
      navSavePrefs();

      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      display.setTextSize(1);
      display.setCursor(0, 26);
      if (!navOriginCaptured) {
        display.print("No GPS fix! Org=0,0");
      } else {
        display.print("Starting navigation");
      }
      display.display();
      delay(450);
      uiDirty = true;
      return;
    }

    if (p2) {
      // NO — start normal (non-round-trip) navigation
      navRoundTrip      = false;
      navOriginCaptured = false;
      navSavedLat       = 0.0;
      navSavedLon       = 0.0;
      navTripActive     = true;
      navStage          = NAV_RUNNING;
      navSavePrefs();

      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      display.setTextSize(1);
      display.setCursor(0, 26);
      display.print("Starting navigation");
      display.display();
      delay(450);
      uiDirty = true;
      return;
    }

    // No button — draw prompt and wait
    if (!screenEnabled || !displayReady) return;
    uint32_t now = millis();
    if (!uiDirty && (now - navLastDrawMs) < 120) return;
    navLastDrawMs = now;
    uiDirty = false;
    drawNavRoundTripPrompt();
    return;
  }

  // ── "Return now?" prompt ──────────────────────────────────────────────
  if (navStage == NAV_RETURN_PROMPT) {
    if (p1) {
      // YES — flip target to saved origin and start the return leg
      navTargetLat    = navSavedLat;
      navTargetLon    = navSavedLon;
      navRoundTrip    = false;
      navTripActive   = true;
      navBeepPeriodMs = 0;
      navNextBeepAt   = 0;
      navStage        = NAV_RUNNING;
      navSavePrefs();

      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      display.setTextSize(1);
      display.setCursor(0, 26);
      display.print("Returning home...");
      display.display();
      delay(450);
      uiDirty = true;
      return;
    }
    // BTN3 already handled above (returns to NAV_RUNNING); draw prompt otherwise
    if (!screenEnabled || !displayReady) return;
    uint32_t now = millis();
    if (!uiDirty && (now - navLastDrawMs) < 120) return;
    navLastDrawMs = now;
    uiDirty = false;
    drawNavReturnPrompt();
    return;
  }

  // ── End-trip confirmation prompt ──────────────────────────────────────
  if (navEndPrompt) {
    if (p1) {
      // Confirmed — clear everything and exit
      navTripActive     = false;
      navEndPrompt      = false;
      navRoundTrip      = false;
      navStage          = NAV_ENTER_LAT;
      navLatStr         = "";
      navLonStr         = "";
      navTargetLat      = 0.0;
      navTargetLon      = 0.0;
      navSavedLat       = 0.0;
      navSavedLon       = 0.0;
      navOriginCaptured = false;
      navClearPrefs();
      navigationExit();
      return;
    }
    if (!screenEnabled || !displayReady) return;
    drawNavEndPrompt();
    return;
  }

  // ── Running navigation ────────────────────────────────────────────────
  if (navStage == NAV_RUNNING) {
    if (p1) {
      if (navRoundTrip) {
        // ARRIVE — ask if ready to return
        navStage = NAV_RETURN_PROMPT;
        uiDirty  = true;
      } else {
        // END — confirm ending the trip
        navEndPrompt = true;
        uiDirty      = true;
      }
      return;
    }

    // BTN2 = toggle metric / imperial units
    if (p2) {
      navImperial     = !navImperial;
      navBeepPeriodMs = 0;
      navNextBeepAt   = 0;
      navSavePrefs();  // persist unit preference across resets
      uiDirty = true;
    }

    serviceNavigationBeep();

    uint32_t now = millis();
    if (!uiDirty && (now - navLastDrawMs) < 120) return;
    navLastDrawMs = now;
    uiDirty = false;
    if (!screenEnabled || !displayReady) return;
    drawNavigationRunning();
    return;
  }

  // ── Keypad entry (lat / lon) ──────────────────────────────────────────
  bool moved = false;
  if (btnWasShortPressed(bUp))    { if (navCursorRow > 0)               navCursorRow--; moved = true; }
  if (btnWasShortPressed(bDown))  { if (navCursorRow < NAV_KP_ROWS - 1) navCursorRow++; moved = true; }
  if (btnWasShortPressed(bLeft))  { if (navCursorCol > 0)               navCursorCol--; moved = true; }
  if (btnWasShortPressed(bRight)) { if (navCursorCol < NAV_KP_COLS - 1) navCursorCol++; moved = true; }
  if (moved) uiDirty = true;

  if (p2) {
    const char* k = NAV_KEYS[navCursorRow][navCursorCol];
    String *activeStr = (navStage == NAV_ENTER_LAT) ? &navLatStr : &navLonStr;

    if (strcmp(k, "EXT") == 0) {
      navigationExit();
      return;
    } else if (strcmp(k, "DEL") == 0) {
      if (activeStr->length() > 0) activeStr->remove(activeStr->length() - 1);
    } else if (strcmp(k, "CLR") == 0) {
      *activeStr = "";
    } else if (strcmp(k, "+/-") == 0) {
      navAppendSignToggle(*activeStr);
    } else if (strcmp(k, "ENT") == 0) {
      double tmp = 0.0;
      bool ok = navParseAndClamp(*activeStr, (navStage == NAV_ENTER_LAT), tmp);
      if (ok) {
        if (navStage == NAV_ENTER_LAT) {
          navTargetLat = tmp;
          navStage     = NAV_ENTER_LON;
          navCursorRow = 0;
          navCursorCol = 0;
        } else {
          // Longitude confirmed.
          // Snapshot GPS origin NOW
          navTargetLon = tmp;
          navStage     = NAV_ROUND_TRIP_PROMPT;
          navRoundTrip = false;

          while (GPSSerial.available()) gps.encode(GPSSerial.read());
          if (gps.location.isValid()) {
            navSavedLat       = gps.location.lat();
            navSavedLon       = gps.location.lng();
            navOriginCaptured = true;
          } else {
            navSavedLat       = 0.0;
            navSavedLon       = 0.0;
            navOriginCaptured = false;
          }
        }
      } else {
        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(1);
        display.setCursor(0, 26);
        display.print("Invalid ");
        display.print((navStage == NAV_ENTER_LAT) ? "LAT" : "LON");
        display.display();
        delay(450);
      }
    } else {
      if (strcmp(k, ".") == 0) {
        if (activeStr->indexOf('.') < 0) *activeStr += ".";
      } else {
        *activeStr += k;
      }
    }
    uiDirty = true;
  }

  uint32_t now = millis();
  if (!uiDirty && (now - navLastDrawMs) < 120) return;
  navLastDrawMs = now;
  uiDirty = false;
  if (!screenEnabled || !displayReady) return;

  if (navStage == NAV_ENTER_LAT) drawNavKeypadAndInput("ENTER LATITUDE",  navLatStr);
  else                            drawNavKeypadAndInput("ENTER LONGITUDE", navLonStr);
}