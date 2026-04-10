#include "RadioApp.h"
#include <Adafruit_SSD1306.h>

// ============================================================================
// RADIO APP IMPLEMENTATION
// ============================================================================

// Forward declarations of globals defined in main .ino
extern bool inRadio;
extern bool inRecordings;
extern bool uiDirty;
extern bool displayReady;
extern bool screenEnabled;
extern Adafruit_SSD1306 display;

extern bool g_mcp_ok;
extern uint8_t g_mcp_gpio;

struct BtnState;
extern BtnState bRight, bLeft;
bool btnWasShortPressed(const BtnState &b_in);

void redrawMenuNow();
void recordingsEncToggle();
void recordingsEncNext();
void recordingsEncPrev();

// Encoder switch debounce state (shared with main)
extern bool encSwStable;
extern bool encSwLastRead;
extern unsigned long encSwLastChange;
extern const unsigned long ENC_SW_DEBOUNCE_MS;

// ── SHUFFLE icon sprite — 10×8 pixels ───────────────────────
static const uint8_t kShuffleIcon[8][10] PROGMEM = {
  { 1,1,1,0,0,0,1,1,1,0 },
  { 0,0,0,1,0,0,0,1,1,0 },
  { 0,0,0,0,1,0,1,0,1,0 },
  { 0,0,0,0,0,1,0,0,0,0 },
  { 0,0,0,0,1,0,1,0,1,0 },
  { 0,0,0,1,0,0,0,1,1,0 },
  { 1,1,1,0,0,0,1,1,1,0 },
  { 0,0,0,0,0,0,0,0,0,0 },
};

// 32-sample sine LUT scaled to [-127..127]
const int8_t kSin32[32] = {
    0,  25,  49,  71,  90, 106, 117, 125,
  127, 125, 117, 106,  90,  71,  49,  25,
    0, -25, -49, -71, -90,-106,-117,-125,
 -127,-125,-117,-106, -90, -71, -49, -25
};

// ── Track tables ──────────────────────────────────────────────────────────


static const TrackInfo kTracks01[11] = {
  {"",""},
  {"Bulkkoch Eulo", "past self"},
  {"Yaksok", "past self"},
  {"Temperance", "Twin Tribes"},
  {"I Wish I Was You (Twin Tribes Remix)", "Creux Lies"},
  {"Clock Man", "FRENCH POLICE"},
  {"Mudang", "past self"},
  {"pain", "blood club"},
  {"Nogseun Kal", "past self"},
  {"The River", "Twin Tribes"},
  {"Gwisin", "past self"},
};

static const TrackInfo kTracks02[21] = {
  {"",""},
  {"The Four Seasons", "Antonio Vivaldi"},
  {"Carnival of the Animals: The Swan", "Camille Saint-Saens"},
  {"Salut d'Amour, Op. 12", "Edward Elgar"},
  {"Nocturne in E Flat Major, Op. 9, No. 2", "Frederic Chopin"},
  {"Clair de Lune", "Claude Debussy"},
  {"Piano Sonata No. 14 in C Sharp Minor", "Ludwig can Beethoven"},
  {"Dawn", "Dario Marianelli"},
  {"Symphonic Metamorphosis of Themes by Carl Maria von Weber", "Paul Hindemith"},
  {"The Barber of Seville (Overture)", "Gioachino Rossini"},
  {"Symphony No. 5 in C Minor, Op. 67", "Ludwig van Beethoven"},
  {"Por Ti Volare (Con Te Partiro)", "Andrea Bocelli"},
  {"Symphony No. 40 in G Minor, K. 550", "Wolfang Mozart"},
  {"Consolation No. 3 in D Flat Major", "Franz Liszt"},
  {"Air", "Johann Sebastian Bach"},
  {"The Nutcracker Suite, Op. 71a", "Pyotr Tchaikovsky"},
  {"Orpheus in the Underworld", "Jacques Offenbach"},
  {"O mio Babbino caro", "Giacomo Puccini"},
  {"Enigma Variations, Op. 36: Var. 9. Adagio (Nimrod) ", "Edward Elgar"},
  {"Canon in D Major", "Johann Pachelbel"},
  {"Eine kleine Nachtmusik","Wolfgang Mozart"},
};

