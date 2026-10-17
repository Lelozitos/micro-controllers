#include "race_manager.hpp"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace {

const char* LOG_HEADER =
    "race_start,tag_id,racer_name,checkpoint,lap,sector,sector_time_s,lap_time_s,elapsed_s,"
    "finish_place";

}  // namespace

RaceManager::RaceManager(std::string racers_file, std::string log_file, double min_lap_s)
    : racers_file_(std::move(racers_file)),
      log_file_(std::move(log_file)),
      min_lap_s_(min_lap_s) {
    loadRacers();
}

void RaceManager::loadRacers() {
    std::lock_guard<std::mutex> lock(mutex_);
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

std::map<std::string, std::string> RaceManager::racers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return racers_;
}

bool RaceManager::setRacers(const std::vector<std::pair<std::string, std::string>>& racers) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Rewritten in place (not via a temp file + rename) so the file keeps
    // its owner even though the timer runs as root.
    std::ofstream out(racers_file_, std::ios::trunc);
    if (!out) return false;
    racers_.clear();
    for (const auto& [tag, name] : racers) {
        out << tag << ',' << name << '\n';
        racers_[tag] = name;
    }
    return static_cast<bool>(out);
}

std::string RaceManager::nameForTag(const std::string& tag_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nameForTagLocked(tag_id);
}

std::string RaceManager::nameForTagLocked(const std::string& tag_id) const {
    if (tag_id.empty()) return "UNKNOWN";
    auto it = racers_.find(tag_id);
    return it != racers_.end() ? it->second : "UNKNOWN_TAG:" + tag_id;
}

void RaceManager::startRace(int laps, int checkpoints) {
    std::lock_guard<std::mutex> lock(mutex_);
    laps_ = laps < 1 ? 1 : laps;
    checkpoints_ = checkpoints < 1 ? 1 : checkpoints;
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

Crossing RaceManager::recordCrossing(int checkpoint, const std::string& tag_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    Crossing c;
    if (!running_ || checkpoint < 0 || checkpoint >= checkpoints_) return c;

    double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_)
                     .count();
    const int n_sectors = checkpoints_ > 1 ? checkpoints_ : 0;
    auto [it, is_new] = progress_.try_emplace(tag_id);
    RacerProgress& p = it->second;
    if (is_new) p.sectors.assign(n_sectors, -1);
    if (p.finished) return c;
    // Sector lines only count once the racer's lap 1 has started.
    if (checkpoint > 0 && !p.started) return c;

    // Same tag at the same line again too soon = the same pass.
    auto seen = p.last_read_s.find(checkpoint);
    if (seen != p.last_read_s.end() && now - seen->second < min_lap_s_) return c;
    double& last_read = p.last_read_s[checkpoint];

    c.checkpoint = checkpoint;
    c.tag_id = tag_id;
    c.racer_name = nameForTagLocked(tag_id);
    c.elapsed_s = now;

    if (checkpoint > 0) {
        // Sector line: only forward in track order within this lap.
        if (checkpoint <= p.last_line) return c;
        last_read = now;
        c.counted = true;
        c.lap = p.laps + 1;
        c.sector = checkpoint;
        c.sector_time_s = checkpoint == p.last_line + 1 ? now - p.last_line_s : -1;
        p.sectors[checkpoint - 1] = c.sector_time_s;
        p.last_line = checkpoint;
        p.last_line_s = now;
        crossings_.push_back(c);
        return c;
    }

    last_read = now;
    c.counted = true;

    // First time over the finish line: lap 1 starts, nothing is completed.
    if (!p.started) {
        p.started = true;
        c.start = true;
        c.lap = 0;
        p.lap_start_s = now;
        p.last_line = 0;
        p.last_line_s = now;
        crossings_.push_back(c);
        return c;
    }

    // Finish line: a lap. Its last sector is only known if the last sector
    // line was crossed this lap.
    if (n_sectors) {
        c.sector = n_sectors;
        c.sector_time_s = p.last_line == n_sectors - 1 ? now - p.last_line_s : -1;
        p.sectors[n_sectors - 1] = c.sector_time_s;
        c.sectors = p.sectors;
    }
    c.lap = ++p.laps;
    c.lap_time_s = now - p.lap_start_s;
    p.lap_start_s = now;
    p.last_line = 0;
    p.last_line_s = now;
    p.sectors.assign(n_sectors, -1);

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

bool RaceManager::allClassified() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (finishers_ == 0) return false;
    for (const auto& [tag, p] : progress_) {
        if (p.started && !p.finished) return false;
    }
    return true;
}

void RaceManager::saveLog() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (crossings_.empty()) return;

    // A log written by an older version has different columns: move it
    // aside instead of mixing the two.
    std::string header;
    bool exists = false;
    {
        std::ifstream in(log_file_);
        exists = in.good() && std::getline(in, header);
    }
    if (exists && header != LOG_HEADER) {
        std::rename(log_file_.c_str(), (log_file_ + ".old").c_str());
        exists = false;
    }

    std::ofstream out(log_file_, std::ios::app);
    if (!exists) out << LOG_HEADER << '\n';

    std::time_t t = std::chrono::system_clock::to_time_t(start_wall_);
    std::tm tm{};
    localtime_r(&t, &tm);
    for (const auto& c : crossings_) {
        out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << ',' << c.tag_id << ','
            << c.racer_name << ',' << c.checkpoint << ',' << c.lap << ',' << c.sector << ',';
        if (c.sector_time_s >= 0) out << c.sector_time_s;
        out << ',';
        if (c.checkpoint == 0) out << c.lap_time_s;
        out << ',' << c.elapsed_s << ',' << c.place << '\n';
    }
}
