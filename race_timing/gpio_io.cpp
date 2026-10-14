#include "gpio_io.hpp"

#include <pigpio.h>

#include "config.hpp"

namespace gpio_io {

bool init() {
    if (gpioInitialise() < 0) return false;

    gpioSetMode(config::READY_LED_PIN, PI_OUTPUT);
    gpioSetMode(config::RUNNING_LED_PIN, PI_OUTPUT);
    gpioSetMode(config::BUZZER_PIN, PI_OUTPUT);
    for (int pin : config::COUNTDOWN_LED_PINS) {
        gpioSetMode(pin, PI_OUTPUT);
        gpioWrite(pin, 0);
    }

    gpioWrite(config::READY_LED_PIN, 1);  // idle = red on, ready to start
    gpioWrite(config::RUNNING_LED_PIN, 0);
    gpioWrite(config::BUZZER_PIN, 0);

    gpioSetMode(config::START_BUTTON_PIN, PI_INPUT);
    gpioSetPullUpDown(config::START_BUTTON_PIN, PI_PUD_UP);
    gpioSetMode(config::RESET_BUTTON_PIN, PI_INPUT);
    gpioSetPullUpDown(config::RESET_BUTTON_PIN, PI_PUD_UP);

    return true;
}

void shutdown() {
    gpioWrite(config::READY_LED_PIN, 0);
    gpioWrite(config::RUNNING_LED_PIN, 0);
    setCountdownLights(0);
    gpioTerminate();
}

void setRunningIndicator(bool running) {
    gpioWrite(config::READY_LED_PIN, running ? 0 : 1);
    gpioWrite(config::RUNNING_LED_PIN, running ? 1 : 0);
}

void setReadyIndicator(bool ready) { gpioWrite(config::READY_LED_PIN, ready ? 1 : 0); }

void setCountdownLights(int lit) {
    int i = 0;
    for (int pin : config::COUNTDOWN_LED_PINS) gpioWrite(pin, i++ < lit ? 1 : 0);
}

void beep(int duration_ms) {
    gpioWrite(config::BUZZER_PIN, 1);
    gpioDelay(duration_ms * 1000);
    gpioWrite(config::BUZZER_PIN, 0);
}

}  // namespace gpio_io
