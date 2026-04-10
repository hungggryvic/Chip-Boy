#pragma once
#include <Arduino.h>

void slotsEnter();      // called once when Slots is selected from Games menu
void slotsUpdate();     // called every frame while in Slots
void slotsExit();       // called once when leaving Slots

bool slotsIsActive();