#include <pigpio.h>

#include <chrono>
#include <cstdio>
#include <thread>

#include "config.hpp"
#include "gpio_io.hpp"
#include "mqtt_publisher.hpp"
#include "race_manager.hpp"
#include "rfid_reader.hpp"

namespace {

RaceManager* g_race = nullptr;
MqttPublisher* g_mqtt = nullptr;
RfidReader* g_reader = nullptr;
std::chrono::steady_clock::time_point g_last_trigger{};
bool g_has_triggered = false;

void handleCrossing() {
    auto now = std::chrono::steady_clock::now();
    if (g_has_triggered) {
        double since = std::chrono::duration<double>(now - g_last_trigger).count();
        if (since < config::MIN_CROSSING_GAP_S) return;
    }
    g_last_trigger = now;
    g_has_triggered = true;

    std::string tag = g_reader->lastTagWithin(config::TAG_MATCH_WINDOW_S);
    Crossing c = g_race->recordCrossing(tag);
    if (c.place == 0) return;  // race not running

    std::printf("[#%d] %s  %.3fs\n", c.place, c.racer_name.c_str(), c.elapsed_s);
    gpio_io::beep();
    g_mqtt->publishCrossing(c.place, c.racer_name, c.elapsed_s);
}

void beamISR(int /*gpio*/, int level, uint32_t /*tick*/, void* /*userdata*/) {
    if (level != 0) return;  // only care about the falling edge (beam broken)
    handleCrossing();
}

void startButtonISR(int /*gpio*/, int level, uint32_t /*tick*/, void* /*userdata*/) {
    if (level != 0 || g_race->isRunning()) return;
    g_race->startRace();
    g_has_triggered = false;
    gpio_io::setRunningIndicator(true);
    gpio_io::beep(300);
    g_mqtt->publishState("running");
    std::printf("Race started\n");
}

void resetButtonISR(int /*gpio*/, int level, uint32_t /*tick*/, void* /*userdata*/) {
    if (level != 0) return;
    if (g_race->isRunning()) {
        g_race->saveLog();
    }
    g_race->reset();
    gpio_io::setRunningIndicator(false);
    g_mqtt->publishState("idle");
    std::printf("Race reset\n");
}

}  // namespace

int main() {
    if (!gpio_io::init()) {
        std::fprintf(stderr, "pigpio init failed (run as root / check daemon)\n");
        return 1;
    }

    RaceManager race(config::RACERS_FILE, config::LOG_FILE);
    MqttPublisher mqtt(config::MQTT_HOST, config::MQTT_PORT, config::MQTT_TOKEN,
                        config::DEVICE_LABEL);
    RfidReader reader(config::RFID_PORT);
    g_race = &race;
    g_mqtt = &mqtt;
    g_reader = &reader;

    gpioGlitchFilter(config::BEAM_PIN, config::DEBOUNCE_US);
    gpioSetISRFuncEx(config::BEAM_PIN, FALLING_EDGE, 0, beamISR, nullptr);

    gpioGlitchFilter(config::START_BUTTON_PIN, 30000);
    gpioGlitchFilter(config::RESET_BUTTON_PIN, 30000);
    gpioSetISRFuncEx(config::START_BUTTON_PIN, FALLING_EDGE, 0, startButtonISR, nullptr);
    gpioSetISRFuncEx(config::RESET_BUTTON_PIN, FALLING_EDGE, 0, resetButtonISR, nullptr);

    std::printf("Race timer ready. Press START to begin.\n");
    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));

    gpio_io::shutdown();
    return 0;
}
