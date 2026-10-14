#include <pigpio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <optional>
#include <random>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>

#include "config.hpp"
#include "gpio_io.hpp"
#include "mqtt_publisher.hpp"
#include "race_manager.hpp"
#include "rfid_reader.hpp"

namespace {

// Finished = the leader has taken the chequered flag; the others are still
// being classified as they cross the line, until RESET.
enum class Phase { Idle, Countdown, Running, Finished };

RaceManager* g_race = nullptr;
MqttPublisher* g_mqtt = nullptr;
std::atomic<Phase> g_phase{Phase::Idle};
std::atomic<bool> g_start_requested{false};
std::atomic<bool> g_reset_requested{false};
std::atomic<bool> g_quit{false};
std::atomic<int> g_test_led_requested{-1};
std::atomic<bool> g_republish{false};  // MQTT (re)connected: resend state + config

// Settings set from the dashboard, saved in SETTINGS_FILE. Only touched by
// the main loop; the MQTT thread hands changes over through g_pending_*.
// g_led_order[k] = index (into COUNTDOWN_LED_PINS) of the k-th light to come on.
std::vector<int> g_led_order;
int g_laps = config::DEFAULT_LAPS;
std::mutex g_pending_mutex;
std::optional<std::vector<int>> g_pending_order;
std::optional<int> g_pending_laps;

std::vector<int> defaultLedOrder() {
    std::vector<int> order(config::COUNTDOWN_LED_COUNT);
    for (int i = 0; i < config::COUNTDOWN_LED_COUNT; ++i) order[i] = i;
    return order;
}

// A valid order uses every countdown light exactly once.
bool isValidLedOrder(std::vector<int> order) {
    std::sort(order.begin(), order.end());
    return order == defaultLedOrder();
}

std::vector<int> parseIntList(const std::string& s) {
    std::vector<int> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        try {
            out.push_back(std::stoi(item));
        } catch (...) {
            return {};
        }
    }
    return out;
}

// race_settings.txt:
//   laps=5
//   led_order=2,0,1
void loadSettings() {
    g_led_order = defaultLedOrder();
    g_laps = config::DEFAULT_LAPS;
    std::ifstream in(config::SETTINGS_FILE);
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "led_order") {
            auto order = parseIntList(value);
            if (isValidLedOrder(order)) g_led_order = order;
        } else if (key == "laps") {
            auto laps = parseIntList(value);
            if (laps.size() == 1 && laps[0] >= 1 && laps[0] <= config::MAX_LAPS) g_laps = laps[0];
        }
    }
}

void saveSettings() {
    std::ofstream out(config::SETTINGS_FILE);
    out << "laps=" << g_laps << "\nled_order=";
    for (size_t i = 0; i < g_led_order.size(); ++i) out << (i ? "," : "") << g_led_order[i];
    out << '\n';
}

void publishConfig() {
    std::vector<int> pins(std::begin(config::COUNTDOWN_LED_PINS),
                          std::end(config::COUNTDOWN_LED_PINS));
    g_mqtt->publishConfig(g_led_order, pins, g_laps);
}

