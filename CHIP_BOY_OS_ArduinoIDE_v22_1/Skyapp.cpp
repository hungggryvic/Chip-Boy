// ============================================================
//  SKY — CHIP-BOY STAR ATLAS  (WatchOS App Module)
//  Display:  SSD1306 128×64 (I2C, addr 0x3C)
//  Buttons:  MCP23008 (BTN1/2/3 + nav switch)
//  Sensors:  BNO055 (heading/pitch) + ATGM336H GPS (lat/lon)
//
//  How it works:
//    The BNO055 gives azimuth (0-360°) and altitude (-90..+90°).
//    GPS gives latitude/longitude for accurate sky calculations.
//    Every sky object (star, planet, moon, sun) is converted from
//    RA/Dec to Alt/Az using your location and the current time.
//    The display shows a 30°×15° window of sky centred on where
//    you are pointing.  A crosshair sits in the centre; any object
//    within ~3° of centre has its name printed below the crosshair.
//
//  Calibration (2-step, Moon + Polaris):
//    Step 1 — Centre crosshair on the Moon, press BTN1.
//             Captures raw BNO + Moon's true Az/Alt (from GPS+time).
//    Step 2 — Centre crosshair on Polaris, press BTN1.
//             Captures raw BNO + Polaris's true Az/Alt.
//             Solves az/alt offsets AND axis signs from the two points.
//
//  Controls:
//    Point device   = move viewfinder
//    BTN3  (GP1)    = EXIT back to main menu
//
//  Symbols:
//    · (dot 1 px)   = dim star  (mag > 3)
//    * (dot 2 px)   = bright star (mag ≤ 3)
//    ○ (circle r=3) = planet
//    ◎ (circle r=5) = Moon
//    ☀ (circle r=4 + rays) = Sun
// ============================================================

#include "SkyApp.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <TinyGPSPlus.h>
#include <math.h>

// ============================================================================
// APP STATE
// ============================================================================
static bool sk_active = false;

bool skyIsActive() { return sk_active; }

extern void gameExitToMenu();
extern void returnToMainMenu();

// ============================================================================
// SHARED HARDWARE (defined in main chipboy sketch)
// ============================================================================
#ifndef SK_SCREEN_W
#define SK_SCREEN_W 128
#define SK_SCREEN_H  64
#endif

extern Adafruit_SSD1306 display;
extern bool displayReady;
extern Adafruit_BNO055 bno;

static TinyGPSPlus sk_gps;
static HardwareSerial &sk_GPSSerial = Serial2;
static const int      SK_GPS_RX_PIN = D5;
static const int      SK_GPS_TX_PIN = D4;
static const uint32_t SK_GPS_BAUD   = 9600;

// MCP23008
#define SK_MCP_ADDR  0x20
#define SK_MCP_IODIR 0x00
#define SK_MCP_GPPU  0x06
#define SK_MCP_GPIO  0x09

#define SK_GP_BTN1   0
#define SK_GP_BTN3   1
#define SK_GP_LEFT   2
#define SK_GP_UP     3
#define SK_GP_DOWN   4
#define SK_GP_RIGHT  5
#define SK_GP_BTN2   6

#ifndef SK_BUZZER_PIN
#define SK_BUZZER_PIN D2
#endif

static uint8_t sk_mcpGpio  = 0xFF;
static uint8_t sk_prevGpio = 0xFF;

static bool sk_mcpRead(uint8_t &val) {
  Wire.beginTransmission(SK_MCP_ADDR);
  Wire.write(SK_MCP_GPIO);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)SK_MCP_ADDR, 1) != 1) return false;
  val = Wire.read();
  return true;
}
static inline bool sk_btnDown(uint8_t p)        { return !(sk_mcpGpio & (1u << p)); }
static inline bool sk_btnJustPressed(uint8_t p) { return !(sk_mcpGpio & (1u << p)) && (sk_prevGpio & (1u << p)); }

// ============================================================================
// MATH HELPERS
// ============================================================================
static inline float sk_wrap360(float a) {
  while (a <   0.f) a += 360.f;
  while (a >= 360.f) a -= 360.f;
  return a;
}
static inline float sk_wrapPM180(float a) {
  while (a >  180.f) a -= 360.f;
  while (a < -180.f) a += 360.f;
  return a;
}
static inline float sk_toRad(float d) { return d * (float)(M_PI / 180.0); }
static inline float sk_toDeg(float r) { return r * (float)(180.0 / M_PI); }

// ============================================================================
// ASTRONOMICAL CONSTANTS & STRUCTS
// ============================================================================

#define SK_FOV_AZ   30.0f
#define SK_FOV_ALT  15.0f
#define SK_CAPTURE_DEG  2.5f

#define SK_TYPE_STAR    0
#define SK_TYPE_PLANET  1
#define SK_TYPE_MOON    2
#define SK_TYPE_SUN     3

struct SkObject {
  const char* name;
  uint8_t     type;
  float       ra;
  float       dec;
  float       mag;
  float       az;
  float       alt;
  bool        visible;
};