static const TrackInfo kTracks03[31] = {
  {"",""},
  {"End Of Beginning", "Djo"},
  {"Money", "The Drums"},
  {"Chamber Of Reflection", "Mac DeMarco"},
  {"Somebody Else", "The 1975"},
  {"Instant Crush (ft. Julian Casablancas)", "Daft Punk"},
  {"Nowhere", "CD Ghost"},
  {"What Once Was", "Her's"},
  {"Outside", "TOPS"},
  {"Chances", "The Strokes"},
  {"Midnight City", "M83"},
  {"1901", "Phoenix"},
  {"Time To Pretend", "MGMT"},
  {"Automatic Stop", "The Strokes"},
  {"505", "Arctic Monkeys"},
  {"Shadowplay", "The Killers"},
  {"Trouble", "Cage The Elephant"},
  {"Soul Meets Body", "Death Cab For Cutie"},
  {"Somebody That I Used To Know ft. Kimbra", "Gotye"},
  {"Sweater Weather", "The Neighbourhood"},
  {"The Adults Are Talking", "The Strokes"},
  {"Cigarette Daydream", "Cage The Elephant"},
  {"Smile Like You Mean It", "The Killers"},
  {"Fireflies", "Owl City"},
  {"Young Blood","The Naked And Famous"},
  {"Electric Feel","MGMT"},
  {"Save Your Tears","The Weeknd"},
  {"Sit Next To Me","Foster The People"},
  {"Tonight, Tonight","The Smashing Pumpkins"},
  {"Use Somebody","Kings Of Leon"},
  {"Fluorescent Adolescent","Arctic Monkeys"},
};

static const TrackInfo kTracks04[42] = {
  {"",""},
  {"True Faith", "New Order"},
  {"(I Just) Died In Your Arms", "Cutting Crew"},
  {"It's My Life", "Talk Talk"},
  {"Space Age Lovesong", "A Flock Of Seagulls"},
  {"Head Over Heels", "Tears For Fears"},
  {"Eyes Without A Face", "Billy Idol"},
  {"Lullaby", "The Cure"},
  {"Precious", "Depeche Mode"},
  {"Turn Around (Total Eclipse Of The Heart)", "Bonnie Tyler"},
  {"Take On Me", "A-ha"},
  {"Talking In Your Sleep", "The Romantics"},
  {"Your Love", "The Outfield"},
  {"Everybody Wants To Rule The World", "Tears For Fears"},
  {"West End Girls", "Pet Shop Boys"},
  {"True", "Spandau Ballet"},
  {"Time After Time", "Cyndi Lauper"},
  {"I Have Nothing", "Whitney Houston"},
  {"Don't Dream It's Over", "Crowded House"},
  {"Under The Milky Way", "The Church"},
  {"Lovesong", "The Cure"},
  {"Enjoy The Silence", "Depeche Mode"},
  {"New Year's Day", "U2"},
  {"Don't You Forget About Me", "Simple Minds"},
  {"Under Pressure", "Queen, David Bowie"},
  {"Roxanne", "The Police"},
  {"Hit Me With Your Best Shot", "Pat Benatar"},
  {"Don't Stop Believin'", "Journey"},
  {"Skin", "Oingo Boingo"},
  {"Pictures Of You", "The Cure"},
  {"Every Breathe You Take", "The Police"},
  {"Lips Like Sugar","Echo And The Bunnymen"},
  {"Sweet Child O' Mine","Guns N' Roses"},
  {"Separate Ways (Worlds Apart)","Journey"},
  {"Maniac","Michael Sembello"},
  {"Promises, Promises","Naked Eyes"},
  {"There Is A Light That Never Goes Out","The Smiths"},
  {"Reptile","The Church"},
  {"The Ghost In You","The Psychedelic Furs"},
  {"No One Like You","Scorpions"},
  {"Beat It","Michael Jackson"},
  {"It's No Good","Depeche Mode"},
};

static const TrackInfo kTracks05[21] = {
  {"",""},
  {"Yesterday","The Beatles"},
  {"Don't Go Breaking My Heart","Elton John, Kiki Dee"},
  {"How Deep Is Your Love","Bee Gees"},
  {"The Winner Takes It All","ABBA"},
  {"Don't Stop Me Now","Queen"},
  {"Across the Universe","The Beatles"},
  {"Landslide","Fleetwood Mac"},
  {"The Air That I Breathe","The Hollies"},
  {"Somebody To Love","Queen"},
  {"Your Song","Elton John"},
  {"A Day In The Life","The Beatles"},
  {"Stairway to Heaven","Led Zeppelin"},
  {"Got To Be Real", "Cheryl Lynn"},
  {"Go Your Own Way","Fleetwood Mac"},
  {"Let's Groove", "Earth, Wind & Fire"},
  {"Happy Together","The Turtles"},
  {"More Than A Woman","Bee Gees"},
  {"Dancing Queen","ABBA"},
  {"Rocket Man (I Think It's Going To Be A Long Long Time)","Elton John"},
  {"Bohemain Rhapsody","Queen"},
};


