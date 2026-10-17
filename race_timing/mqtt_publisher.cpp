#include "mqtt_publisher.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>

namespace {

// Minimal JSON string escaping - good enough for racer names typed by hand.
std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

std::string jsonIntArray(const std::vector<int>& v) {
    std::ostringstream out;
    out << '[';
    for (size_t i = 0; i < v.size(); ++i) out << (i ? "," : "") << v[i];
    out << ']';
    return out.str();
}

// Seconds as a JSON number; negative (= not timed) becomes null.
std::string jsonTime(double s) {
    if (s < 0) return "null";
    std::ostringstream out;
    out << s;
    return out.str();
}

std::string jsonString(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }

// "race/timer" is "online" while the timer runs; the broker publishes
// "offline" for us (last will) if it dies, so the dashboard can grey out
// its controls.
constexpr const char* TIMER_TOPIC = "race/timer";
constexpr const char* COMMAND_TOPIC = "race/command";

}  // namespace

MqttPublisher::MqttPublisher(std::string host, int port, std::string token,
                              std::string device_label, CommandHandler on_command,
                              ConnectHandler on_connected)
    : device_label_(std::move(device_label)),
      on_command_(std::move(on_command)),
      on_connected_(std::move(on_connected)) {
    mosquitto_lib_init();
    mosq_ = mosquitto_new(device_label_.c_str(), true, this);
    mosquitto_username_pw_set(mosq_, token.c_str(), "");
    mosquitto_will_set(mosq_, TIMER_TOPIC, 7, "offline", 1, true);
    mosquitto_connect_callback_set(mosq_, &MqttPublisher::onConnect);
    mosquitto_message_callback_set(mosq_, &MqttPublisher::onMessage);
    mosquitto_reconnect_delay_set(mosq_, 2, 10, false);

    // Async connect + loop thread: keeps retrying if the broker isn't up yet
    // and reconnects (re-subscribing in onConnect) if it drops.
    if (mosquitto_connect_async(mosq_, host.c_str(), port, 60) != MOSQ_ERR_SUCCESS) {
        std::cerr << "MQTT connect to " << host << ':' << port << " failed, retrying\n";
    }
    mosquitto_loop_start(mosq_);
}

MqttPublisher::~MqttPublisher() {
    if (mosq_) {
        publish(TIMER_TOPIC, "offline", true);
        mosquitto_disconnect(mosq_);
        mosquitto_loop_stop(mosq_, false);
        mosquitto_destroy(mosq_);
    }
    mosquitto_lib_cleanup();
}

void MqttPublisher::onConnect(mosquitto* mosq, void* self, int rc) {
    if (rc != 0) {
        std::cerr << "MQTT connection refused: " << mosquitto_connack_string(rc) << '\n';
        return;
    }
    std::cout << "MQTT connected" << std::endl;
    mosquitto_subscribe(mosq, nullptr, COMMAND_TOPIC, 1);
    auto* me = static_cast<MqttPublisher*>(self);
    me->publish(TIMER_TOPIC, "online", true);
    if (me->on_connected_) me->on_connected_();
}

void MqttPublisher::onMessage(mosquitto* /*mosq*/, void* self, const mosquitto_message* msg) {
    auto* me = static_cast<MqttPublisher*>(self);
    if (!me->on_command_ || std::strcmp(msg->topic, COMMAND_TOPIC) != 0) return;
    me->on_command_(std::string(static_cast<const char*>(msg->payload), msg->payloadlen));
}

void MqttPublisher::publish(const std::string& topic, const std::string& payload, bool retain) {
    if (!mosq_) return;
    mosquitto_publish(mosq_, nullptr, topic.c_str(), static_cast<int>(payload.size()),
                       payload.c_str(), 0, retain);
}

void MqttPublisher::publishLap(const std::string& tag, const std::string& racer_name, int lap,
                                double lap_time_s, double elapsed_s,
                                const std::vector<double>& sectors) {
    std::ostringstream payload;
    payload << "{\"tag\":" << jsonString(tag) << ",\"racer\":" << jsonString(racer_name)
            << ",\"lap\":" << lap << ",\"lap_time_s\":" << lap_time_s
            << ",\"elapsed_s\":" << elapsed_s << ",\"sectors\":[";
    for (size_t i = 0; i < sectors.size(); ++i) payload << (i ? "," : "") << jsonTime(sectors[i]);
    payload << "]}";
    publish("race/lap", payload.str());
}

