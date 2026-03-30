#pragma once
#include <Arduino.h>

// App lifecycle 
void racingEnter();    // called once when Night Drive is selected from Games menu
void racingUpdate();   // called every frame while Night Drive is active
void racingExit();     // called once when leaving Night Drive

bool racingIsActive();