#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

struct Crossing {
    int place;               // finish order, 1-based; 0 if race wasn't running
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

    // Called from the finish-line callback. Returns the recorded crossing
    // (place == 0 if the race isn't running).
    Crossing recordCrossing(const std::string& tag_id);

    const std::vector<Crossing>& results() const { return crossings_; }
    void saveLog() const;

    std::string nameForTag(const std::string& tag_id) const;

private:
    std::string racers_file_;
    std::string log_file_;
    std::map<std::string, std::string> racers_;  // tag_id -> name

    bool running_ = false;
    std::chrono::steady_clock::time_point start_time_;
    std::vector<Crossing> crossings_;
};
