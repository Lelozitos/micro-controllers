#include "gpio_io.hpp"

#include <pigpio.h>

#include "config.hpp"

namespace gpio_io {

bool init() {
    if (gpioInitialise() < 0) return false;

    gpioSetMode(config::STATUS_LED_PIN, PI_OUTPUT);
    gpioSetMode(config::ALERT_LED_PIN, PI_OUTPUT);
    gpioSetMode(config::BUZZER_PIN, PI_OUTPUT);

    gpioWrite(config::STATUS_LED_PIN, 1);  // idle = green on
    gpioWrite(config::ALERT_LED_PIN, 0);
    gpioWrite(config::BUZZER_PIN, 0);

    gpioSetMode(config::BEAM_PIN, PI_INPUT);
    gpioSetPullUpDown(config::BEAM_PIN, PI_PUD_UP);

    gpioSetMode(config::START_BUTTON_PIN, PI_INPUT);
    gpioSetPullUpDown(config::START_BUTTON_PIN, PI_PUD_UP);
    gpioSetMode(config::RESET_BUTTON_PIN, PI_INPUT);
    gpioSetPullUpDown(config::RESET_BUTTON_PIN, PI_PUD_UP);

    return true;
}

void shutdown() { gpioTerminate(); }

void setRunningIndicator(bool running) {
    gpioWrite(config::STATUS_LED_PIN, running ? 0 : 1);
    gpioWrite(config::ALERT_LED_PIN, running ? 1 : 0);
}

void beep(int duration_ms) {
    gpioWrite(config::BUZZER_PIN, 1);
    gpioDelay(duration_ms * 1000);
    gpioWrite(config::BUZZER_PIN, 0);
}

}  // namespace gpio_io
