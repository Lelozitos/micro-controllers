#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// One read of a racer's tag at the line that counted as a lap.
struct Crossing {
    bool counted = false;    // false: race not running, or same tag read again too soon
    std::string tag_id;
    std::string racer_name;  // "UNKNOWN_TAG:<id>" if the tag isn't in racers.csv
    int lap = 0;             // laps this racer has now completed
    double lap_time_s = 0;   // time of this lap
    double elapsed_s = 0;    // race time at this crossing
    int place = 0;           // finishing position if this crossing finished the racer, else 0
};

// Lap race: every crossing of the line is a lap. The first racer to complete
// the set number of laps wins; after that (chequered flag) every other racer
// is classified the next time they cross the line, F1-style.
class RaceManager {
public:
    RaceManager(std::string racers_file, std::string log_file, double min_lap_s);

    void loadRacers();  // (re)reads the "tag_id,name" CSV

    bool isRunning() const { return running_; }
    bool flagOut() const { return flag_out_; }  // leader has finished
    void startRace(int laps);
    void reset();

    // Called for every RFID read. Reads of the same tag closer together than
    // min_lap_s (the reader repeats while a tag is held there) don't count.
    Crossing recordCrossing(const std::string& tag_id);

    void saveLog() const;

    std::string nameForTag(const std::string& tag_id) const;

private:
    struct RacerProgress {
        int laps = 0;
        double last_crossing_s = 0;  // race time of the last counted crossing
        bool finished = false;
    };

    std::string racers_file_;
    std::string log_file_;
    double min_lap_s_;
    std::map<std::string, std::string> racers_;  // tag_id -> name

    mutable std::mutex mutex_;  // RFID thread + main loop
    std::atomic<bool> running_{false};
    std::atomic<bool> flag_out_{false};
    int laps_ = 1;
    int finishers_ = 0;
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::system_clock::time_point start_wall_;
    std::map<std::string, RacerProgress> progress_;  // by tag_id
    std::vector<Crossing> crossings_;
};
