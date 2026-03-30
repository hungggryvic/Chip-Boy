#pragma once
#include <Arduino.h>

// ============================================================================
// CAMERA APP
// ============================================================================

extern bool inCamera;
extern bool camera_ok;
extern bool sd_ok;
extern int imageCount;

void startCameraApp();
void updateCameraApp();
void initCameraModule();
void deinitCameraModule();
void cameraDrawIdle();
void cameraFlash();
bool cameraSaveOne();
void cameraPreviewOnOLED();