#pragma once

#include <chrono>
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

struct Crossing {
    int place;               // finish order, 1-based; 0 if not recorded
    std::string tag_id;
    std::string racer_name;  // "UNKNOWN" if the tag wasn't recognized/read
    double elapsed_s;
};

class RaceManager {
public:
    explicit RaceManager(std::string racers_file, std::string log_file);

    void loadRacers();  // (re)reads the "tag_id,name" CSV

    bool isRunning() const { return running_; }
    void startRace();
    void stopRace();
    void reset();

    // Called for every RFID read. Only a tag's first read in a race counts;
    // returns place == 0 if the race isn't running or the tag already finished.
    Crossing recordCrossing(const std::string& tag_id);

    void saveLog() const;

    std::string nameForTag(const std::string& tag_id) const;

private:
    std::string racers_file_;
    std::string log_file_;
    std::map<std::string, std::string> racers_;  // tag_id -> name

    mutable std::mutex mutex_;  // RFID thread + button callbacks
    std::atomic<bool> running_{false};
    std::chrono::steady_clock::time_point start_time_;
    std::vector<Crossing> crossings_;
    std::set<std::string> finished_tags_;
};
