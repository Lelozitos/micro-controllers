#pragma once

// Thin wrapper over pigpio for the buttons/LEDs/buzzer.
namespace gpio_io {

bool init();  // call once; returns false on failure
void shutdown();

void setRunningIndicator(bool running);  // red on/green on
void beep(int duration_ms = 80);

}  // namespace gpio_io
