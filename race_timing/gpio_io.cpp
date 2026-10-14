#include "gpio_io.hpp"

#include <pigpio.h>

#include "config.hpp"

namespace gpio_io {

bool init() {
    if (gpioInitialise() < 0) return false;

    gpioSetMode(config::READY_LED_PIN, PI_OUTPUT);
    gpioSetMode(config::RUNNING_LED_PIN, PI_OUTPUT);
    for (int pin : config::COUNTDOWN_LED_PINS) {
        gpioSetMode(pin, PI_OUTPUT);
        gpioWrite(pin, 0);
    }

    gpioWrite(config::READY_LED_PIN, 1);  // idle = red on, ready to start
    gpioWrite(config::RUNNING_LED_PIN, 0);

    // RC522 RST is active low: hold it high so the reader runs.
    gpioSetMode(config::RFID_RST_PIN, PI_OUTPUT);
    gpioWrite(config::RFID_RST_PIN, 1);

    gpioSetMode(config::START_BUTTON_PIN, PI_INPUT);
    gpioSetPullUpDown(config::START_BUTTON_PIN, PI_PUD_UP);
    gpioSetMode(config::RESET_BUTTON_PIN, PI_INPUT);
    gpioSetPullUpDown(config::RESET_BUTTON_PIN, PI_PUD_UP);

    return true;
}

void shutdown() {
    gpioWrite(config::READY_LED_PIN, 0);
    gpioWrite(config::RUNNING_LED_PIN, 0);
    allCountdownOff();
    gpioTerminate();
}

void setRunningIndicator(bool running) {
    gpioWrite(config::READY_LED_PIN, running ? 0 : 1);
    gpioWrite(config::RUNNING_LED_PIN, running ? 1 : 0);
}

void setReadyIndicator(bool ready) { gpioWrite(config::READY_LED_PIN, ready ? 1 : 0); }

void setCountdownLed(int index, bool on) {
    gpioWrite(config::COUNTDOWN_LED_PINS[index], on ? 1 : 0);
}

void allCountdownOff() {
    for (int pin : config::COUNTDOWN_LED_PINS) gpioWrite(pin, 0);
}

void blinkRunning() {
    gpioWrite(config::RUNNING_LED_PIN, 0);
    gpioDelay(150000);
    gpioWrite(config::RUNNING_LED_PIN, 1);
}

}  // namespace gpio_io
