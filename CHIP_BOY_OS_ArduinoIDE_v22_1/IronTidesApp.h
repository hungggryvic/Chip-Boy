#pragma once
#include <Arduino.h>

// App lifecycle
void ironTidesEnter();   // called once when GAME is selected
void ironTidesUpdate();  // called every frame while in GAME
void ironTidesExit();    // called once when leaving GAME

// Optional helpers
bool ironTidesIsActive();
