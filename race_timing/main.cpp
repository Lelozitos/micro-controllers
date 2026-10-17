#include <pigpio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
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
std::atomic<bool> g_reload_racers{false};

// Settings set from the dashboard, saved in SETTINGS_FILE. Only touched by
// the main loop; the MQTT thread hands changes over through g_pending_*.
// g_led_order[k] = index (into COUNTDOWN_LED_PINS) of the k-th light to come on.
// g_checkpoints[k] = SPI device of timing line k (0 = finish line).
std::vector<int> g_led_order;
int g_laps = config::DEFAULT_LAPS;
std::vector<std::string> g_checkpoints;
std::mutex g_pending_mutex;
std::optional<std::vector<int>> g_pending_order;
std::optional<int> g_pending_laps;
std::optional<std::vector<std::string>> g_pending_checkpoints;
std::optional<std::vector<std::pair<std::string, std::string>>> g_pending_racers;

// One reader per timing line, same index as g_checkpoints. Only (re)built by
// the main loop while idle.
std::vector<std::unique_ptr<RfidReader>> g_readers;

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

// Every line needs its own reader: known spidev names, no duplicates.
bool isValidCheckpoints(const std::vector<std::string>& devices) {
    static const std::regex dev_re(R"(/dev/spidev\d+\.\d+)");
    if (devices.empty() || devices.size() > static_cast<size_t>(config::MAX_CHECKPOINTS)) {
        return false;
    }
    for (size_t i = 0; i < devices.size(); ++i) {
        if (!std::regex_match(devices[i], dev_re)) return false;
        for (size_t j = 0; j < i; ++j) {
            if (devices[i] == devices[j]) return false;
        }
    }
    return true;
}

std::vector<std::string> parseStringList(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) out.push_back(item);
    return out;
}

// SPI devices present on this Pi, for the dashboard's reader picker.
std::vector<std::string> listSpiDevices() {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/dev", ec)) {
        std::string name = entry.path().filename().string();
        if (name.rfind("spidev", 0) == 0) out.push_back(entry.path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
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
//   checkpoints=/dev/spidev0.0,/dev/spidev0.1
void loadSettings() {
    g_led_order = defaultLedOrder();
    g_laps = config::DEFAULT_LAPS;
    g_checkpoints = {config::DEFAULT_FINISH_DEVICE};
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
        } else if (key == "checkpoints") {
            auto devices = parseStringList(value);
            if (isValidCheckpoints(devices)) g_checkpoints = devices;
        }
    }
}

void saveSettings() {
    std::ofstream out(config::SETTINGS_FILE);
    out << "laps=" << g_laps << "\nled_order=";
    for (size_t i = 0; i < g_led_order.size(); ++i) out << (i ? "," : "") << g_led_order[i];
    out << "\ncheckpoints=";
    for (size_t i = 0; i < g_checkpoints.size(); ++i) out << (i ? "," : "") << g_checkpoints[i];
    out << '\n';
}

void publishConfig() {
    std::vector<int> pins(std::begin(config::COUNTDOWN_LED_PINS),
                          std::end(config::COUNTDOWN_LED_PINS));
    std::vector<MqttPublisher::Checkpoint> checkpoints;
    for (size_t i = 0; i < g_checkpoints.size(); ++i) {
        bool ok = i < g_readers.size() && g_readers[i] && g_readers[i]->isOpen();
        checkpoints.push_back({g_checkpoints[i], ok});
    }
    g_mqtt->publishConfig(g_led_order, pins, g_laps, checkpoints, listSpiDevices(),
                          g_race->racers());
}

std::string lineName(int checkpoint) {
    return checkpoint == 0 ? "finish" : "sector line " + std::to_string(checkpoint);
}

// Reports each tag presented to a reader once (the reader repeats every
// RFID_POLL_MS while it stays there), so new tags can be registered in
// racers.csv and the wiring of every line can be checked.
void announceTag(int checkpoint, const std::string& tag) {
    static std::mutex m;
    static std::map<int, std::pair<std::string, std::chrono::steady_clock::time_point>> last;
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(m);
        auto& [last_tag, last_seen] = last[checkpoint];
        bool fresh = tag != last_tag || now - last_seen > std::chrono::seconds(2);
        last_tag = tag;
        last_seen = now;
        if (!fresh) return;
    }
    std::string name = g_race->nameForTag(tag);
    g_mqtt->publishTag(checkpoint, tag, name);
    if (g_phase != Phase::Running && g_phase != Phase::Finished) {
        std::printf("Tag on %s reader: %s  (%s)\n", lineName(checkpoint).c_str(), tag.c_str(),
                    name.c_str());
    }
}

