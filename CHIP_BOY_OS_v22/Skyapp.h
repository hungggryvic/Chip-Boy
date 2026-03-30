#pragma once
#include <Arduino.h>

// App lifecycle — mirrors IronTidesApp.h / ObscurusApp.h exactly
void skyEnter();       // called once when Sky is selected from menu
void skyUpdate();      // called every frame while Sky is active
void skyExit();        // called once when leaving Sky

bool skyIsActive();