#pragma once

// ============================================================
//  BatteryApp.h — Battery voltage monitor
//
//  batteryBackgroundSample() — call every loop from main sketch
//                              regardless of active app.
//  batteryEnter/Exit/drawBatteryScreen — normal app lifecycle.
//  BTN3 exit handled in main sketch's if(inBattery) block.
// ============================================================

void batteryBackgroundSample();  // call unconditionally each loop
void batteryEnter();
void batteryExit();
void drawBatteryScreen();