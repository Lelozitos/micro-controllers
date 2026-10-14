#pragma once

// Thin wrapper over pigpio for the buttons/LEDs/buzzer.
namespace gpio_io {

bool init();  // call once; returns false on failure
void shutdown();

void setRunningIndicator(bool running);  // green on while running, red on while ready
void setReadyIndicator(bool ready);      // red "can start" LED only
void setCountdownLights(int lit);        // light the first `lit` countdown LEDs, rest off
void beep(int duration_ms = 80);

}  // namespace gpio_io
