#pragma once

#include <string>

// Pin numbers use BCM numbering (pigpio convention).
// Single track, no break-beam: RFID readers are the timing lines. The first
// one is the start/finish line (a read there = a lap); any others are sector
// lines around the track, in track order.

namespace config {

// RC522 (MFRC522, 13.56 MHz) readers, one per timing line. Which SPI device
// each line uses is set from the dashboard (Setup tab) and saved in
// SETTINGS_FILE; this is the default: just a finish line on SPI0 CE0.
// SPI0 gives /dev/spidev0.0 (CE0) and /dev/spidev0.1 (CE1); enable SPI1
// (dtoverlay=spi1-3cs) for /dev/spidev1.0-1.2.
const std::string DEFAULT_FINISH_DEVICE = "/dev/spidev0.0";
constexpr int MAX_CHECKPOINTS = 6;             // finish line + up to 5 sector lines
constexpr int RFID_RST_PIN = -1;               // RC522 RST wired to 3.3V (or a GPIO number to drive it)
constexpr int RFID_POLL_MS = 50;               // how often to look for a card

constexpr int START_BUTTON_PIN = 22;
constexpr int RESET_BUTTON_PIN = 23;
constexpr int READY_LED_PIN = 24;    // red   = idle, race can start (physical pin 18)
constexpr int RUNNING_LED_PIN = 25;  // green = race in progress     (physical pin 22)

// F1-style start lights: they come on one by one, stay lit for a random
// hold, then all go out at once = GO. The order they light in can be changed
// from the dashboard and is saved in SETTINGS_FILE.
constexpr int COUNTDOWN_LED_PINS[] = {5, 6, 13};  // physical pins 29, 31, 33
constexpr int COUNTDOWN_LED_COUNT = sizeof(COUNTDOWN_LED_PINS) / sizeof(COUNTDOWN_LED_PINS[0]);
constexpr int COUNTDOWN_STEP_MS = 1000;      // gap between lights coming on
constexpr int COUNTDOWN_HOLD_MIN_MS = 1000;  // after the last light comes on, all stay lit
constexpr int COUNTDOWN_HOLD_MAX_MS = 3000;  // for a random 1-3 s, then lights out

// Lap race: every read of a racer's tag at the line is a lap. The first to
// complete the set number of laps (chosen on the dashboard) wins.
constexpr int DEFAULT_LAPS = 3;
constexpr int MAX_LAPS = 99;
constexpr double MIN_LAP_S = 3.0;  // same tag at the same line sooner than this = still the same pass

const std::string RACERS_FILE = "racers.csv";  // "tag_id,name" per line
const std::string LOG_FILE = "race_log.csv";
const std::string SETTINGS_FILE = "race_settings.txt";  // laps, light order, timing lines set from the dashboard

// MQTT broker shared with the dashboard (it also sends START/RESET/LED
// commands on race/command). For Ubidots: token as username, blank password.
const std::string MQTT_HOST = "localhost";
constexpr int MQTT_PORT = 1883;
const std::string MQTT_TOKEN = "";
const std::string DEVICE_LABEL = "race_timer";

}  // namespace config