static const TrackInfo kTracks06[84] = {
  {"",""},
  {"Helena", "My Chemical Romance"},
  {"What's It Feel Like To Be A Ghost?", "Taking Back Sunday"},
  {"Swing, Swing", "The All-American Rejects"},
  {"All Around Me", "Flyleaf"},
  {"Say It Ain't So", "Weezer"},
  {"When I Come Around", "Green Day"},
  {"Dammit", "Blink-182"},
  {"I Will Not Bow", "Breaking Benjamin"},
  {"Adam's Song", "Blink-182"},
  {"When It Rains", "Paramore"},
  {"I Miss You", "Blink-182"},
  {"Last Resort", "Papa Roach"},
  {"Disconnected", "Face To Face"},
  {"Nine in the Afternoon", "Panic! At The Disco"},
  {"Ready To Fall", "Rise Against"},
  {"Ides Of March", "Silverstein"},
  {"Liar (It Takes One to Know One)", "Taking Back Sunday"},
  {"Inside the Fire", "Disturbed"},
  {"Graveyard Dancing", "D.R.U.G.S."},
  {"Monsters", "Matchbook Romance"},
  {"I Won't Say The Lord's Prayer", "The Wonder Years"},
  {"My Blue Heaven", "Taking Back Sunday"},
  {"Smile In Your Sleep", "Silverstein"},
  {"Carl Barker", "Dance Gavin Dance"},
  {"Blurry", "Puddle Of Mudd"},
  {"The Good Left Undone", "Rise Against"},
  {"Here Without You", "3 Doors Down"},
  {"Brick by Boring Brick", "Paramore"},
  {"The Only Difference Between Martyrdom and Suicide Is Press Coverage", "Panic! At The Disco"},
  {"Cemetery Drive", "My Chemical Romance"},
  {"Stricken", "Disturbed"},
  {"Shadow of the Day", "Linkin Park"},
  {"Faint", "Linkin Park"},
  {"Sweetness", "Jimmy Eat World"},
  {"Holiday", "Green Day"},
  {"Sugar, We're Goin Down", "Fall Out Boy"},
  {"My Apocalypse", "Escape The Fate"},
  {"Uneasy Hearts Weigh The Most", "Dance Gavin Dance"},
  {"Bat Country", "Avenged Sevenfold"},
  {"Suitcase", "Circa Survive"},
  {"Love Like Winter", "AFI"},
  {"Miss Murder", "AFI"},
  {"NASA", "Dance Gavin Dance"},
  {"Mad", "Emarosa"},
  {"Same Tight Rope", "Emarosa"},
  {"If It Means A Lot To You", "A Day To Remember"},
  {"All Signs Point To Lauderdale", "A Day To Remember"},
  {"Turn Off the Lights", "Dance Gavin Dance"},
  {"Welcome Home", "Coheed and Cambria"},
  {"Dyed In The Wool", "Circa Survive"},
  {"So Cold", "Breaking Benjamin"},
  {"Lost", "Avenged Sevenfold"},
  {"Move Along", "The All-American Rejects"},
  {"This Conversation Is Over", "Alesana"},
  {"The Days Of The Phoenix", "AFI"},
  {"Slow Down", "The Academy Is..."},
  {"Forever Fades Away", "Tiger Army"},
  {"Reinventing Your Exit", "Underoath"},
  {"Blue and Yellow", "The Used"},
  {"Never Too Late", "Three Days Grace"},
  {"MakeDamnSure", "Taking Back Sunday"},
  {"The Kill", "30 Seconds To Mars"},
  {"If I'm James Dean, You're Audrey Hepburn", "Sleeping With Sirens"},
  {"Hey Nightmare, Where Did You Get Those Teeth?", "A Skylit Drive"},
  {"The Take Over, The Breaks Over", "Fall Out Boy"},
  {"Face Down", "The Red Jumpsuit Apparatus"},
  {"Prayer Of The Refugee", "Rise Against"},
  {"Caraphernelia", "Pierce The Veil"},
  {"Dance, Dance", "Fall Out Boy"},
  {"That's What You Get", "Paramore"},
  {"Heads or Tails, Real Or Not", "Emarosa"},
  {"F.C.P.R.E.M.I.X.", "The Fall Of Troy"},
  {"The Webs We Weave", "Escape The Fate"},
  {"Mr. Owl Ate My Metal Worm", "D.R.U.G.S."},
  {"Letter from a Thief", "Chevelle"},
  {"The Diary Of Jane", "Breaking Benjamin"},
  {"Afterlife", "Avenged Sevenfold"},
  {"A Single Moment Of Sincerity", "Asking Alexandria"},
  {"Congratulations, I Hate You", "Alesana"},
  {"Dirty Little Secret", "The All-American Rejects"},
  {"Camisado", "Panic! At The Disco"},
  {"Seduction", "Alesana"},
  {"Pathetic, Ordinary", "Alesana"},
};

