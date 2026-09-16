#include "mqtt_publisher.hpp"

#include <chrono>
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

}  // namespace

MqttPublisher::MqttPublisher(std::string host, int port, std::string token,
                              std::string device_label)
    : device_label_(std::move(device_label)) {
    mosquitto_lib_init();
    mosq_ = mosquitto_new(device_label_.c_str(), true, nullptr);
    mosquitto_username_pw_set(mosq_, token.c_str(), "");
    if (mosquitto_connect(mosq_, host.c_str(), port, 60) != MOSQ_ERR_SUCCESS) {
        std::cerr << "MQTT connect failed to " << host << ':' << port << '\n';
        return;
    }
    mosquitto_loop_start(mosq_);
}

MqttPublisher::~MqttPublisher() {
    if (mosq_) {
        mosquitto_loop_stop(mosq_, true);
        mosquitto_disconnect(mosq_);
        mosquitto_destroy(mosq_);
    }
    mosquitto_lib_cleanup();
}

void MqttPublisher::publishCrossing(int place, const std::string& racer_name,
                                     double elapsed_s) {
    if (!mosq_) return;

    // Ubidots accepts a "context" object alongside the numeric value,
    // which is how the racer's name/finish order rides along with the time.
    std::ostringstream ubidots_topic;
    ubidots_topic << "/v1.6/devices/" << device_label_;
    std::ostringstream ubidots_payload;
    ubidots_payload << "{\"finish_time\":{\"value\":" << elapsed_s
                     << ",\"context\":{\"racer\":\"" << jsonEscape(racer_name)
                     << "\",\"place\":" << place << "}}}";
    {
        std::string t = ubidots_topic.str();
        std::string p = ubidots_payload.str();
        mosquitto_publish(mosq_, nullptr, t.c_str(), static_cast<int>(p.size()),
                           p.c_str(), 0, false);
    }

    // Plain topic for the local dashboard (dashboard/server.js) - no Ubidots
    // wrapping, just the fields it needs.
    std::ostringstream dash_payload;
    dash_payload << "{\"place\":" << place << ",\"racer\":\"" << jsonEscape(racer_name)
                 << "\",\"elapsed_s\":" << elapsed_s << "}";
    {
        std::string p = dash_payload.str();
        mosquitto_publish(mosq_, nullptr, "race/finish", static_cast<int>(p.size()),
                           p.c_str(), 0, false);
    }
}

void MqttPublisher::publishState(const std::string& state) {
    if (!mosq_) return;

    std::ostringstream payload;
    if (state == "running") {
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
        payload << "{\"state\":\"running\",\"started_at\":" << now_ms << "}";
    } else {
        payload << "{\"state\":\"" << jsonEscape(state) << "\"}";
    }

    std::string p = payload.str();
    mosquitto_publish(mosq_, nullptr, "race/state", static_cast<int>(p.size()),
                       p.c_str(), 0, false);
}
