#pragma once
#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>
#include <Preferences.h>

// ============================================================================
// RADIO APP
// ============================================================================

struct TrackInfo { const char* title; const char* artist; };

// Station/track helpers
uint8_t radioTrackMaxForStation(uint8_t station);
uint8_t radioClampTrack(uint8_t station, uint8_t track);
const TrackInfo& radioGetTrackInfo(uint8_t station, uint8_t track);

// Globals
extern DFRobotDFPlayerMini dfp;
extern HardwareSerial &DFSerial;

extern bool radioActive;
extern bool radioReady;
extern bool radioPaused;
extern bool radioShuffle;

extern uint8_t radioStation;
extern int radioVolume;
extern uint8_t radioTrack;

extern const uint8_t RADIO_STATION_MIN;
extern const uint8_t RADIO_STATION_MAX;
extern const uint8_t RADIO_TRACK_MIN;
extern const uint8_t RADIO_TRACK_MAX;
extern const int RADIO_VOL_MIN;
extern const int RADIO_VOL_MAX;

extern unsigned long radioIgnoreDfErrorsUntilMs;

// Shuffle overlay
extern unsigned long radioShuffleOverlayUntilMs;

// Marquee state
extern int16_t radioMarqueeX;

// Wave phase
extern uint8_t radioWavePhase;

extern unsigned long radioLastTrackChangeAt;

// Functions
void radioEnsureInit();
void dfplayerHardStopAndFlush();
void radioLoadPersisted();
void radioSaveIfNeeded(bool force = false);
void radioStart();
void radioService();
void radioUiService();
void radioSetVolume(int v);
void radioSkipNext();
void radioSkipPrev();
void radioToggleStation();
void radioSwitchToStation(uint8_t newStation);
void radioTogglePausePlay(bool requestUiRedraw);
void radioToggleShuffle();
void radioResetMarquee();
void radioNavSkipService();
void radioRecoverAfterCamera();
void drawRadioNowPlaying();
void redrawRadioNow();
void radioDrawSineWave();

// Functions needed by CameraApp and MicrophoneApp
void radioPlayTrack(uint8_t station, uint8_t track);
void radioPersistFromGlobals();

// Sine wave LUT (used by MicrophoneApp)
extern const int8_t kSin32[32];