// ── Station/track helpers ─────────────────────────────────────────────────

uint8_t radioTrackMaxForStation(uint8_t station) {
  if (station == 1) return 10;
  if (station == 2) return 20;
  if (station == 3) return 30;
  if (station == 4) return 41;
  if (station == 5) return 20;
  if (station == 6) return 83;
  return 10;
}

uint8_t radioClampTrack(uint8_t station, uint8_t track) {
  uint8_t tmax = radioTrackMaxForStation(station);
  if (track < 1 || track > tmax) return 1;
  return track;
}

const TrackInfo& radioGetTrackInfo(uint8_t station, uint8_t track) {
  track = radioClampTrack(station, track);
  if (station == 2) return kTracks02[track];
  if (station == 3) return kTracks03[track];
  if (station == 4) return kTracks04[track];
  if (station == 5) return kTracks05[track];
  if (station == 6) return kTracks06[track];
  return kTracks01[track];
}

// ── Module globals ────────────────────────────────────────────────────────

DFRobotDFPlayerMini dfp;
HardwareSerial &DFSerial = Serial1;

bool radioActive = false;
bool radioReady  = false;
bool radioPaused = false;
bool radioShuffle = false;

uint8_t radioStation = 1;
const uint8_t RADIO_STATION_MIN = 1;
const uint8_t RADIO_STATION_MAX = 6;

uint8_t radioTrack = 1;
const uint8_t RADIO_TRACK_MIN = 1;
const uint8_t RADIO_TRACK_MAX = 11;

int radioVolume = 22;
const int RADIO_VOL_MIN = 0;
const int RADIO_VOL_MAX = 30;

unsigned long radioIgnoreDfErrorsUntilMs = 0;

static const uint8_t RADIO_MAX_EVENTS_PER_LOOP = 8;

// Shuffle
static const uint8_t RADIO_SHUFFLE_MAX = 83;
static uint8_t radioShuffleQueue[RADIO_SHUFFLE_MAX];
static uint8_t radioShuffleLen = 0;
static uint8_t radioShufflePos = 0;
unsigned long radioShuffleOverlayUntilMs = 0;
static const unsigned long RADIO_SHUFFLE_OVERLAY_MS = 1800;

// Persist
struct RadioPersist {
  uint8_t station;
  uint8_t track;
  uint8_t volume;
  bool    active;
  bool    paused;
};
static RadioPersist gRadio = {1, 1, 22, false, false};
static bool radioStateDirty = false;
static unsigned long radioLastSaveMs = 0;
static const unsigned long RADIO_SAVE_DEBOUNCE_MS = 700;
static Preferences prefs;

// Marquee
int16_t radioMarqueeX = 0;
static unsigned long radioMarqueeLastMs = 0;
static unsigned long radioMarqueeHoldUntil = 0;
static uint8_t radioMarqueePhase = 0;

static const uint8_t  RADIO_CHAR_W = 6;
static const unsigned long RADIO_MARQUEE_STEP_MS = 250;
static const int16_t RADIO_MARQUEE_STEP_PX = 6;
static const unsigned long RADIO_MARQUEE_START_HOLD_MS = 900;
static const unsigned long RADIO_MARQUEE_END_HOLD_MS   = 700;

// UI animation
static unsigned long radioUiLastFrameMs = 0;
static const unsigned long RADIO_UI_FRAME_MS = 33;

uint8_t radioWavePhase = 0;
static unsigned long radioWaveLastMs = 0;
static const unsigned long RADIO_WAVE_STEP_MS = 45;
static const int RADIO_WAVE_BASE_Y = 38;
static const int RADIO_WAVE_AMP_PX = 6;
static const uint8_t RADIO_WAVE_PERIOD = 32;

unsigned long radioLastTrackChangeAt = 0;
static const unsigned long RADIO_TRACK_GUARD_MS = 600;

static bool cameraPausedRadio = false;

#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH 128
#endif
#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT 64
#endif

// ── Internal helpers ──────────────────────────────────────────────────────

static inline void radioMarkDirty() { radioStateDirty = true; }

void radioPersistFromGlobals() {
  gRadio.station = radioStation;
  gRadio.track   = radioTrack;
  gRadio.volume  = (uint8_t)radioVolume;
  gRadio.active  = radioActive;
  gRadio.paused  = radioPaused;
  radioMarkDirty();
}

