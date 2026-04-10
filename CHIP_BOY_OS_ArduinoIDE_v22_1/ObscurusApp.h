#pragma once
#include <Arduino.h>

// App lifecycle 
void obscurusEnter();    // called once when Obscurus is selected from Games menu
void obscurusUpdate();   // called every frame while in Obscurus
void obscurusExit();     // called once when leaving Obscurus

// Optional helper
bool obscurusIsActive();
