#pragma once
#include <Arduino.h>

void skyEnter();       // called once when Sky is selected from menu
void skyUpdate();      // called every frame while Sky is active
void skyExit();        // called once when leaving Sky

bool skyIsActive();