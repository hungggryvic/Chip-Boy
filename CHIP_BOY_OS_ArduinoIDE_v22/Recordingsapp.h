#pragma once

// ============================================================================
// RecordingsApp.h
// Plays MP3s from folder "00" on the DFPlayer SD card.
// Mirrors the Radio app UI: title marquee, sine-wave animation,
// play/pause + prev/next icons, rotary encoder for volume + play/pause/prev/next.
// Stops playback immediately when the user exits the app.
// ============================================================================

// Call once from inside the RECORDINGS menu selection block:
void recordingsEnter();

// Call every loop iteration while inRecordings == true:
void recordingsService();

// Draw the now-playing screen (call when uiDirty, or after a command):
void redrawRecordingsNow();

// Exposed so encSwitchGestureService() can check it (same way inRadio is used):
extern bool inRecordings;

// Called from encSwitchGestureService() in the main file:
void recordingsEncToggle();
void recordingsEncNext();
void recordingsEncPrev();