// RFID reader threads (and simulated reads from the dashboard): a read at
// the finish line is a lap, at a sector line a split. RaceManager drops the
// repeats while the tag is held there.
void onTag(int checkpoint, const std::string& tag) {
    announceTag(checkpoint, tag);
    if (g_phase != Phase::Running && g_phase != Phase::Finished) return;

    Crossing c = g_race->recordCrossing(checkpoint, tag);
    if (!c.counted) return;

    if (c.start) {
        g_mqtt->publishStart(c.tag_id, c.racer_name, c.elapsed_s);
        std::printf("%s  crossed the line - lap 1 started (%.3fs)\n", c.racer_name.c_str(),
                    c.elapsed_s);
        gpio_io::blinkRunning();
        if (g_phase == Phase::Idle) gpio_io::setRunningIndicator(false);
        return;
    }

    if (c.checkpoint > 0) {
        g_mqtt->publishSector(c.tag_id, c.racer_name, c.lap, c.sector, c.sector_time_s,
                              c.elapsed_s);
        if (c.sector_time_s >= 0) {
            std::printf("%s  lap %d  S%d %.3fs\n", c.racer_name.c_str(), c.lap, c.sector,
                        c.sector_time_s);
        } else {
            std::printf("%s  lap %d  S%d (previous line missed)\n", c.racer_name.c_str(), c.lap,
                        c.sector);
        }
        return;
    }

    g_mqtt->publishLap(c.tag_id, c.racer_name, c.lap, c.lap_time_s, c.elapsed_s, c.sectors);
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
        // Everyone who started is classified: the race is over. Ends it the
        // same way RESET does (log saved, green off, red "ready" on).
        if (g_race->allClassified()) {
            std::printf("All racers classified - race over\n");
            g_reset_requested = true;
        }
    }
    gpio_io::blinkRunning();
    if (g_phase == Phase::Idle) gpio_io::setRunningIndicator(false);  // RESET mid-blink
}