// ============================================================================
// STAR CATALOGUE — 52 prominent named stars (J2000 RA/Dec)
// ============================================================================
static SkObject sk_stars[] = {
  { "Sirius",         SK_TYPE_STAR,  6.7525f, -16.716f, -1.46f },
  { "Canopus",        SK_TYPE_STAR,  6.3992f, -52.696f, -0.72f },
  { "Arcturus",       SK_TYPE_STAR, 14.2612f,  19.182f, -0.04f },
  { "Vega",           SK_TYPE_STAR, 18.6157f,  38.783f,  0.03f },
  { "Capella",        SK_TYPE_STAR,  5.2783f,  45.998f,  0.08f },
  { "Rigel",          SK_TYPE_STAR,  5.2422f,  -8.202f,  0.13f },
  { "Procyon",        SK_TYPE_STAR,  7.6553f,   5.225f,  0.38f },
  { "Achernar",       SK_TYPE_STAR,  1.6286f, -57.237f,  0.46f },
  { "Betelgeuse",     SK_TYPE_STAR,  5.9195f,   7.407f,  0.50f },
  { "Hadar",          SK_TYPE_STAR, 14.0637f, -60.373f,  0.61f },
  { "Altair",         SK_TYPE_STAR, 19.8463f,   8.868f,  0.77f },
  { "Aldebaran",      SK_TYPE_STAR,  4.5987f,  16.509f,  0.87f },
  { "Antares",        SK_TYPE_STAR, 16.4901f, -26.432f,  0.96f },
  { "Spica",          SK_TYPE_STAR, 13.4199f, -11.161f,  1.04f },
  { "Pollux",         SK_TYPE_STAR,  7.7553f,  28.026f,  1.14f },
  { "Fomalhaut",      SK_TYPE_STAR, 22.9608f, -29.622f,  1.16f },
  { "Deneb",          SK_TYPE_STAR, 20.6905f,  45.280f,  1.25f },
  { "Mimosa",         SK_TYPE_STAR, 12.7954f, -59.689f,  1.25f },
  { "Regulus",        SK_TYPE_STAR, 10.1395f,  11.967f,  1.35f },
  { "Adhara",         SK_TYPE_STAR,  6.9771f, -28.972f,  1.50f },
  { "Castor",         SK_TYPE_STAR,  7.5766f,  31.888f,  1.58f },
  { "Gacrux",         SK_TYPE_STAR, 12.5194f, -57.113f,  1.59f },
  { "Shaula",         SK_TYPE_STAR, 17.5602f, -37.103f,  1.62f },
  { "Bellatrix",      SK_TYPE_STAR,  5.4186f,   6.350f,  1.64f },
  { "Elnath",         SK_TYPE_STAR,  5.4381f,  28.608f,  1.65f },
  { "Miaplacidus",    SK_TYPE_STAR,  9.2200f, -69.717f,  1.67f },
  { "Alnilam",        SK_TYPE_STAR,  5.6036f,  -1.202f,  1.69f },
  { "Alnitak",        SK_TYPE_STAR,  5.6795f,  -1.943f,  1.74f },
  { "Gamma Vel",      SK_TYPE_STAR,  8.1589f, -47.337f,  1.75f },
  { "Alioth",         SK_TYPE_STAR, 12.9004f,  55.960f,  1.76f },
  { "Mirfak",         SK_TYPE_STAR,  3.4054f,  49.861f,  1.79f },
  { "Dubhe",          SK_TYPE_STAR, 11.0621f,  61.751f,  1.81f },
  { "Wezen",          SK_TYPE_STAR,  7.1397f, -26.393f,  1.83f },
  { "Kaus Australis", SK_TYPE_STAR, 18.4029f, -34.385f,  1.85f },
  { "Avior",          SK_TYPE_STAR,  8.3752f, -59.510f,  1.86f },
  { "Alkaid",         SK_TYPE_STAR, 13.7923f,  49.313f,  1.86f },
  { "Menkent",        SK_TYPE_STAR, 14.1115f, -36.370f,  2.06f },
  { "Atria",          SK_TYPE_STAR, 16.8113f, -69.028f,  1.91f },
  { "Eta Cen",        SK_TYPE_STAR, 14.5916f, -42.158f,  2.31f },
  { "Polaris",        SK_TYPE_STAR,  2.5302f,  89.264f,  1.98f },
  { "Alphard",        SK_TYPE_STAR,  9.4597f,  -8.659f,  1.99f },
  { "Hamal",          SK_TYPE_STAR,  2.1199f,  23.462f,  2.01f },
  { "Diphda",         SK_TYPE_STAR,  0.7264f, -17.987f,  2.04f },
  { "Nunki",          SK_TYPE_STAR, 18.9211f, -26.296f,  2.05f },
  { "Denebola",       SK_TYPE_STAR, 11.8173f,  14.572f,  2.14f },
  { "Alpheratz",      SK_TYPE_STAR,  0.1397f,  29.090f,  2.06f },
  { "Saiph",          SK_TYPE_STAR,  5.7958f,  -9.670f,  2.07f },
  { "Schedar",        SK_TYPE_STAR,  0.6751f,  56.537f,  2.24f },
  { "Mintaka",        SK_TYPE_STAR,  5.5333f,  -0.299f,  2.25f },
  { "Caph",           SK_TYPE_STAR,  0.1527f,  59.150f,  2.28f },
  { "Dschubba",       SK_TYPE_STAR, 16.0056f, -22.622f,  2.29f },
  { "Zuben Elg.",     SK_TYPE_STAR, 14.8479f, -16.042f,  2.61f },
};
static const int SK_NUM_STARS = sizeof(sk_stars) / sizeof(sk_stars[0]);