// RFID reader thread: each read of a tag at the line is a lap (RaceManager
// drops the repeats while the tag is held there).
void onTag(const std::string& tag) {
    if (g_phase != Phase::Running && g_phase != Phase::Finished) {
        // Outside a race: just show which tag is on the reader, so new tags
        // can be registered in racers.csv. Once per tag presentation.
        static std::string last_tag;
        static auto last_seen = std::chrono::steady_clock::time_point{};
        auto now = std::chrono::steady_clock::now();
        if (tag != last_tag || now - last_seen > std::chrono::seconds(2)) {
            std::printf("Tag on reader: %s  (%s)\n", tag.c_str(), g_race->nameForTag(tag).c_str());
        }
        last_tag = tag;
        last_seen = now;
        return;
    }
    Crossing c = g_race->recordCrossing(tag);
    if (!c.counted) return;

    g_mqtt->publishLap(c.tag_id, c.racer_name, c.lap, c.lap_time_s, c.elapsed_s);
    if (c.place == 0) {
        std::printf("%s  lap %d  %.3fs  (total %.3fs)\n", c.racer_name.c_str(), c.lap,
                    c.lap_time_s, c.elapsed_s);
    } else {
        std::printf("[#%d] %s  finished, %d laps  %.3fs\n", c.place, c.racer_name.c_str(), c.lap,
                    c.elapsed_s);
        g_mqtt->publishFinish(c.place, c.tag_id, c.racer_name, c.lap, c.elapsed_s);
        if (c.place == 1) {
            Phase expected = Phase::Running;
            if (g_phase.compare_exchange_strong(expected, Phase::Finished)) {
                g_mqtt->publishFinished(c.racer_name, c.elapsed_s, c.lap);
                std::printf("Chequered flag - %s wins\n", c.racer_name.c_str());
            }
        }
    }
    gpio_io::blinkRunning();
    if (g_phase == Phase::Idle) gpio_io::setRunningIndicator(false);  // RESET mid-blink
}

// MQTT thread: commands from the dashboard on "race/command", e.g.
//   {"cmd":"start"}  {"cmd":"reset"}
//   {"cmd":"set_led_order","order":[2,0,1]}  {"cmd":"test_led","led":1}
//   {"cmd":"set_laps","laps":5}
// Like the buttons, they only set flags; the main loop does the work.
void onCommand(const std::string& payload) {
    static const std::regex cmd_re(R"re("cmd"\s*:\s*"(\w+)")re");
    static const std::regex order_re(R"re("order"\s*:\s*\[([^\]]*)\])re");
    static const std::regex led_re(R"re("led"\s*:\s*(\d+))re");
    static const std::regex laps_re(R"re("laps"\s*:\s*(\d+))re");

    std::smatch m;
    if (!std::regex_search(payload, m, cmd_re)) return;
    const std::string cmd = m[1];

    if (cmd == "start") {
        if (g_phase == Phase::Idle) g_start_requested = true;
    } else if (cmd == "reset") {
        g_reset_requested = true;
    } else if (cmd == "set_led_order" && std::regex_search(payload, m, order_re)) {
        auto order = parseIntList(m[1]);
        if (!isValidLedOrder(order)) return;
        std::lock_guard<std::mutex> lock(g_pending_mutex);
        g_pending_order = order;
    } else if (cmd == "set_laps" && std::regex_search(payload, m, laps_re)) {
        int laps = std::stoi(m[1].str().substr(0, 3));
        if (laps < 1 || laps > config::MAX_LAPS) return;
        std::lock_guard<std::mutex> lock(g_pending_mutex);
        g_pending_laps = laps;
    } else if (cmd == "test_led" && std::regex_search(payload, m, led_re)) {
        int led = std::stoi(m[1].str().substr(0, 3));
        if (led >= 0 && led < config::COUNTDOWN_LED_COUNT) g_test_led_requested = led;
    }
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

// Sleeps in small steps; returns false early if RESET (or quit) comes in.
bool waitMs(int ms) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (g_reset_requested || g_quit) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return !(g_reset_requested || g_quit);
}

void doReset() {
    g_reset_requested = false;
    g_start_requested = false;
    g_race->saveLog();
    g_race->reset();
    g_phase = Phase::Idle;
    gpio_io::allCountdownOff();
    gpio_io::setRunningIndicator(false);
    g_mqtt->publishIdle();
    std::printf("Race reset - ready\n");
}

