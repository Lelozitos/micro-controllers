#pragma once

// Thin wrapper over pigpio for the buttons/LEDs.
namespace gpio_io {

bool init();  // call once; returns false on failure
void shutdown();

void setRunningIndicator(bool running);  // green on while running, red on while ready
void setReadyIndicator(bool ready);      // red "can start" LED only
void setCountdownLed(int index, bool on);  // index into config::COUNTDOWN_LED_PINS
void allCountdownOff();
void blinkRunning();                     // short green blink - "finish recorded"

}  // namespace gpio_io