// Index of Polaris in sk_stars — used by calibration
// Must match the position in the catalogue above (index 39)
static const int SK_POLARIS_IDX = 39;

static SkObject sk_sun     = { "Sun",     SK_TYPE_SUN,    0,0,0, 0,0,false };
static SkObject sk_moon    = { "Moon",    SK_TYPE_MOON,   0,0,0, 0,0,false };
static SkObject sk_mercury = { "Mercury", SK_TYPE_PLANET, 0,0,0, 0,0,false };
static SkObject sk_venus   = { "Venus",   SK_TYPE_PLANET, 0,0,0, 0,0,false };
static SkObject sk_mars    = { "Mars",    SK_TYPE_PLANET, 0,0,0, 0,0,false };
static SkObject sk_jupiter = { "Jupiter", SK_TYPE_PLANET, 0,0,0, 0,0,false };
static SkObject sk_saturn  = { "Saturn",  SK_TYPE_PLANET, 0,0,0, 0,0,false };

// ============================================================================
// ORIENTATION STATE
// ============================================================================
static float sk_azimuth  = 0.f;
static float sk_altitude = 0.f;

// ============================================================================
// CALIBRATION STATE
// ============================================================================
// 2-step calibration: Moon (step 1) then Polaris (step 2).
// From two known true Az/Alt positions and their corresponding raw BNO
// readings we solve for axis signs and additive offsets.
//
// Math:
//   corrected_az  = raw_x * azSign  + azOffset   (mod 360)
//   corrected_alt = raw_y * altSign + altOffset
//
// From step 1 (Moon) and step 2 (Polaris):
//   azSign  = sign( (trueAz2  - trueAz1)  / (rawX2  - rawX1) )
//   altSign = sign( (trueAlt2 - trueAlt1) / (rawY2  - rawY1) )
//   azOffset  = trueAz1  - rawX1 * azSign
//   altOffset = trueAlt1 - rawY1 * altSign

static bool  sk_calDone     = false;
static float sk_azOffsetDeg  = 0.f;
static float sk_altOffsetDeg = 0.f;
static float sk_azSign       = 1.f;
static float sk_altSign      = -1.f;

// Calibration wizard steps
enum SkCalStep {
  SK_CAL_MOON,     // Step 1 — point at Moon, press BTN1
  SK_CAL_POLARIS,  // Step 2 — point at Polaris, press BTN1
  SK_CAL_DONE
};
static SkCalStep sk_calStep = SK_CAL_MOON;

// Raw BNO readings captured at each step
static float sk_rawMoonX    = 0.f, sk_rawMoonY    = 0.f;
static float sk_rawPolarisX = 0.f, sk_rawPolarisY = 0.f;

// Read raw BNO Euler x and y (no corrections applied)
static void sk_readRawBNO(float &rx, float &ry) {
  sensors_event_t ori;
  bno.getEvent(&ori, Adafruit_BNO055::VECTOR_EULER);
  rx = ori.orientation.x;
  ry = ori.orientation.y;
}

// Solve calibration from Moon + Polaris captures.
static void sk_solveCalibration() {
  float trueAz1  = sk_moon.az,           trueAlt1 = sk_moon.alt;
  float trueAz2  = sk_stars[SK_POLARIS_IDX].az;
  float trueAlt2 = sk_stars[SK_POLARIS_IDX].alt;

  // ── Az sign ──────────────────────────────────────────────────────────────
  // Wrap the raw delta the same way we wrap the true delta so the sign
  // comparison is consistent across the 0/360 boundary.
  float trueAzDelta = sk_wrapPM180(trueAz2 - trueAz1);
  float rawAzDelta  = sk_rawPolarisX - sk_rawMoonX;
  // Wrap raw delta too (BNO x also wraps at 360)
  while (rawAzDelta >  180.f) rawAzDelta -= 360.f;
  while (rawAzDelta < -180.f) rawAzDelta += 360.f;

  // If both deltas have the same sign, BNO x increases in the same direction
  // as true azimuth → azSign = +1.  Otherwise flip.
  if (fabsf(rawAzDelta) < 1.0f) {
    // Degenerate: user barely moved in az between the two targets.
    // Fall back to sign = +1 and rely on offset alone.
    sk_azSign = 1.f;
  } else {
    sk_azSign = ((trueAzDelta * rawAzDelta) >= 0.f) ? 1.f : -1.f;
  }

  // ── Alt sign ─────────────────────────────────────────────────────────────
  // Moon alt is typically 10–60°; Polaris alt ≈ observer latitude (~38° for CA).
  // They are often similar in altitude — but the raw y values should still
  // show a clear signed delta when the user tilts between them.
  float trueAltDelta = trueAlt2 - trueAlt1;
  float rawAltDelta  = sk_rawPolarisY - sk_rawMoonY;
  while (rawAltDelta >  180.f) rawAltDelta -= 360.f;
  while (rawAltDelta < -180.f) rawAltDelta += 360.f;

  if (fabsf(rawAltDelta) < 1.0f) {
    // Degenerate: targets at nearly the same altitude.
    // Keep default sign (-1, most BNO orientations) and rely on offset.
    sk_altSign = -1.f;
  } else {
    sk_altSign = ((trueAltDelta * rawAltDelta) >= 0.f) ? 1.f : -1.f;
  }

  // ── Offsets from Moon (step 1) ────────────────────────────────────────────
  // Use Moon as the anchor since it was captured first and is the bigger target.
  sk_azOffsetDeg  = trueAz1  - (sk_rawMoonX * sk_azSign);
  sk_altOffsetDeg = trueAlt1 - (sk_rawMoonY * sk_altSign);

  // Normalise az offset to 0..360
  sk_azOffsetDeg = sk_wrap360(sk_azOffsetDeg);

  sk_calDone = true;
  sk_calStep = SK_CAL_DONE;
}

