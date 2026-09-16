#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

// Reads an RDM6300 125 kHz RFID module over UART.
// Frame format: 0x02 STX, 10 ASCII-hex chars (tag ID), 2 ASCII-hex checksum, 0x03 ETX.
// The module transmits a frame every ~100-200 ms while a tag stays in range,
// so we simply keep the most recent tag + the time it was seen.
class RfidReader {
public:
    explicit RfidReader(std::string port);
    ~RfidReader();

    RfidReader(const RfidReader&) = delete;
    RfidReader& operator=(const RfidReader&) = delete;

    // Returns the last tag read within `window_seconds`, or "" if none / stale.
    std::string lastTagWithin(double window_seconds) const;

private:
    void run();

    std::string port_;
    int fd_ = -1;
    std::atomic<bool> stop_{false};
    std::thread thread_;

    mutable std::mutex mutex_;
    std::string last_tag_;
    std::chrono::steady_clock::time_point last_seen_;
};
