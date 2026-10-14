#pragma once

#include <string>

// Pin numbers use BCM numbering (pigpio convention).
// Single track, no break-beam: the RFID reader at the finish line is the
// finish line. A racer finishes the first time their tag is read during a race.

namespace config {

const std::string RFID_PORT = "/dev/ttyUSB0";    // RDM6300 reader at the finish line

constexpr int START_BUTTON_PIN = 22;
constexpr int RESET_BUTTON_PIN = 23;
constexpr int READY_LED_PIN = 24;    // red   = idle, race can start (physical pin 18)
constexpr int RUNNING_LED_PIN = 25;  // green = race in progress     (physical pin 22)
constexpr int BUZZER_PIN = 18;

// Start countdown lights: lit one by one, then all go out = GO.
constexpr int COUNTDOWN_LED_PINS[] = {5, 6, 13};  // physical pins 29, 31, 33
constexpr int COUNTDOWN_STEP_MS = 1000;

const std::string RACERS_FILE = "racers.csv";  // "tag_id,name" per line
const std::string LOG_FILE = "race_log.csv";

// MQTT / Ubidots - same auth model as the EEN251 course slides:
// token as username, blank password.
const std::string MQTT_HOST = "localhost";
constexpr int MQTT_PORT = 1883;
const std::string MQTT_TOKEN = "";
const std::string DEVICE_LABEL = "race_timer";

}  // namespace config