void MqttPublisher::publishStart(const std::string& tag, const std::string& racer_name,
                                  double elapsed_s) {
    std::ostringstream payload;
    payload << "{\"tag\":" << jsonString(tag) << ",\"racer\":" << jsonString(racer_name)
            << ",\"elapsed_s\":" << elapsed_s << "}";
    publish("race/start", payload.str());
}

void MqttPublisher::publishSector(const std::string& tag, const std::string& racer_name, int lap,
                                   int sector, double sector_time_s, double elapsed_s) {
    std::ostringstream payload;
    payload << "{\"tag\":" << jsonString(tag) << ",\"racer\":" << jsonString(racer_name)
            << ",\"lap\":" << lap << ",\"sector\":" << sector
            << ",\"sector_time_s\":" << jsonTime(sector_time_s) << ",\"elapsed_s\":" << elapsed_s
            << "}";
    publish("race/sector", payload.str());
}

void MqttPublisher::publishTag(int checkpoint, const std::string& tag,
                                const std::string& racer_name) {
    publish("race/tag", "{\"checkpoint\":" + std::to_string(checkpoint) +
                            ",\"tag\":" + jsonString(tag) + ",\"racer\":" +
                            jsonString(racer_name) + "}");
}

void MqttPublisher::publishFinish(int place, const std::string& tag,
                                  const std::string& racer_name, int laps, double elapsed_s) {
    // Ubidots accepts a "context" object alongside the numeric value,
    // which is how the racer's name/finish order rides along with the time.
    std::ostringstream ubidots_payload;
    ubidots_payload << "{\"finish_time\":{\"value\":" << elapsed_s
                     << ",\"context\":{\"racer\":\"" << jsonEscape(racer_name)
                     << "\",\"place\":" << place << ",\"laps\":" << laps << "}}}";
    publish("/v1.6/devices/" + device_label_, ubidots_payload.str());

    // Plain topic for the local dashboard (dashboard/server.js).
    std::ostringstream dash_payload;
    dash_payload << "{\"place\":" << place << ",\"tag\":\"" << jsonEscape(tag)
                 << "\",\"racer\":\"" << jsonEscape(racer_name) << "\",\"laps\":" << laps
                 << ",\"elapsed_s\":" << elapsed_s << "}";
    publish("race/finish", dash_payload.str());
}

void MqttPublisher::publishIdle() { publish("race/state", "{\"state\":\"idle\"}"); }

void MqttPublisher::publishRunning(int laps, int sectors) {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    publish("race/state", "{\"state\":\"running\",\"started_at\":" + std::to_string(now_ms) +
                              ",\"laps\":" + std::to_string(laps) +
                              ",\"sectors\":" + std::to_string(sectors) + "}");
}

void MqttPublisher::publishFinished(const std::string& winner, double elapsed_s, int laps) {
    std::ostringstream payload;
    payload << "{\"state\":\"finished\",\"winner\":\"" << jsonEscape(winner)
            << "\",\"elapsed_s\":" << elapsed_s << ",\"laps\":" << laps << "}";
    publish("race/state", payload.str());
}

void MqttPublisher::publishCountdown(int lit) {
    publish("race/state", "{\"state\":\"countdown\",\"lit\":" + std::to_string(lit) + "}");
}

void MqttPublisher::publishConfig(const std::vector<int>& led_order,
                                   const std::vector<int>& led_pins, int laps,
                                   const std::vector<Checkpoint>& checkpoints,
                                   const std::vector<std::string>& spi_devices,
                                   const std::map<std::string, std::string>& racers) {
    std::ostringstream payload;
    payload << "{\"led_order\":" << jsonIntArray(led_order)
            << ",\"led_pins\":" << jsonIntArray(led_pins) << ",\"laps\":" << laps
            << ",\"checkpoints\":[";
    for (size_t i = 0; i < checkpoints.size(); ++i) {
        payload << (i ? "," : "") << "{\"device\":" << jsonString(checkpoints[i].device)
                << ",\"ok\":" << (checkpoints[i].ok ? "true" : "false") << "}";
    }
    payload << "],\"spi_devices\":[";
    for (size_t i = 0; i < spi_devices.size(); ++i) {
        payload << (i ? "," : "") << jsonString(spi_devices[i]);
    }
    payload << "],\"racers\":[";
    bool first = true;
    for (const auto& [tag, name] : racers) {
        payload << (first ? "" : ",") << "{\"tag\":" << jsonString(tag)
                << ",\"name\":" << jsonString(name) << "}";
        first = false;
    }
    payload << "]}";
    publish("race/config", payload.str(), true);
}