// ============================================================================
// LOCATION STATE
// ============================================================================
static float sk_lat =  37.77f;
static float sk_lon = -122.42f;
static bool  sk_hasGPS = false;

// ============================================================================
// TIME STATE
// ============================================================================
static double sk_jd = 2451545.0;

static double sk_julianDay(int yr, int mo, int dy, int h, int mi, int s) {
  if (mo <= 2) { yr--; mo += 12; }
  int A = (int)(yr / 100);
  int B = 2 - A + (int)(A / 4);
  return (int)(365.25 * (yr + 4716))
       + (int)(30.6001 * (mo + 1))
       + dy + B - 1524.5
       + (h + mi / 60.0 + s / 3600.0) / 24.0;
}

static double sk_gmst(double jd) {
  double T = (jd - 2451545.0) / 36525.0;
  double gmst = 280.46061837
              + 360.98564736629 * (jd - 2451545.0)
              + T * T * 0.000387933
              - T * T * T / 38710000.0;
  while (gmst <   0) gmst += 360.0;
  while (gmst >= 360) gmst -= 360.0;
  return gmst;
}

static double sk_lst(double jd, float lon) {
  return fmod(sk_gmst(jd) + (double)lon, 360.0);
}

static void sk_raDecToAltAz(float ra_h, float dec_d,
                              float lat,  float lon,
                              double jd,
                              float &az, float &alt) {
  double lst = sk_lst(jd, lon);
  double ha  = lst - (double)(ra_h * 15.0f);
  while (ha >  180.0) ha -= 360.0;
  while (ha < -180.0) ha += 360.0;

  double haR  = ha  * (M_PI / 180.0);
  double decR = (double)dec_d * (M_PI / 180.0);
  double latR = (double)lat   * (M_PI / 180.0);

  double sinAlt = sin(decR)*sin(latR) + cos(decR)*cos(latR)*cos(haR);
  double altR   = asin(sinAlt);

  double cosAz = (sin(decR) - sin(altR)*sin(latR)) / (cos(altR)*cos(latR));
  cosAz = constrain((float)cosAz, -1.f, 1.f);
  double azR   = acos(cosAz);
  if (sin(haR) > 0) azR = 2.0 * M_PI - azR;

  alt = (float)(altR * 180.0 / M_PI);
  az  = (float)(azR  * 180.0 / M_PI);
}

// ============================================================================
// SOLAR SYSTEM POSITIONS
// ============================================================================
static double sk_norm(double a) {
  a = fmod(a, 360.0);
  return (a < 0) ? a + 360.0 : a;
}

static double sk_kepler(double M_deg, double e) {
  double M = M_deg * (M_PI / 180.0);
  double E = M;
  for (int i = 0; i < 10; i++) E = M + e * sin(E);
  return E * (180.0 / M_PI);
}

static void sk_sunRaDec(double jd, float &ra_h, float &dec_d) {
  double d = jd - 2451543.5;
  double w = sk_norm(282.9404 + 4.70935e-5 * d);
  double e = 0.016709 - 1.151e-9 * d;
  double M = sk_norm(356.0470 + 0.9856002585 * d);
  double E = sk_kepler(M, e);
  double v = sk_norm(2.0 * atan2(sqrt(1+e)*sin(E*(M_PI/180.0)/2.0),
                                  sqrt(1-e)*cos(E*(M_PI/180.0)/2.0))
                      * (180.0/M_PI));
  double lon  = sk_norm(v + w);
  double oblq = 23.4393 - 3.563e-7 * d;
  double oblqR = oblq * (M_PI/180.0);
  double lonR  = lon  * (M_PI/180.0);
  double ra  = atan2(cos(oblqR)*sin(lonR), cos(lonR));
  double dec = asin( sin(oblqR)*sin(lonR));
  ra_h  = (float)(ra * (12.0/M_PI));
  if (ra_h < 0) ra_h += 24.0f;
  dec_d = (float)(dec * (180.0/M_PI));
}