static inline uint8_t radioNextTrack(uint8_t station, uint8_t t) {
  uint8_t tmax = radioTrackMaxForStation(station);
  return (t < tmax) ? (t + 1) : 1;
}

static inline uint8_t radioPrevTrack(uint8_t station, uint8_t t) {
  uint8_t tmax = radioTrackMaxForStation(station);
  return (t > 1) ? (t - 1) : tmax;
}

static inline uint8_t radioToggleStationIdx(uint8_t s) {
  return (s < RADIO_STATION_MAX) ? (s + 1) : RADIO_STATION_MIN;
}

// ── Shuffle ───────────────────────────────────────────────────────────────

static void radioShuffleBuild(uint8_t station, uint8_t currentTrack) {
  uint8_t tmax = radioTrackMaxForStation(station);
  radioShuffleLen = tmax;
  for (uint8_t i = 0; i < tmax; i++) radioShuffleQueue[i] = i + 1;
  for (uint8_t i = tmax - 1; i > 0; i--) {
    uint8_t j = (uint8_t)(random(i + 1));
    uint8_t tmp = radioShuffleQueue[i];
    radioShuffleQueue[i] = radioShuffleQueue[j];
    radioShuffleQueue[j] = tmp;
  }
  for (uint8_t i = 0; i < tmax; i++) {
    if (radioShuffleQueue[i] == currentTrack) {
      uint8_t tmp = radioShuffleQueue[0];
      radioShuffleQueue[0] = radioShuffleQueue[i];
      radioShuffleQueue[i] = tmp;
      break;
    }
  }
  radioShufflePos = 0;
}

static uint8_t radioShuffleNext() {
  radioShufflePos++;
  if (radioShufflePos >= radioShuffleLen) {
    uint8_t tmax = radioShuffleLen;
    for (uint8_t i = 0; i < tmax; i++) radioShuffleQueue[i] = i + 1;
    for (uint8_t i = tmax - 1; i > 0; i--) {
      uint8_t j = (uint8_t)(random(i + 1));
      uint8_t tmp = radioShuffleQueue[i];
      radioShuffleQueue[i] = radioShuffleQueue[j];
      radioShuffleQueue[j] = tmp;
    }
    radioShufflePos = 0;
  }
  return radioShuffleQueue[radioShufflePos];
}

void radioToggleShuffle() {
  if (!radioShuffle) {
    radioShuffle = true;
    radioShuffleBuild(radioStation, radioTrack);
  } else {
    radioShuffle = false;
  }
  radioShuffleOverlayUntilMs = millis() + RADIO_SHUFFLE_OVERLAY_MS;
}

// ── Persist ───────────────────────────────────────────────────────────────

void radioSaveIfNeeded(bool force) {
  unsigned long now = millis();
  if (!force) {
    if (!radioStateDirty) return;
    if (now - radioLastSaveMs < RADIO_SAVE_DEBOUNCE_MS) return;
  }
  prefs.begin("chipboy", false);
  prefs.putUChar("r_station", gRadio.station);
  prefs.putUChar("r_track",   gRadio.track);
  prefs.putUChar("r_vol",     gRadio.volume);
  prefs.putBool("r_active",   gRadio.active);
  prefs.putBool("r_paused",   gRadio.paused);
  prefs.end();
  radioLastSaveMs = now;
  radioStateDirty = false;
}

void radioLoadPersisted() {
  prefs.begin("chipboy", true);
  gRadio.station = prefs.getUChar("r_station", RADIO_STATION_MIN);
  gRadio.track   = prefs.getUChar("r_track",   RADIO_TRACK_MIN);
  gRadio.volume  = prefs.getUChar("r_vol",     22);
  gRadio.active  = prefs.getBool("r_active",   false);
  gRadio.paused  = prefs.getBool("r_paused",   false);
  prefs.end();

  if (gRadio.station < RADIO_STATION_MIN || gRadio.station > RADIO_STATION_MAX)
    gRadio.station = RADIO_STATION_MIN;
  gRadio.track = radioClampTrack(gRadio.station, gRadio.track);
  if (gRadio.volume > RADIO_VOL_MAX) gRadio.volume = RADIO_VOL_MAX;

  radioStation = gRadio.station;
  radioTrack   = gRadio.track;
  radioVolume  = gRadio.volume;
  radioActive  = gRadio.active;
  radioPaused  = gRadio.paused;
}

// ── Init / core ───────────────────────────────────────────────────────────

