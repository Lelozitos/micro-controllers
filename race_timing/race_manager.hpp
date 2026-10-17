#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// One read of a racer's tag at a timing line that counted.
//
// The track has one or more timing lines (checkpoints), each with its own
// RFID reader. Checkpoint 0 is the start/finish line. A racer's first
// crossing of it starts their lap 1 (rolling start); every crossing after
// that completes a lap. Checkpoints 1..N-1 are sector lines placed in track order, so with N
// lines a lap has N sectors: S1 = finish -> line 1, ..., SN = line N-1 ->
// finish. With a single line there are no sectors, only laps.
struct Crossing {
    bool counted = false;    // false: race not running, repeat read, or out of order
    bool start = false;      // the racer's first finish-line crossing: lap 1 starts now
    int checkpoint = 0;      // 0 = finish line
    std::string tag_id;
    std::string racer_name;  // "UNKNOWN_TAG:<id>" if the tag isn't in racers.csv
    int lap = 0;             // finish: laps now completed (0 on the start crossing);
                             // sector line: lap in progress
    int sector = 0;          // sector this crossing completed (1-based), 0 if no sectors
    double sector_time_s = -1;  // < 0: unknown (the previous line was missed)
    double lap_time_s = 0;   // finish line only
    double elapsed_s = 0;    // race time at this crossing
    std::vector<double> sectors;  // finish line only: the lap's sectors, < 0 = missed
    int place = 0;           // finishing position if this crossing finished the racer, else 0
};

// Lap race: after the start crossing, every crossing of the finish line is a
// lap. The first racer to
// complete the set number of laps wins; after that (chequered flag) every
// other racer is classified the next time they cross the finish line, F1-style.
class RaceManager {
public:
    RaceManager(std::string racers_file, std::string log_file, double min_lap_s);

    void loadRacers();  // (re)reads the "tag_id,name" CSV
    std::map<std::string, std::string> racers() const;
    // Replaces the roster and rewrites the CSV. Safe during a race: crossings
    // from then on use the new names. Returns false if the file can't be written.
    bool setRacers(const std::vector<std::pair<std::string, std::string>>& racers);

    bool isRunning() const { return running_; }
    bool flagOut() const { return flag_out_; }  // leader has finished
    void startRace(int laps, int checkpoints);
    void reset();

    // Called for every RFID read. Reads of the same tag at the same line
    // closer together than min_lap_s (the reader repeats while a tag is held
    // there) don't count; neither do sector lines read out of track order.
    Crossing recordCrossing(int checkpoint, const std::string& tag_id);

    void saveLog() const;

    // True once every racer who started has been classified (and at least
    // one has): nobody is left on track, the race is over.
    bool allClassified() const;

    std::string nameForTag(const std::string& tag_id) const;

private:
    std::string nameForTagLocked(const std::string& tag_id) const;  // mutex_ held

    struct RacerProgress {
        bool started = false;        // has crossed the finish line once (lap 1 running)
        int laps = 0;
        double lap_start_s = 0;      // race time the current lap started
        int last_line = 0;           // last checkpoint crossed this lap (0 = lap start)
        double last_line_s = 0;      // race time of that crossing
        std::vector<double> sectors;  // current lap, < 0 = not (yet) timed
        std::map<int, double> last_read_s;  // by checkpoint, for repeat reads
        bool finished = false;
    };

    std::string racers_file_;
    std::string log_file_;
    double min_lap_s_;
    std::map<std::string, std::string> racers_;  // tag_id -> name

    mutable std::mutex mutex_;  // RFID threads + main loop
    std::atomic<bool> running_{false};
    std::atomic<bool> flag_out_{false};
    int laps_ = 1;
    int checkpoints_ = 1;
    int finishers_ = 0;
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::system_clock::time_point start_wall_;
    std::map<std::string, RacerProgress> progress_;  // by tag_id
    std::vector<Crossing> crossings_;
};