static void sk_moonRaDec(double jd, float &ra_h, float &dec_d) {
  double d = jd - 2451543.5;
  double N = sk_norm(125.1228 - 0.0529538083 * d);
  double i = 5.1454;
  double w = sk_norm(318.0634 + 0.1643573223 * d);
  double e = 0.054900;
  double M = sk_norm(115.3654 + 13.0649929509 * d);
  double E = sk_kepler(M, e);
  double v = sk_norm(2.0 * atan2(sqrt(1+e)*sin(E*(M_PI/180.0)/2.0),
                                  sqrt(1-e)*cos(E*(M_PI/180.0)/2.0))
                      * (180.0/M_PI));
  double lon_ecl = v + w;
  double NR = N*(M_PI/180.0);
  double lon_eclR = lon_ecl*(M_PI/180.0);
  double iR = i*(M_PI/180.0);
  double xecl = cos(lon_eclR);
  double yecl = sin(lon_eclR)*cos(iR);
  double zecl = sin(lon_eclR)*sin(iR);
  double oblq  = 23.4393 - 3.563e-7 * d;
  double oblqR = oblq*(M_PI/180.0);
  double xeq = xecl;
  double yeq = yecl*cos(oblqR) - zecl*sin(oblqR);
  double zeq = yecl*sin(oblqR) + zecl*cos(oblqR);
  double ra  = atan2(yeq, xeq);
  double dec = atan2(zeq, sqrt(xeq*xeq+yeq*yeq));
  ra_h  = (float)(ra * (12.0/M_PI));
  if (ra_h < 0) ra_h += 24.0f;
  dec_d = (float)(dec * (180.0/M_PI));
}

struct SkPlanetElem {
  double N0, Nd, i0, id, w0, wd, a, e0, ed, M0, Md;
};

static const SkPlanetElem sk_planetElem[] = {
  { 48.3313,3.24587e-5, 7.0047,5.00e-8,  29.1241,1.01444e-5, 0.387098,0.205635, 5.59e-10,168.6562,4.0923344368 },
  { 76.6799,2.46590e-5, 3.3946,2.75e-8,  54.8910,1.38374e-5, 0.723330,0.006773,-1.302e-9, 48.0052,1.6021302244 },
  { 49.5574,2.11081e-5, 1.8497,-1.78e-8,286.5016,2.92961e-5, 1.523688,0.093405, 2.516e-9, 18.6021,0.5240207766 },
  {100.4542,2.76854e-5, 1.3030,-1.557e-7,273.8777,1.64505e-5,5.20256, 0.048498, 4.469e-9, 19.8950,0.0830853001 },
  {113.6634,2.38980e-5, 2.4886,-1.081e-7,339.3939,2.97661e-5,9.55475, 0.055546,-9.499e-9,316.9670,0.0334442282 },
};

static void sk_planetRaDec(int idx, double jd, float &ra_h, float &dec_d) {
  const SkPlanetElem &p = sk_planetElem[idx];
  double d = jd - 2451543.5;
  double N = sk_norm(p.N0 + p.Nd*d);
  double i = p.i0 + p.id*d;
  double w = sk_norm(p.w0 + p.wd*d);
  double a = p.a;
  double e = p.e0 + p.ed*d;
  double M = sk_norm(p.M0 + p.Md*d);
  double E = sk_kepler(M, e);
  double v = sk_norm(2.0*atan2(sqrt(1+e)*sin(E*(M_PI/180.0)/2.0),
                                sqrt(1-e)*cos(E*(M_PI/180.0)/2.0))*(180.0/M_PI));
  double r   = a*(1-e*cos(E*(M_PI/180.0)));
  double NR  = N*(M_PI/180.0);
  double iR  = i*(M_PI/180.0);
  double vwR = (v+w)*(M_PI/180.0);
  double xh  = r*(cos(NR)*cos(vwR) - sin(NR)*sin(vwR)*cos(iR));
  double yh  = r*(sin(NR)*cos(vwR) + cos(NR)*sin(vwR)*cos(iR));
  double zh  = r*(sin(vwR)*sin(iR));
  double oblq  = 23.4393 - 3.563e-7*d;
  double oblqR = oblq*(M_PI/180.0);
  double xe = xh;
  double ye = yh*cos(oblqR) - zh*sin(oblqR);
  double ze = yh*sin(oblqR) + zh*cos(oblqR);
  double ra  = atan2(ye, xe);
  double dec = atan2(ze, sqrt(xe*xe+ye*ye));
  ra_h  = (float)(ra*(12.0/M_PI));
  if (ra_h < 0) ra_h += 24.0f;
  dec_d = (float)(dec*(180.0/M_PI));
}

// ============================================================================
// UPDATE ORIENTATION FROM BNO055
// ============================================================================
static void sk_updateOrientation() {
  float rx, ry;
  sk_readRawBNO(rx, ry);

  if (sk_calDone) {
    sk_azimuth  = sk_wrap360(rx * sk_azSign + sk_azOffsetDeg);
    float alt   = ry * sk_altSign + sk_altOffsetDeg;
    if (alt >  180.f) alt -= 360.f;
    if (alt < -180.f) alt += 360.f;
    sk_altitude = constrain(alt, -90.f, 90.f);
  } else {
    // Pre-calibration passthrough so the screen visibly moves
    sk_azimuth  = sk_wrap360(rx);
    float pitch = ry;
    if (pitch >  180.f) pitch -= 360.f;
    if (pitch < -180.f) pitch += 360.f;
    sk_altitude = constrain(-pitch, -90.f, 90.f);
  }
}

