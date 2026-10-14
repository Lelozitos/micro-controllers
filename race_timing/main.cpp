#include <pigpio.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "config.hpp"
#include "gpio_io.hpp"
#include "mqtt_publisher.hpp"
#include "race_manager.hpp"
#include "rfid_reader.hpp"

namespace {

enum class Phase { Idle, Countdown, Running };

RaceManager* g_race = nullptr;
MqttPublisher* g_mqtt = nullptr;
std::atomic<Phase> g_phase{Phase::Idle};
std::atomic<bool> g_start_requested{false};
std::atomic<bool> g_reset_requested{false};
std::atomic<bool> g_quit{false};

// RFID reader thread: the first read of a tag during a race is that racer's finish.
void onTag(const std::string& tag) {
    if (g_phase != Phase::Running) return;
    Crossing c = g_race->recordCrossing(tag);
    if (c.place == 0) return;  // already finished (reader repeats while tag is in range)

    std::printf("[#%d] %s  %.3fs\n", c.place, c.racer_name.c_str(), c.elapsed_s);
    gpio_io::beep();
    g_mqtt->publishCrossing(c.place, c.racer_name, c.elapsed_s);
}

// Button callbacks run on pigpio's alert thread - keep them short and let
// the main loop do the work.
void startButtonISR(int /*gpio*/, int level, uint32_t /*tick*/, void* /*userdata*/) {
    if (level == 0 && g_phase == Phase::Idle) g_start_requested = true;
}

void resetButtonISR(int /*gpio*/, int level, uint32_t /*tick*/, void* /*userdata*/) {
    if (level == 0) g_reset_requested = true;
}

void onSignal(int /*signum*/) { g_quit = true; }

void doReset() {
    g_reset_requested = false;
    g_start_requested = false;
    if (g_phase == Phase::Running) g_race->saveLog();
    g_race->reset();
    g_phase = Phase::Idle;
    gpio_io::setCountdownLights(0);
    gpio_io::setRunningIndicator(false);
    g_mqtt->publishState("idle");
    std::printf("Race reset - ready\n");
}

// Lights the countdown LEDs one by one; all off = GO. Returns false if
// RESET was pressed mid-countdown.
bool runCountdown() {
    g_phase = Phase::Countdown;
    gpio_io::setReadyIndicator(false);
    std::printf("Countdown...\n");

    constexpr int n = sizeof(config::COUNTDOWN_LED_PINS) / sizeof(config::COUNTDOWN_LED_PINS[0]);
    for (int lit = 1; lit <= n; ++lit) {
        gpio_io::setCountdownLights(lit);
        gpio_io::beep(150);
        std::this_thread::sleep_for(std::chrono::milliseconds(config::COUNTDOWN_STEP_MS - 150));
        if (g_reset_requested || g_quit) return false;
    }
    gpio_io::setCountdownLights(0);
    return true;
}

void startRace() {
    g_race->startRace();
    g_phase = Phase::Running;
    gpio_io::setRunningIndicator(true);
    g_mqtt->publishState("running");
    std::printf("GO! Race started\n");
    gpio_io::beep(400);
}

}  // namespace

int main() {
    if (!gpio_io::init()) {
        std::fprintf(stderr, "pigpio init failed (run as root / check daemon)\n");
        return 1;
    }
    gpioSetSignalFunc(SIGINT, onSignal);
    gpioSetSignalFunc(SIGTERM, onSignal);

    RaceManager race(config::RACERS_FILE, config::LOG_FILE);
    MqttPublisher mqtt(config::MQTT_HOST, config::MQTT_PORT, config::MQTT_TOKEN,
                        config::DEVICE_LABEL);
    g_race = &race;
    g_mqtt = &mqtt;
    RfidReader reader(config::RFID_PORT, onTag);
    if (!reader.isOpen()) {
        std::fprintf(stderr, "warning: no RFID reader on %s - finishes can't be recorded\n",
                     config::RFID_PORT.c_str());
    }

    // Alert callbacks, not gpioSetISRFunc: ISRs go through sysfs GPIO, which
    // newer kernels number from 512, so they silently fail to register.
    gpioGlitchFilter(config::START_BUTTON_PIN, 30000);
    gpioGlitchFilter(config::RESET_BUTTON_PIN, 30000);
    gpioSetAlertFuncEx(config::START_BUTTON_PIN, startButtonISR, nullptr);
    gpioSetAlertFuncEx(config::RESET_BUTTON_PIN, resetButtonISR, nullptr);

    mqtt.publishState("idle");
    std::printf("Race timer ready (red light). Press START to begin.\n");

    while (!g_quit) {
        if (g_reset_requested) {
            doReset();
        } else if (g_start_requested) {
            g_start_requested = false;
            if (runCountdown()) {
                startRace();
            } else if (!g_quit) {
                doReset();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    std::printf("\nShutting down\n");
    if (g_phase == Phase::Running) race.saveLog();
    gpio_io::shutdown();
    // The RFID thread may be blocked in read(); don't wait to join it.
    std::_Exit(0);
}
