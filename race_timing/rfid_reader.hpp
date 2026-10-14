#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

// Reads an RDM6300 125 kHz RFID module over UART.
// Frame format: 0x02 STX, 10 ASCII-hex chars (tag ID), 2 ASCII-hex checksum, 0x03 ETX.
// The module transmits a frame every ~100-200 ms while a tag stays in range,
// so the callback fires repeatedly for the same tag - callers must de-duplicate.
class RfidReader {
public:
    using TagCallback = std::function<void(const std::string& tag_id)>;

    RfidReader(std::string port, TagCallback on_tag);
    ~RfidReader();

    RfidReader(const RfidReader&) = delete;
    RfidReader& operator=(const RfidReader&) = delete;

    bool isOpen() const { return fd_ >= 0; }

private:
    void run();

    std::string port_;
    TagCallback on_tag_;
    int fd_ = -1;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};