// ============================================================================
// UPDATE GPS
// ============================================================================
static void sk_updateGPS() {
  while (sk_GPSSerial.available()) sk_gps.encode(sk_GPSSerial.read());
  if (sk_gps.location.isValid()) {
    sk_lat    = (float)sk_gps.location.lat();
    sk_lon    = (float)sk_gps.location.lng();
    sk_hasGPS = true;
  }
  if (sk_gps.date.isValid() && sk_gps.time.isValid()) {
    sk_jd = sk_julianDay(sk_gps.date.year(), sk_gps.date.month(), sk_gps.date.day(),
                          sk_gps.time.hour(), sk_gps.time.minute(), sk_gps.time.second());
  }
}

// ============================================================================
// COMPUTE ALL SKY POSITIONS
// ============================================================================
static void sk_computeSkyPositions() {
  for (int i = 0; i < SK_NUM_STARS; i++) {
    sk_raDecToAltAz(sk_stars[i].ra, sk_stars[i].dec,
                    sk_lat, sk_lon, sk_jd,
                    sk_stars[i].az, sk_stars[i].alt);
    sk_stars[i].visible = (sk_stars[i].alt > -1.0f);
  }
  sk_sunRaDec(sk_jd, sk_sun.ra, sk_sun.dec);
  sk_raDecToAltAz(sk_sun.ra, sk_sun.dec, sk_lat, sk_lon, sk_jd,
                  sk_sun.az, sk_sun.alt);
  sk_sun.visible = (sk_sun.alt > -1.0f);

  sk_moonRaDec(sk_jd, sk_moon.ra, sk_moon.dec);
  sk_raDecToAltAz(sk_moon.ra, sk_moon.dec, sk_lat, sk_lon, sk_jd,
                  sk_moon.az, sk_moon.alt);
  sk_moon.visible = (sk_moon.alt > -1.0f);

  SkObject* planets[] = { &sk_mercury,&sk_venus,&sk_mars,&sk_jupiter,&sk_saturn };
  for (int p = 0; p < 5; p++) {
    sk_planetRaDec(p, sk_jd, planets[p]->ra, planets[p]->dec);
    sk_raDecToAltAz(planets[p]->ra, planets[p]->dec, sk_lat, sk_lon, sk_jd,
                    planets[p]->az, planets[p]->alt);
    planets[p]->visible = (planets[p]->alt > -1.0f);
  }
}

// ============================================================================
// PROJECTION
// ============================================================================
static bool sk_project(float objAz, float objAlt, int16_t &px, int16_t &py) {
  float dAz  = sk_wrapPM180(objAz  - sk_azimuth);
  float dAlt = objAlt - sk_altitude;
  if (fabsf(dAz)  > SK_FOV_AZ  / 2.0f + 1.5f) return false;
  if (fabsf(dAlt) > SK_FOV_ALT / 2.0f + 1.5f) return false;
  px = (int16_t)((dAz  / SK_FOV_AZ)  * SK_SCREEN_W + SK_SCREEN_W / 2.0f);
  py = (int16_t)(-(dAlt / SK_FOV_ALT) * SK_SCREEN_H + SK_SCREEN_H / 2.0f);
  return true;
}

// ============================================================================
// DRAW HELPERS
// ============================================================================
static void sk_drawStar(int16_t px, int16_t py, float mag) {
  if (mag <= 1.5f) {
    display.drawPixel(px,   py,   WHITE);
    display.drawPixel(px-1, py,   WHITE);
    display.drawPixel(px+1, py,   WHITE);
    display.drawPixel(px,   py-1, WHITE);
    display.drawPixel(px,   py+1, WHITE);
  } else if (mag <= 3.0f) {
    display.drawPixel(px,   py,   WHITE);
    display.drawPixel(px+1, py,   WHITE);
    display.drawPixel(px,   py+1, WHITE);
    display.drawPixel(px+1, py+1, WHITE);
  } else {
    display.drawPixel(px, py, WHITE);
  }
}

static void sk_drawPlanet(int16_t px, int16_t py) {
  display.drawCircle(px, py, 3, WHITE);
  display.drawPixel(px, py, WHITE);
}

static void sk_drawMoon(int16_t px, int16_t py) {
  display.drawCircle(px, py, 5, WHITE);
  display.drawCircle(px, py, 4, WHITE);
}

static void sk_drawSun(int16_t px, int16_t py) {
  display.drawCircle(px, py, 4, WHITE);
  display.fillCircle(px, py, 2, WHITE);
  display.drawFastHLine(px-7, py,   3, WHITE);
  display.drawFastHLine(px+5, py,   3, WHITE);
  display.drawFastVLine(px,   py-7, 3, WHITE);
  display.drawFastVLine(px,   py+5, 3, WHITE);
}

static void sk_drawCrosshair() {
  int cx = SK_SCREEN_W / 2;
  int cy = SK_SCREEN_H / 2;
  int arm = 6, gap = 3;
  display.drawFastHLine(cx-arm-gap, cy,        arm, WHITE);
  display.drawFastHLine(cx+gap+1,   cy,        arm, WHITE);
  display.drawFastVLine(cx,         cy-arm-gap, arm, WHITE);
  display.drawFastVLine(cx,         cy+gap+1,   arm, WHITE);
  display.drawPixel(cx, cy, WHITE);
}

