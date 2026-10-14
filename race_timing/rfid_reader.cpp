#include "rfid_reader.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cstdio>
#include <vector>

namespace {

int openSerial(const std::string& port) {
    int fd = ::open(port.c_str(), O_RDONLY | O_NOCTTY);
    if (fd < 0) {
        std::perror(("open " + port).c_str());
        return -1;
    }

    termios tty{};
    tcgetattr(fd, &tty);
    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_lflag = 0;
    tty.c_iflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}

}  // namespace

RfidReader::RfidReader(std::string port, TagCallback on_tag)
    : port_(std::move(port)), on_tag_(std::move(on_tag)) {
    fd_ = openSerial(port_);
    thread_ = std::thread(&RfidReader::run, this);
}

RfidReader::~RfidReader() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    if (fd_ >= 0) ::close(fd_);
}

void RfidReader::run() {
    if (fd_ < 0) return;

    std::vector<char> frame;
    frame.reserve(16);
    bool in_frame = false;
    char byte;

    while (!stop_) {
        ssize_t n = ::read(fd_, &byte, 1);
        if (n <= 0) continue;

        if (byte == 0x02) {  // STX
            frame.clear();
            in_frame = true;
            continue;
        }
        if (byte == 0x03) {  // ETX
            if (in_frame && frame.size() >= 10) {
                std::string tag(frame.begin(), frame.begin() + 10);
                if (on_tag_) on_tag_(tag);
            }
            in_frame = false;
            continue;
        }
        if (in_frame) frame.push_back(byte);
    }
}
