#pragma once
#include <Arduino.h>

// ============================================================================
// NAVIGATION APP
// ============================================================================

extern bool inNavigation;

void navigationEnter();
void navigationExit();
void handleNavigation();

// Distance / bearing helpers (also used by nav beep service internally)
double navDistanceMeters(double lat1, double lon1, double lat2, double lon2);
double navBearingDeg(double lat1, double lon1, double lat2, double lon2);