// ============================================================================
// LABEL LOGIC
// ============================================================================
struct SkCapture {
  const char* name;
  uint8_t     type;
  float       dist;
};

static SkCapture sk_findCapture() {
  SkCapture best = { nullptr, 0, SK_CAPTURE_DEG + 1.0f };

  auto check = [&](const char* name, uint8_t type, float az, float alt, bool visible) {
    if (!visible) return;
    float dAz  = fabsf(sk_wrapPM180(az  - sk_azimuth));
    float dAlt = fabsf(alt - sk_altitude);
    float dist = sqrtf(dAz*dAz + dAlt*dAlt);
    if (dist < best.dist) { best.name = name; best.type = type; best.dist = dist; }
  };

  for (int i = 0; i < SK_NUM_STARS; i++)
    check(sk_stars[i].name, SK_TYPE_STAR, sk_stars[i].az, sk_stars[i].alt, sk_stars[i].visible);
  check(sk_sun.name,     SK_TYPE_SUN,    sk_sun.az,     sk_sun.alt,     sk_sun.visible);
  check(sk_moon.name,    SK_TYPE_MOON,   sk_moon.az,    sk_moon.alt,    sk_moon.visible);
  check(sk_mercury.name, SK_TYPE_PLANET, sk_mercury.az, sk_mercury.alt, sk_mercury.visible);
  check(sk_venus.name,   SK_TYPE_PLANET, sk_venus.az,   sk_venus.alt,   sk_venus.visible);
  check(sk_mars.name,    SK_TYPE_PLANET, sk_mars.az,    sk_mars.alt,    sk_mars.visible);
  check(sk_jupiter.name, SK_TYPE_PLANET, sk_jupiter.az, sk_jupiter.alt, sk_jupiter.visible);
  check(sk_saturn.name,  SK_TYPE_PLANET, sk_saturn.az,  sk_saturn.alt,  sk_saturn.visible);

  return best;
}

// ============================================================================
// CALIBRATION WIZARD SCREEN
// ============================================================================
static void sk_drawCalScreen() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);

  // Live crosshair so user can see device movement
  sk_drawCrosshair();

  switch (sk_calStep) {

    case SK_CAL_MOON: {
      // Header
      display.setCursor(0, 0);
      display.print("CAL 1/2: MOON");

      // Show Moon's computed position as a pointing guide
      char guide[24];
      if (sk_moon.visible) {
        snprintf(guide, sizeof(guide), "az:%3d  alt:%+3d",
                 (int)sk_moon.az, (int)sk_moon.alt);
      } else {
        snprintf(guide, sizeof(guide), "Moon below horizon");
      }
      display.setCursor(0, 10);
      display.print(guide);

      // Instruction
      display.setCursor(0, 22);
      display.print("Centre crosshair");
      display.setCursor(0, 31);
      display.print("on the MOON");
      display.setCursor(0, 40);
      display.print("then press BTN1");
      break;
    }

    case SK_CAL_POLARIS: {
      // Header
      display.setCursor(0, 0);
      display.print("CAL 2/2: POLARIS");

      // Show Polaris's computed position
      char guide[24];
      snprintf(guide, sizeof(guide), "az:%3d  alt:%+3d",
               (int)sk_stars[SK_POLARIS_IDX].az,
               (int)sk_stars[SK_POLARIS_IDX].alt);
      display.setCursor(0, 10);
      display.print(guide);

      // Instruction
      display.setCursor(0, 22);
      display.print("Centre crosshair");
      display.setCursor(0, 31);
      display.print("on POLARIS");
      display.setCursor(0, 40);
      display.print("then press BTN1");
      break;
    }

    default: break;
  }

  // Live az/alt readout so the user can see the sensor responding
  char buf[20];
  snprintf(buf, sizeof(buf), "now:%3d az %+3d alt",
           (int)sk_azimuth, (int)sk_altitude);
  display.setCursor(0, 51);
  display.print(buf);

  display.setCursor(SK_SCREEN_W - 24, 55);
  display.print("EXIT");

  display.display();
}

