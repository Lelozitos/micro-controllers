#pragma once

#include <string>

// Pin numbers use BCM numbering (pigpio convention).
// Single track: one break-beam sensor and one RFID reader at the finish line.
// Racers cross one at a time; the reader identifies whoever's tag was seen
// last, right as the beam breaks.

namespace config {

constexpr int BEAM_PIN = 17;                    // physical pin 11
const std::string RFID_PORT = "/dev/ttyUSB0";    // RDM6300 reader at the finish line

constexpr int START_BUTTON_PIN = 22;
constexpr int RESET_BUTTON_PIN = 23;
constexpr int STATUS_LED_PIN = 24;  // green = idle/ready
constexpr int ALERT_LED_PIN = 25;   // red = race running
constexpr int BUZZER_PIN = 18;

constexpr int DEBOUNCE_US = 50000;            // 50 ms, beam flicker rejection
constexpr double MIN_CROSSING_GAP_S = 0.3;    // ignore re-trigger sooner than this (noise, not pacing racers apart)
constexpr double TAG_MATCH_WINDOW_S = 3.0;    // last RFID read within this window counts as the crosser

const std::string RACERS_FILE = "racers.csv";  // "tag_id,name" per line
const std::string LOG_FILE = "race_log.csv";

// MQTT / Ubidots - same auth model as the EEN251 course slides:
// token as username, blank password.
const std::string MQTT_HOST = "industrial.api.ubidots.com";
constexpr int MQTT_PORT = 1883;
const std::string MQTT_TOKEN = "YOUR_UBIDOTS_TOKEN";
const std::string DEVICE_LABEL = "race_timer";

}  // namespace config
