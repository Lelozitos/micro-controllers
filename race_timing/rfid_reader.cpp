#include "rfid_reader.hpp"

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>

#include "config.hpp"

namespace {

// MFRC522 registers (datasheet section 9).
constexpr uint8_t CommandReg = 0x01;
constexpr uint8_t ComIrqReg = 0x04;
constexpr uint8_t ErrorReg = 0x06;
constexpr uint8_t FIFODataReg = 0x09;
constexpr uint8_t FIFOLevelReg = 0x0A;
constexpr uint8_t ControlReg = 0x0C;
constexpr uint8_t BitFramingReg = 0x0D;
constexpr uint8_t CollReg = 0x0E;
constexpr uint8_t ModeReg = 0x11;
constexpr uint8_t TxModeReg = 0x12;
constexpr uint8_t RxModeReg = 0x13;
constexpr uint8_t TxControlReg = 0x14;
constexpr uint8_t TxASKReg = 0x15;
constexpr uint8_t ModWidthReg = 0x24;
constexpr uint8_t TModeReg = 0x2A;
constexpr uint8_t TPrescalerReg = 0x2B;
constexpr uint8_t TReloadRegH = 0x2C;
constexpr uint8_t TReloadRegL = 0x2D;
constexpr uint8_t VersionReg = 0x37;

// MFRC522 commands.
constexpr uint8_t CmdIdle = 0x00;
constexpr uint8_t CmdTransceive = 0x0C;
constexpr uint8_t CmdSoftReset = 0x0F;

// ISO 14443A card commands.
constexpr uint8_t PICC_WUPA = 0x52;         // wake up any card, including halted ones
constexpr uint8_t PICC_ANTICOLL_CL1 = 0x93;

constexpr uint32_t SPI_SPEED_HZ = 1000000;

}  // namespace

RfidReader::RfidReader(std::string spi_device, TagCallback on_tag)
    : device_(std::move(spi_device)), on_tag_(std::move(on_tag)) {
    fd_ = ::open(device_.c_str(), O_RDWR);
    if (fd_ < 0) {
        std::perror(("open " + device_).c_str());
        return;
    }
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = SPI_SPEED_HZ;
    ioctl(fd_, SPI_IOC_WR_MODE, &mode);
    ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    ok_ = initChip();
    if (ok_) thread_ = std::thread(&RfidReader::run, this);
}

RfidReader::~RfidReader() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    if (fd_ >= 0) ::close(fd_);
}

void RfidReader::writeReg(uint8_t reg, uint8_t value) {
    uint8_t tx[2] = {static_cast<uint8_t>((reg << 1) & 0x7E), value};
    uint8_t rx[2] = {};
    spi_ioc_transfer t{};
    t.tx_buf = reinterpret_cast<unsigned long>(tx);
    t.rx_buf = reinterpret_cast<unsigned long>(rx);
    t.len = 2;
    t.speed_hz = SPI_SPEED_HZ;
    t.bits_per_word = 8;
    ioctl(fd_, SPI_IOC_MESSAGE(1), &t);
}

uint8_t RfidReader::readReg(uint8_t reg) {
    uint8_t tx[2] = {static_cast<uint8_t>(((reg << 1) & 0x7E) | 0x80), 0};
    uint8_t rx[2] = {};
    spi_ioc_transfer t{};
    t.tx_buf = reinterpret_cast<unsigned long>(tx);
    t.rx_buf = reinterpret_cast<unsigned long>(rx);
    t.len = 2;
    t.speed_hz = SPI_SPEED_HZ;
    t.bits_per_word = 8;
    ioctl(fd_, SPI_IOC_MESSAGE(1), &t);
    return rx[1];
}

void RfidReader::setBits(uint8_t reg, uint8_t mask) { writeReg(reg, readReg(reg) | mask); }
void RfidReader::clearBits(uint8_t reg, uint8_t mask) { writeReg(reg, readReg(reg) & ~mask); }

bool RfidReader::initChip() {
    writeReg(CommandReg, CmdSoftReset);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    uint8_t version = readReg(VersionReg);
    // 0x91/0x92 = genuine MFRC522 v1/v2; 0x88, 0x12, 0xB2 = common clones.
    if (version != 0x91 && version != 0x92 && version != 0x88 && version != 0x12 &&
        version != 0xB2) {
        std::fprintf(stderr, "RC522 not detected on %s (version reg = 0x%02X) - check wiring\n",
                     device_.c_str(), version);
        return false;
    }

    writeReg(TxModeReg, 0x00);
    writeReg(RxModeReg, 0x00);
    writeReg(ModWidthReg, 0x26);
    // Internal timer: ~25 ms timeout for each card exchange.
    writeReg(TModeReg, 0x80);
    writeReg(TPrescalerReg, 0xA9);
    writeReg(TReloadRegH, 0x03);
    writeReg(TReloadRegL, 0xE8);
    writeReg(TxASKReg, 0x40);   // 100% ASK modulation
    writeReg(ModeReg, 0x3D);    // CRC preset 0x6363
    setBits(TxControlReg, 0x03);  // antenna on

    std::printf("RC522 reader ready (chip version 0x%02X)\n", version);
    return true;
}

bool RfidReader::transceive(const std::vector<uint8_t>& data, uint8_t tx_last_bits,
                            std::vector<uint8_t>& reply) {
    writeReg(CommandReg, CmdIdle);
    writeReg(ComIrqReg, 0x7F);      // clear interrupt flags
    writeReg(FIFOLevelReg, 0x80);   // flush FIFO
    for (uint8_t b : data) writeReg(FIFODataReg, b);
    writeReg(BitFramingReg, tx_last_bits);
    writeReg(CommandReg, CmdTransceive);
    setBits(BitFramingReg, 0x80);   // StartSend

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(40);
    uint8_t irq = 0;
    while (true) {
        irq = readReg(ComIrqReg);
        if (irq & 0x30) break;                   // RxIRq or IdleIRq: done
        if (irq & 0x01) return false;            // TimerIRq: no card answered
        if (std::chrono::steady_clock::now() > deadline) return false;
    }
    clearBits(BitFramingReg, 0x80);

    if (readReg(ErrorReg) & 0x1B) return false;  // buffer overflow, collision, parity, protocol

    uint8_t n = readReg(FIFOLevelReg);
    reply.resize(n);
    for (uint8_t i = 0; i < n; ++i) reply[i] = readReg(FIFODataReg);
    return true;
}

bool RfidReader::readUid(std::string& uid_hex) {
    std::vector<uint8_t> reply;

    // Wake up any card in the field (7-bit short frame); answer is the 2-byte ATQA.
    clearBits(CollReg, 0x80);
    if (!transceive({PICC_WUPA}, 7, reply) || reply.size() != 2) return false;

    // Anticollision, cascade level 1: 4 UID bytes + BCC checksum.
    if (!transceive({PICC_ANTICOLL_CL1, 0x20}, 0, reply) || reply.size() != 5) return false;
    if ((reply[0] ^ reply[1] ^ reply[2] ^ reply[3]) != reply[4]) return false;

    char buf[9];
    std::snprintf(buf, sizeof(buf), "%02X%02X%02X%02X", reply[0], reply[1], reply[2], reply[3]);
    uid_hex = buf;
    return true;
}

void RfidReader::run() {
    while (!stop_) {
        std::string uid;
        if (readUid(uid) && on_tag_) on_tag_(uid);
        std::this_thread::sleep_for(std::chrono::milliseconds(config::RFID_POLL_MS));
    }
}
