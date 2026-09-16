#pragma once

#include <string>

#include <mosquitto.h>

// Publishes crossings to Ubidots over MQTT, same auth model as the
// EEN251 slides: API token as username, blank password.
class MqttPublisher {
public:
    MqttPublisher(std::string host, int port, std::string token, std::string device_label);
    ~MqttPublisher();

    MqttPublisher(const MqttPublisher&) = delete;
    MqttPublisher& operator=(const MqttPublisher&) = delete;

    void publishCrossing(int place, const std::string& racer_name, double elapsed_s);

    // Publishes to "race/state": {"state":"running","started_at":<epoch_ms>} or
    // {"state":"idle"}. Consumed directly by the local dashboard (dashboard/server.js).
    void publishState(const std::string& state);

private:
    std::string device_label_;
    mosquitto* mosq_ = nullptr;
};