void dfplayerHardStopAndFlush() {
  while (DFSerial.available()) DFSerial.read();
  delay(20);
  while (DFSerial.available()) DFSerial.read();
}

void radioEnsureInit() {
  if (radioReady) return;
  DFSerial.begin(9600, SERIAL_8N1, D10, D9);
  dfplayerHardStopAndFlush();
  if (dfp.begin(DFSerial, false, false)) {
    radioReady = true;
    dfp.stop();
    delay(70);
    dfp.volume(radioVolume);
    dfp.EQ(DFPLAYER_EQ_BASS);
    if (radioActive && !radioPaused) {
      radioPlayTrack(radioStation, radioTrack);
    }
  } else {
    radioReady = false;
  }
}

void radioRecoverAfterCamera() {
  radioReady = false;
  DFSerial.end();
  delay(20);
  DFSerial.begin(9600, SERIAL_8N1, D10, D9);
  dfplayerHardStopAndFlush();
  radioEnsureInit();
}

void radioPlayTrack(uint8_t station, uint8_t track) {
  if (!radioReady) {
    radioEnsureInit();
    if (!radioReady) return;
  }
  dfp.stop();
  delay(30);
  dfplayerHardStopAndFlush();
  delay(20);
  dfp.playFolder(station, track);
  radioLastTrackChangeAt = millis();
  radioPaused = false;
  radioPersistFromGlobals();
}

// ── Marquee ───────────────────────────────────────────────────────────────

void radioResetMarquee() {
  radioMarqueeX = 0;
  radioMarqueeLastMs = 0;
  radioMarqueeHoldUntil = 0;
  radioMarqueePhase = 0;
}

// ── Skip ──────────────────────────────────────────────────────────────────

void radioSkipNext() {
  if (!radioReady) { radioEnsureInit(); if (!radioReady) return; }
  radioTrack = radioShuffle ? radioShuffleNext() : radioNextTrack(radioStation, radioTrack);
  radioResetMarquee();
  if (radioActive) { radioPaused = false; radioPlayTrack(radioStation, radioTrack); }
  radioPersistFromGlobals();
  if (inRadio) { uiDirty = true; redrawRadioNow(); }
}

void radioSkipPrev() {
  if (!radioReady) { radioEnsureInit(); if (!radioReady) return; }
  radioTrack = radioPrevTrack(radioStation, radioTrack);
  radioResetMarquee();
  if (radioActive) { radioPaused = false; radioPlayTrack(radioStation, radioTrack); }
  radioPersistFromGlobals();
  if (inRadio) { uiDirty = true; redrawRadioNow(); }
}

// ── Volume ────────────────────────────────────────────────────────────────

void radioSetVolume(int v) {
  v = constrain(v, RADIO_VOL_MIN, RADIO_VOL_MAX);
  radioVolume = v;
  radioPersistFromGlobals();
  if (radioReady && radioActive) dfp.volume(radioVolume);
}

// ── Station switch ────────────────────────────────────────────────────────

void radioSwitchToStation(uint8_t newStation) {
  if (!radioReady) { radioEnsureInit(); if (!radioReady) return; }
  if (newStation < RADIO_STATION_MIN || newStation > RADIO_STATION_MAX)
    newStation = RADIO_STATION_MIN;

  bool wasActive = radioActive;
  radioStation = newStation;
  radioTrack   = RADIO_TRACK_MIN;
  radioResetMarquee();
  if (radioShuffle) radioShuffleBuild(newStation, RADIO_TRACK_MIN);
  radioPersistFromGlobals();

  if (!wasActive) { if (inRadio) { uiDirty = true; redrawRadioNow(); } return; }

  radioPaused = false;
  radioActive = true;
  radioIgnoreDfErrorsUntilMs = millis() + 2500;

  dfp.stop();
  delay(25);
  dfplayerHardStopAndFlush();
  delay(10);
  dfp.playFolder(radioStation, radioTrack);
  radioLastTrackChangeAt = millis();

  if (inRadio) { uiDirty = true; redrawRadioNow(); }
}

void radioToggleStation() {
  radioSwitchToStation(radioToggleStationIdx(radioStation));
}

void radioGoToStation1() {
  radioSwitchToStation(RADIO_STATION_MIN);
}

// ── Pause/Play ────────────────────────────────────────────────────────────

void radioTogglePausePlay(bool requestUiRedraw) {
  if (!radioReady) { radioEnsureInit(); if (!radioReady) return; }
  if (!radioActive) {
    radioActive = true;
    radioPaused = false;
    radioPersistFromGlobals();
    radioPlayTrack(radioStation, radioTrack);
    if (inRadio && requestUiRedraw) { uiDirty = true; redrawRadioNow(); }
    return;
  }
  if (!radioPaused) { dfp.pause(); radioPaused = true; }
  else              { dfp.start(); radioPaused = false; }
  radioPersistFromGlobals();
  if (inRadio && requestUiRedraw) { uiDirty = true; redrawRadioNow(); }
}

