#include "race_manager.hpp"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>

RaceManager::RaceManager(std::string racers_file, std::string log_file, double min_lap_s)
    : racers_file_(std::move(racers_file)),
      log_file_(std::move(log_file)),
      min_lap_s_(min_lap_s) {
    loadRacers();
}

void RaceManager::loadRacers() {
    racers_.clear();
    std::ifstream in(racers_file_);
    if (!in) {
        std::cerr << "warning: could not open " << racers_file_
                  << " (no racers loaded)\n";
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto comma = line.find(',');
        if (comma == std::string::npos) continue;
        std::string tag = line.substr(0, comma);
        std::string name = line.substr(comma + 1);
        if (!tag.empty() && !name.empty()) racers_[tag] = name;
    }
}

std::string RaceManager::nameForTag(const std::string& tag_id) const {
    if (tag_id.empty()) return "UNKNOWN";
    auto it = racers_.find(tag_id);
    return it != racers_.end() ? it->second : "UNKNOWN_TAG:" + tag_id;
}

void RaceManager::startRace(int laps) {
    std::lock_guard<std::mutex> lock(mutex_);
    laps_ = laps < 1 ? 1 : laps;
    finishers_ = 0;
    progress_.clear();
    crossings_.clear();
    flag_out_ = false;
    start_time_ = std::chrono::steady_clock::now();
    start_wall_ = std::chrono::system_clock::now();
    running_ = true;
}

void RaceManager::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    flag_out_ = false;
    progress_.clear();
    crossings_.clear();
}

Crossing RaceManager::recordCrossing(const std::string& tag_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    Crossing c;
    if (!running_) return c;

    double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_)
                     .count();
    RacerProgress& p = progress_[tag_id];  // new racers start at 0 laps, last crossing = start
    if (p.finished || now - p.last_crossing_s < min_lap_s_) return c;

    c.counted = true;
    c.tag_id = tag_id;
    c.racer_name = nameForTag(tag_id);
    c.lap = ++p.laps;
    c.lap_time_s = now - p.last_crossing_s;
    c.elapsed_s = now;
    p.last_crossing_s = now;

    // The leader finishes on the last lap; once the flag is out, everyone
    // else finishes on their next crossing.
    if (flag_out_ || p.laps >= laps_) {
        p.finished = true;
        c.place = ++finishers_;
        flag_out_ = true;
    }
    crossings_.push_back(c);
    return c;
}

void RaceManager::saveLog() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (crossings_.empty()) return;

    bool exists = std::ifstream(log_file_).good();
    std::ofstream out(log_file_, std::ios::app);
    if (!exists) out << "race_start,tag_id,racer_name,lap,lap_time_s,elapsed_s,finish_place\n";

    std::time_t t = std::chrono::system_clock::to_time_t(start_wall_);
    std::tm tm{};
    localtime_r(&t, &tm);
    for (const auto& c : crossings_) {
        out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << ',' << c.tag_id << ','
            << c.racer_name << ',' << c.lap << ',' << c.lap_time_s << ',' << c.elapsed_s
            << ',' << c.place << '\n';
    }
}
