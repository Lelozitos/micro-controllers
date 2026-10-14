#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

// Reads an RC522 (NXP MFRC522) 13.56 MHz RFID module over SPI (Linux spidev).
// A background thread polls for a card every RFID_POLL_MS and reports its UID
// as uppercase hex (e.g. "A1B2C3D4"). The callback fires repeatedly while a
// card stays on the reader, so callers must de-duplicate.
class RfidReader {
public:
    using TagCallback = std::function<void(const std::string& tag_id)>;

    RfidReader(std::string spi_device, TagCallback on_tag);
    ~RfidReader();

    RfidReader(const RfidReader&) = delete;
    RfidReader& operator=(const RfidReader&) = delete;

    // True if the SPI device opened and the chip answered with a known version.
    bool isOpen() const { return ok_; }

private:
    void run();

    bool initChip();
    void writeReg(uint8_t reg, uint8_t value);
    uint8_t readReg(uint8_t reg);
    void setBits(uint8_t reg, uint8_t mask);
    void clearBits(uint8_t reg, uint8_t mask);

    // Sends `data` to the card and collects the reply. `tx_last_bits` is the
    // number of bits to send from the last byte (0 = all 8). Returns false on
    // timeout or error.
    bool transceive(const std::vector<uint8_t>& data, uint8_t tx_last_bits,
                    std::vector<uint8_t>& reply);
    bool readUid(std::string& uid_hex);

    std::string device_;
    TagCallback on_tag_;
    int fd_ = -1;
    bool ok_ = false;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};
