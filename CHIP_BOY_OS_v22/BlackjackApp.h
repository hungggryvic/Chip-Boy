#pragma once
#include <Arduino.h>

// App lifecycle — mirrors IronTidesApp.h exactly
void blackjackEnter();    // called once when Blackjack is selected from Games menu
void blackjackUpdate();   // called every frame while in Blackjack
void blackjackExit();     // called once when leaving Blackjack

// Optional helper
bool blackjackIsActive();