// F1 start: lights come on one by one in g_led_order, stay lit for a random
// hold, then all go out at once = GO. Returns false if RESET was pressed.
bool runCountdown() {
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> hold_ms(config::COUNTDOWN_HOLD_MIN_MS,
                                               config::COUNTDOWN_HOLD_MAX_MS);

    g_phase = Phase::Countdown;
    gpio_io::setReadyIndicator(false);
    std::printf("Lights...\n");

    for (size_t k = 0; k < g_led_order.size(); ++k) {
        if (!waitMs(k == 0 ? 0 : config::COUNTDOWN_STEP_MS)) return false;
        gpio_io::setCountdownLed(g_led_order[k], true);
        g_mqtt->publishCountdown(static_cast<int>(k) + 1);
    }
    if (!waitMs(hold_ms(rng))) return false;

    gpio_io::allCountdownOff();
    return true;
}

void startRace() {
    g_race->startRace(g_laps);
    g_phase = Phase::Running;
    gpio_io::setRunningIndicator(true);
    g_mqtt->publishRunning(g_laps);
    std::printf("Lights out - GO! %d laps\n", g_laps);
}

// Settings only change between races, so a race keeps the laps it started with.
void applyPendingSettings() {
    std::optional<std::vector<int>> order;
    std::optional<int> laps;
    {
        std::lock_guard<std::mutex> lock(g_pending_mutex);
        if (g_phase != Phase::Idle) return;
        order.swap(g_pending_order);
        laps.swap(g_pending_laps);
    }
    if (!order && !laps) return;
    if (order) g_led_order = *order;
    if (laps) g_laps = *laps;
    saveSettings();
    publishConfig();
    std::printf("Settings: %d laps, light order changed=%s\n", g_laps, order ? "yes" : "no");
}

void testLed(int led) {
    g_test_led_requested = -1;
    if (g_phase != Phase::Idle) return;
    gpio_io::setCountdownLed(led, true);
    waitMs(1000);
    gpio_io::setCountdownLed(led, false);
}

}  // namespace

int main() {
    if (!gpio_io::init()) {
        std::fprintf(stderr, "pigpio init failed (run as root / check daemon)\n");
        return 1;
    }
    // Ctrl+C, kill/systemctl stop, closed terminal: exit through the cleanup
    // that turns the LEDs off.
    gpioSetSignalFunc(SIGINT, onSignal);
    gpioSetSignalFunc(SIGTERM, onSignal);
    gpioSetSignalFunc(SIGHUP, onSignal);
    gpioSetSignalFunc(SIGQUIT, onSignal);

    loadSettings();

    RaceManager race(config::RACERS_FILE, config::LOG_FILE, config::MIN_LAP_S);
    MqttPublisher mqtt(config::MQTT_HOST, config::MQTT_PORT, config::MQTT_TOKEN,
                        config::DEVICE_LABEL, onCommand, [] { g_republish = true; });
    g_race = &race;
    g_mqtt = &mqtt;
    RfidReader reader(config::RFID_SPI_DEVICE, onTag);
    if (!reader.isOpen()) {
        std::fprintf(stderr, "warning: no RC522 reader on %s - finishes can't be recorded\n",
                     config::RFID_SPI_DEVICE.c_str());
    }

    // Alert callbacks, not gpioSetISRFunc: ISRs go through sysfs GPIO, which
    // newer kernels number from 512, so they silently fail to register.
    gpioGlitchFilter(config::START_BUTTON_PIN, 30000);
    gpioGlitchFilter(config::RESET_BUTTON_PIN, 30000);
    gpioSetAlertFuncEx(config::START_BUTTON_PIN, startButtonISR, nullptr);
    gpioSetAlertFuncEx(config::RESET_BUTTON_PIN, resetButtonISR, nullptr);

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
        } else if (g_test_led_requested >= 0) {
            testLed(g_test_led_requested);
        }
        applyPendingSettings();
        if (g_republish.exchange(false)) {
            publishConfig();
            if (g_phase == Phase::Idle) mqtt.publishIdle();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    std::printf("\nShutting down\n");
    race.saveLog();
    gpio_io::shutdown();
    // The RFID thread may be blocked mid-poll; don't wait to join it. The
    // broker publishes our "offline" last will when the connection drops.
    std::_Exit(0);
}