// ── Start ─────────────────────────────────────────────────────────────────

void radioStart() {
  static bool loaded = false;
  if (!loaded) { loaded = true; radioLoadPersisted(); }
  radioEnsureInit();
  if (!radioReady) return;
  radioActive = true;
  radioPaused = false;
  radioPlayTrack(radioStation, radioTrack);
  radioPersistFromGlobals();
  radioSaveIfNeeded(true);
}

// ── Service ───────────────────────────────────────────────────────────────

void radioService() {
  unsigned long now = millis();

  if (!radioReady) {
    if (radioActive || inRadio) {
      radioEnsureInit();
      if (radioReady) {
        dfp.volume(radioVolume);
        dfp.EQ(DFPLAYER_EQ_ROCK);
        dfp.stop();
        delay(30);
        dfplayerHardStopAndFlush();
        if (radioActive && !radioPaused) radioPlayTrack(radioStation, radioTrack);
      }
    }
    return;
  }

  uint8_t processed = 0;
  while (dfp.available() && processed < RADIO_MAX_EVENTS_PER_LOOP) {
    processed++;
    uint8_t type = dfp.readType();
    int value    = dfp.read();

    if (type == DFPlayerCardOnline) {
      radioReady = false;
      dfplayerHardStopAndFlush();
      break;
    }
    if (type == DFPlayerError) {
      if (millis() < radioIgnoreDfErrorsUntilMs) continue;
      if (millis() - radioLastTrackChangeAt < 1500) continue;
      radioReady = false;
      dfplayerHardStopAndFlush();
      break;
    }
    if (type == DFPlayerPlayFinished) {
      if (!radioActive || radioPaused) continue;
      if (now - radioLastTrackChangeAt < RADIO_TRACK_GUARD_MS) continue;
      radioTrack = radioShuffle ? radioShuffleNext() : radioNextTrack(radioStation, radioTrack);
      radioResetMarquee();
      radioPlayTrack(radioStation, radioTrack);
      if (inRadio) uiDirty = true;
    }
  }
}

// ── UI service ────────────────────────────────────────────────────────────

void radioUiService() {
  if (!inRadio || !displayReady || !screenEnabled) return;
  unsigned long now = millis();
  if (now - radioUiLastFrameMs < RADIO_UI_FRAME_MS) return;
  radioUiLastFrameMs = now;

  bool changed = false;
  if (millis() < radioShuffleOverlayUntilMs) changed = true;

  const char* title = radioGetTrackInfo(radioStation, radioTrack).title;
  int16_t titleW = (int16_t)strlen(title) * (int16_t)RADIO_CHAR_W;

  if (titleW > SCREEN_WIDTH) {
    const int16_t minX = (int16_t)SCREEN_WIDTH - titleW;
    if (radioMarqueePhase == 0) {
      radioMarqueeX = 0;
      radioMarqueeLastMs = now;
      radioMarqueeHoldUntil = now + RADIO_MARQUEE_START_HOLD_MS;
      radioMarqueePhase = 1;
      changed = true;
    }
    if (radioMarqueePhase == 1) {
      if (now >= radioMarqueeHoldUntil) { radioMarqueePhase = 2; radioMarqueeLastMs = now; }
    }
    if (radioMarqueePhase == 2) {
      while (now - radioMarqueeLastMs >= RADIO_MARQUEE_STEP_MS) {
        radioMarqueeLastMs += RADIO_MARQUEE_STEP_MS;
        if (radioMarqueeX > minX) {
          radioMarqueeX -= RADIO_MARQUEE_STEP_PX;
          if (radioMarqueeX < minX) radioMarqueeX = minX;
          changed = true;
          if (radioMarqueeX == minX) {
            radioMarqueeHoldUntil = now + RADIO_MARQUEE_END_HOLD_MS;
            radioMarqueePhase = 3;
            break;
          }
        }
      }
    }
    if (radioMarqueePhase == 3) {
      if (now >= radioMarqueeHoldUntil) {
        radioMarqueeX = 0;
        radioMarqueeHoldUntil = now + RADIO_MARQUEE_START_HOLD_MS;
        radioMarqueePhase = 1;
        radioMarqueeLastMs = now;
        changed = true;
      }
    }
  } else {
    if (radioMarqueeX != 0 || radioMarqueePhase != 0 || radioMarqueeLastMs != 0 || radioMarqueeHoldUntil != 0) {
      radioMarqueeX = 0; radioMarqueeLastMs = 0; radioMarqueeHoldUntil = 0; radioMarqueePhase = 0;
      changed = true;
    }
  }

  if (radioActive && !radioPaused) {
    if (radioWaveLastMs == 0) {
      radioWaveLastMs = now;
    } else {
      while (now - radioWaveLastMs >= RADIO_WAVE_STEP_MS) {
        radioWaveLastMs += RADIO_WAVE_STEP_MS;
        radioWavePhase = (uint8_t)((radioWavePhase + 1) & (RADIO_WAVE_PERIOD - 1));
        changed = true;
      }
    }
  } else {
    radioWaveLastMs = 0;
  }

  if (changed) uiDirty = true;
}