// ============================================================================
// DRAW FULL FRAME
// ============================================================================
static void sk_drawFrame() {
  display.clearDisplay();

  int16_t px, py;

  for (int i = 0; i < SK_NUM_STARS; i++) {
    if (!sk_stars[i].visible) continue;
    if (sk_project(sk_stars[i].az, sk_stars[i].alt, px, py))
      sk_drawStar(px, py, sk_stars[i].mag);
  }
  if (sk_sun.visible  && sk_project(sk_sun.az,  sk_sun.alt,  px, py)) sk_drawSun(px, py);
  if (sk_moon.visible && sk_project(sk_moon.az, sk_moon.alt, px, py)) sk_drawMoon(px, py);

  SkObject* planets[] = { &sk_mercury,&sk_venus,&sk_mars,&sk_jupiter,&sk_saturn };
  for (int p = 0; p < 5; p++) {
    if (planets[p]->visible && sk_project(planets[p]->az, planets[p]->alt, px, py))
      sk_drawPlanet(px, py);
  }

  sk_drawCrosshair();

  SkCapture cap = sk_findCapture();
  if (cap.name != nullptr) {
    display.setTextSize(1);
    display.setTextColor(WHITE);
    char label[24];
    if (cap.type == SK_TYPE_MOON)        snprintf(label, sizeof(label), "MOON");
    else if (cap.type == SK_TYPE_SUN)    snprintf(label, sizeof(label), "SUN");
    else if (cap.type == SK_TYPE_PLANET) snprintf(label, sizeof(label), "PLANET: %s", cap.name);
    else                                  snprintf(label, sizeof(label), "%s", cap.name);

    int labelW = strlen(label) * 6;
    int labelX = (SK_SCREEN_W - labelW) / 2;
    int labelY = (SK_SCREEN_H / 2) + 10;
    if (labelY > 46) labelY = 46;
    display.setCursor(labelX, labelY);
    display.print(label);
  }

  display.setTextSize(1);
  display.setTextColor(WHITE);
  char buf[16];
  snprintf(buf, sizeof(buf), "%3d\xF8 %+3d\xF8",
           (int)sk_azimuth, (int)sk_altitude);
  display.setCursor(0, 0);
  display.print(buf);

  if (!sk_hasGPS) {
    display.setCursor(SK_SCREEN_W - 18, 0);
    display.print("?GPS");
  }

  display.setCursor(SK_SCREEN_W - 24, 55);
  display.print("EXIT");

  display.display();
}

// ============================================================================
// STARTUP SPLASH
// ============================================================================
static void sk_drawSplash() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(2);
  display.setCursor(16, 8);
  display.print("SKY MAP");
  display.setTextSize(1);
  display.setCursor(4, 30);
  display.print("Finding orientation");
  display.setCursor(4, 40);
  display.print("Point at the sky...");
  display.setCursor(SK_SCREEN_W - 24, 55);
  display.print("EXIT");
  display.display();
}

// ============================================================================
// FRAME RATE CONTROL
// ============================================================================
static unsigned long sk_lastFrame = 0;
static const unsigned long SK_FRAME_MS = 100;

// ============================================================================
// PUBLIC WATCHOS API
// ============================================================================
extern bool inMenu;
extern bool uiDirty;
extern void redrawMenuNow();

void skyEnter() {
  sk_active    = true;
  sk_lastFrame = 0;

  // Reset calibration wizard for this launch (offsets kept if already solved)
  if (!sk_calDone) {
    sk_calStep      = SK_CAL_MOON;
    sk_azOffsetDeg  = 0.f;
    sk_altOffsetDeg = 0.f;
    sk_azSign       = 1.f;
    sk_altSign      = -1.f;
  }

  sk_gps = TinyGPSPlus();
  sk_GPSSerial.begin(SK_GPS_BAUD, SERIAL_8N1, SK_GPS_RX_PIN, SK_GPS_TX_PIN);

  for (int i = 0; i < 5; i++) {
    sensors_event_t e;
    bno.getEvent(&e, Adafruit_BNO055::VECTOR_EULER);
    delay(20);
  }

  // Get GPS + compute sky so Moon/Polaris az/alt are ready before wizard
  sk_updateGPS();
  sk_computeSkyPositions();
}

void skyUpdate() {
  if (!sk_active) return;

  unsigned long now = millis();
  if (now - sk_lastFrame < SK_FRAME_MS) return;
  sk_lastFrame = now;

  sk_prevGpio = sk_mcpGpio;
  if (!sk_mcpRead(sk_mcpGpio)) sk_mcpGpio = 0xFF;

  if (sk_btnJustPressed(SK_GP_BTN3)) {
    skyExit();
    inMenu  = true;
    uiDirty = true;
    redrawMenuNow();
    return;
  }

  sk_updateOrientation();

  static uint8_t sk_frameCount = 0;
  if (++sk_frameCount >= 50) {
    sk_frameCount = 0;
    sk_updateGPS();
    sk_computeSkyPositions();
  }

  // ── CALIBRATION WIZARD ──────────────────────────────────────────────────
  if (!sk_calDone) {
    if (sk_btnJustPressed(SK_GP_BTN1)) {
      float rx, ry;
      sk_readRawBNO(rx, ry);

      switch (sk_calStep) {

        case SK_CAL_MOON:
          sk_rawMoonX = rx;
          sk_rawMoonY = ry;
          sk_calStep  = SK_CAL_POLARIS;
          tone(SK_BUZZER_PIN, 880, 40);
          break;

        case SK_CAL_POLARIS:
          sk_rawPolarisX = rx;
          sk_rawPolarisY = ry;
          // Both points captured — solve and go live
          sk_solveCalibration();
          tone(SK_BUZZER_PIN, 1047, 80);
          delay(90);
          tone(SK_BUZZER_PIN, 1319, 120);
          break;

        default: break;
      }
    }
    sk_drawCalScreen();
    return;
  }

  // ── NORMAL SKY MAP ───────────────────────────────────────────────────────
  sk_drawFrame();
}

void skyExit() {
  sk_active = false;
  // sk_calDone intentionally NOT cleared — re-entering skips the wizard
  // for the rest of the session.
  sk_GPSSerial.end();
  noTone(SK_BUZZER_PIN);
}