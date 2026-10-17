#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <mosquitto.h>

// MQTT link for the race timer: publishes race events (local dashboard topics
// plus the Ubidots-shaped topic) and receives control commands from the
// dashboard on "race/command". Ubidots auth model as in the EEN251 slides:
// API token as username, blank password.
class MqttPublisher {
public:
    // Called on the MQTT network thread with the raw payload of each
    // "race/command" message.
    using CommandHandler = std::function<void(const std::string& payload)>;
    // Called on the MQTT network thread after every (re)connect.
    using ConnectHandler = std::function<void()>;

    MqttPublisher(std::string host, int port, std::string token, std::string device_label,
                  CommandHandler on_command = {}, ConnectHandler on_connected = {});
    ~MqttPublisher();

    MqttPublisher(const MqttPublisher&) = delete;
    MqttPublisher& operator=(const MqttPublisher&) = delete;

    // "race/lap": {"tag":"A1B2C3D4","racer":"Alice","lap":2,"lap_time_s":31.2,
    //              "elapsed_s":63.0,"sectors":[10.1,null,11.0]}
    // sectors is empty without sector lines; null = that sector wasn't timed.
    void publishLap(const std::string& tag, const std::string& racer_name, int lap,
                    double lap_time_s, double elapsed_s, const std::vector<double>& sectors);

    // "race/start" on a racer's first finish-line crossing, which starts their lap 1:
    // {"tag":"A1B2C3D4","racer":"Alice","elapsed_s":4.2}
    void publishStart(const std::string& tag, const std::string& racer_name, double elapsed_s);

    // "race/sector" when a racer crosses a sector line:
    // {"tag":"A1B2C3D4","racer":"Alice","lap":3,"sector":1,"sector_time_s":10.1,"elapsed_s":73.1}
    void publishSector(const std::string& tag, const std::string& racer_name, int lap, int sector,
                       double sector_time_s, double elapsed_s);

    // "race/tag" for each tag presented to a reader (once per presentation),
    // race or not: {"checkpoint":0,"tag":"A1B2C3D4","racer":"Alice"}
    void publishTag(int checkpoint, const std::string& tag, const std::string& racer_name);

    // "race/finish" (plus the Ubidots topic) when a racer is classified:
    // {"place":1,"tag":"A1B2C3D4","racer":"Alice","laps":5,"elapsed_s":160.4}
    void publishFinish(int place, const std::string& tag, const std::string& racer_name,
                       int laps, double elapsed_s);

    // "race/state" {"state":"idle"}.
    void publishIdle();

    // "race/state" {"state":"running","started_at":<epoch_ms>,"laps":N,"sectors":S}.
    void publishRunning(int laps, int sectors);

    // "race/state" once the leader completes the last lap:
    // {"state":"finished","winner":"Alice","elapsed_s":160.4,"laps":N}.
    void publishFinished(const std::string& winner, double elapsed_s, int laps);

    // "race/state" during the start lights: {"state":"countdown","lit":N}.
    void publishCountdown(int lit);

    // One timing line as reported in race/config.
    struct Checkpoint {
        std::string device;  // SPI device of its RC522, e.g. /dev/spidev0.1
        bool ok;             // reader found and running
    };

    // Retained "race/config" so the dashboard always knows the current settings:
    // {"led_order":[...],"led_pins":[...],"laps":N,
    //  "checkpoints":[{"device":"/dev/spidev0.0","ok":true},...],
    //  "spi_devices":["/dev/spidev0.0","/dev/spidev0.1"],
    //  "racers":[{"tag":"A1B2C3D4","name":"Alice"},...]}
    void publishConfig(const std::vector<int>& led_order, const std::vector<int>& led_pins,
                       int laps, const std::vector<Checkpoint>& checkpoints,
                       const std::vector<std::string>& spi_devices,
                       const std::map<std::string, std::string>& racers);

private:
    static void onConnect(mosquitto* mosq, void* self, int rc);
    static void onMessage(mosquitto* mosq, void* self, const mosquitto_message* msg);
    void publish(const std::string& topic, const std::string& payload, bool retain = false);

    std::string device_label_;
    CommandHandler on_command_;
    ConnectHandler on_connected_;
    mosquitto* mosq_ = nullptr;
};