// ── Draw ──────────────────────────────────────────────────────────────────

void radioDrawSineWave() {
  int prevY = -1;
  for (int x = 0; x < SCREEN_WIDTH; x++) {
    uint8_t idx = (uint8_t)((x + radioWavePhase) & (RADIO_WAVE_PERIOD - 1));
    int y = RADIO_WAVE_BASE_Y + (kSin32[idx] * RADIO_WAVE_AMP_PX) / 127;
    if (y < 0) y = 0;
    if (y > 52) y = 52;
    if (x > 0 && prevY >= 0) display.drawLine(x - 1, prevY, x, y, WHITE);
    else display.drawPixel(x, y, WHITE);
    prevY = y;
  }
}

void drawRadioNowPlaying() {
  if (!displayReady) return;
  display.clearDisplay();
  display.setTextWrap(false);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  const TrackInfo &ti = radioGetTrackInfo(radioStation, radioTrack);
  const char* title  = ti.title;
  const char* artist = ti.artist;

  const int16_t titleY = 0;
  int16_t titleW = (int16_t)strlen(title) * 6;
  if (titleW <= SCREEN_WIDTH) { display.setCursor(0, titleY); display.print(title); }
  else { display.setCursor(radioMarqueeX, titleY); display.print(title); }

  const int16_t artistY = 12;
  display.setCursor(0, artistY);
  display.print(artist);

  radioDrawSineWave();

  int iconCx = SCREEN_WIDTH / 2;
  int iconCy = 56;
  display.setTextSize(1);
  const int CHAR_W = 6;
  const int ARROW_OFFSET = 14;

  display.setCursor(iconCx - ARROW_OFFSET - CHAR_W / 2, iconCy - 3);
  display.print("<");
  display.setCursor(iconCx + ARROW_OFFSET - CHAR_W / 2, iconCy - 3);
  display.print(">");

  if (radioActive && !radioPaused) {
    display.fillRect(iconCx - 5, iconCy - 3, 3, 7, SSD1306_WHITE);
    display.fillRect(iconCx + 2, iconCy - 3, 3, 7, SSD1306_WHITE);
  } else {
    display.fillTriangle(iconCx - 4, iconCy - 3, iconCx - 4, iconCy + 3, iconCx + 4, iconCy, SSD1306_WHITE);
  }

  display.setCursor(0, 55);
  display.print("STAT");
  display.print((int)radioStation);

  display.setCursor(SCREEN_WIDTH - (6 * 4), 55);
  display.print("EXIT");

  if (radioShuffle) {
    for (int r = 0; r < 8; r++)
      for (int c = 0; c < 10; c++)
        if (pgm_read_byte(&kShuffleIcon[r][c]))
          display.drawPixel(0 + c, 22 + r, SSD1306_WHITE);
  }

  if (millis() < radioShuffleOverlayUntilMs) {
    const char* msg = radioShuffle ? "SHUFFLE ON" : "SHUFFLE OFF";
    int msgW  = strlen(msg) * 6;
    int msgX  = (SCREEN_WIDTH - msgW) / 2;
    int msgY  = 25;
    display.fillRect(msgX - 4, msgY - 3, msgW + 8, 14, SSD1306_BLACK);
    display.drawRect(msgX - 4, msgY - 3, msgW + 8, 14, SSD1306_WHITE);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(msgX, msgY);
    display.print(msg);
  }

  display.display();
}

void redrawRadioNow() {
  if (screenEnabled && displayReady) {
    drawRadioNowPlaying();
    uiDirty = false;
  }
}

// ── Nav skip ──────────────────────────────────────────────────────────────

void radioNavSkipService() {
  if (!inRadio) return;
  if (btnWasShortPressed(bRight)) radioSkipNext();
  if (btnWasShortPressed(bLeft))  radioSkipPrev();
}