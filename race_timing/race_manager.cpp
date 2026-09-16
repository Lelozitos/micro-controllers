#include "race_manager.hpp"

#include <fstream>
#include <iostream>

RaceManager::RaceManager(std::string racers_file, std::string log_file)
    : racers_file_(std::move(racers_file)), log_file_(std::move(log_file)) {
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

void RaceManager::startRace() {
    running_ = true;
    start_time_ = std::chrono::steady_clock::now();
    crossings_.clear();
}

void RaceManager::stopRace() { running_ = false; }

void RaceManager::reset() {
    running_ = false;
    crossings_.clear();
}

Crossing RaceManager::recordCrossing(const std::string& tag_id) {
    if (!running_) return {0, "", "", 0.0};

    double elapsed = std::chrono::duration<double>(
                          std::chrono::steady_clock::now() - start_time_)
                          .count();

    Crossing c{static_cast<int>(crossings_.size()) + 1, tag_id, nameForTag(tag_id),
               elapsed};
    crossings_.push_back(c);
    return c;
}

void RaceManager::saveLog() const {
    bool exists = std::ifstream(log_file_).good();
    std::ofstream out(log_file_, std::ios::app);
    if (!exists) out << "place,tag_id,racer_name,elapsed_s\n";
    for (const auto& c : crossings_) {
        out << c.place << ',' << c.tag_id << ',' << c.racer_name << ',' << c.elapsed_s
            << '\n';
    }
}