// MQTT thread: commands from the dashboard on "race/command", e.g.
//   {"cmd":"start"}  {"cmd":"reset"}
//   {"cmd":"set_led_order","order":[2,0,1]}  {"cmd":"test_led","led":1}
//   {"cmd":"set_laps","laps":5}
//   {"cmd":"set_checkpoints","devices":["/dev/spidev0.0","/dev/spidev0.1"]}
//   {"cmd":"sim_tag","checkpoint":1,"tag":"A1B2C3D4"}  {"cmd":"reload_racers"}
//   {"cmd":"set_racers","racers":[{"tag":"A1B2C3D4","name":"Alice"},...]}
// Like the buttons, they only set flags; the main loop does the work.
// sim_tag is the exception: it goes through onTag like a real read.
void onCommand(const std::string& payload) {
    static const std::regex cmd_re(R"re("cmd"\s*:\s*"(\w+)")re");
    static const std::regex order_re(R"re("order"\s*:\s*\[([^\]]*)\])re");
    static const std::regex led_re(R"re("led"\s*:\s*(\d+))re");
    static const std::regex laps_re(R"re("laps"\s*:\s*(\d+))re");
    static const std::regex devices_re(R"re("devices"\s*:\s*\[([^\]]*)\])re");
    static const std::regex quoted_re(R"re("([^"]*)")re");
    static const std::regex checkpoint_re(R"re("checkpoint"\s*:\s*(\d+))re");
    static const std::regex tag_re(R"re("tag"\s*:\s*"([0-9A-Fa-f]{1,20})")re");
    static const std::regex racers_re(R"re("racers"\s*:\s*\[)re");
    static const std::regex racer_re(
        R"re(\{\s*"tag"\s*:\s*"([0-9A-Fa-f]{1,20})"\s*,\s*"name"\s*:\s*"((?:[^"\\]|\\.)*)"\s*\})re");

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
    } else if (cmd == "set_checkpoints" && std::regex_search(payload, m, devices_re)) {
        std::vector<std::string> devices;
        const std::string list = m[1];
        for (auto it = std::sregex_iterator(list.begin(), list.end(), quoted_re);
             it != std::sregex_iterator(); ++it) {
            devices.push_back((*it)[1]);
        }
        if (!isValidCheckpoints(devices)) return;
        std::lock_guard<std::mutex> lock(g_pending_mutex);
        g_pending_checkpoints = devices;
    } else if (cmd == "sim_tag" && std::regex_search(payload, m, checkpoint_re)) {
        int checkpoint = std::stoi(m[1].str().substr(0, 3));
        if (!std::regex_search(payload, m, tag_re)) return;
        std::string tag = m[1];
        std::transform(tag.begin(), tag.end(), tag.begin(), ::toupper);
        // Main-thread-owned g_checkpoints isn't read here; RaceManager
        // ignores lines beyond the race's own.
        if (checkpoint < config::MAX_CHECKPOINTS) onTag(checkpoint, tag);
    } else if (cmd == "reload_racers") {
        g_reload_racers = true;
    } else if (cmd == "set_racers" && std::regex_search(payload, racers_re)) {
        std::vector<std::pair<std::string, std::string>> racers;
        for (auto it = std::sregex_iterator(payload.begin(), payload.end(), racer_re);
             it != std::sregex_iterator(); ++it) {
            std::string tag = (*it)[1];
            std::transform(tag.begin(), tag.end(), tag.begin(), ::toupper);
            // JSON string -> plain text; the dashboard only sends \" and \\ escapes.
            std::string raw = (*it)[2], name;
            for (size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '\\' && i + 1 < raw.size()) ++i;
                name += raw[i];
            }
            if (name.empty() || name.find_first_of("\r\n") != std::string::npos) return;
            for (const auto& r : racers) {
                if (r.first == tag) return;  // duplicate tag
            }
            racers.emplace_back(tag, name);
        }
        std::lock_guard<std::mutex> lock(g_pending_mutex);
        g_pending_racers = racers;
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
    int lines = static_cast<int>(g_checkpoints.size());
    g_race->startRace(g_laps, lines);
    g_phase = Phase::Running;
    gpio_io::setRunningIndicator(true);
    g_mqtt->publishRunning(g_laps, lines > 1 ? lines : 0);
    std::printf("Lights out - GO! %d laps, %d sectors\n", g_laps, lines > 1 ? lines : 0);
}

// (Re)opens one RC522 per timing line. Old readers are stopped first, since
// a device can only be polled by one of them.
void openReaders() {
    g_readers.clear();
    for (size_t i = 0; i < g_checkpoints.size(); ++i) {
        int checkpoint = static_cast<int>(i);
        auto reader = std::make_unique<RfidReader>(
            g_checkpoints[i], [checkpoint](const std::string& tag) { onTag(checkpoint, tag); });
        if (!reader->isOpen()) {
            std::fprintf(stderr, "warning: no RC522 on %s (%s) - it can't record crossings\n",
                         g_checkpoints[i].c_str(), lineName(checkpoint).c_str());
        }
        g_readers.push_back(std::move(reader));
    }
}

// The roster can change at any time, e.g. to name an unknown tag mid-race.
void applyPendingRacers() {
    std::optional<std::vector<std::pair<std::string, std::string>>> racers;
    {
        std::lock_guard<std::mutex> lock(g_pending_mutex);
        racers.swap(g_pending_racers);
    }
    if (!racers) return;
    if (g_race->setRacers(*racers)) {
        std::printf("Saved %zu racer(s) to %s\n", racers->size(), config::RACERS_FILE.c_str());
    } else {
        std::fprintf(stderr, "could not write %s\n", config::RACERS_FILE.c_str());
    }
    publishConfig();
}

// Settings only change between races, so a race keeps the laps it started with.
void applyPendingSettings() {
    std::optional<std::vector<int>> order;
    std::optional<int> laps;
    std::optional<std::vector<std::string>> checkpoints;
    {
        std::lock_guard<std::mutex> lock(g_pending_mutex);
        if (g_phase != Phase::Idle) return;
        order.swap(g_pending_order);
        laps.swap(g_pending_laps);
        checkpoints.swap(g_pending_checkpoints);
    }
    if (!order && !laps && !checkpoints) return;
    if (order) g_led_order = *order;
    if (laps) g_laps = *laps;
    if (checkpoints && *checkpoints != g_checkpoints) {
        g_checkpoints = *checkpoints;
        openReaders();
    }
    saveSettings();
    publishConfig();
    std::printf("Settings: %d laps, %zu timing line(s), light order changed=%s\n", g_laps,
                g_checkpoints.size(), order ? "yes" : "no");
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
    openReaders();

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
        applyPendingRacers();
        if (g_reload_racers.exchange(false)) {
            race.loadRacers();
            publishConfig();
            std::printf("Reloaded %s\n", config::RACERS_FILE.c_str());
        }
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
