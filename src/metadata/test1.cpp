#include <iostream>
#include <ctime>
#include <iomanip>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <vector>
#include <cstring>

#define DLE  0x10
#define STX  0x02
#define ETX  0x03
#define CMD_SYNC_TIME 0x03
#define NUM_BOARDS 4

const char* SERIAL_PORTS[NUM_BOARDS] = {
    "/dev/ttyAMA0",  // ID 1
    "/dev/ttyAMA2",  // ID 2
    "/dev/ttyAMA1",  // ID 3
    "/dev/ttyAMA3"   // ID 4
};

uint8_t reverse(uint8_t val, int bits) {
    uint8_t res = 0;
    for (int i = 0; i < bits; ++i)
        res = (res << 1) | ((val >> i) & 1);
    return res;
}

uint16_t reverse16(uint16_t val, int bits) {
    uint16_t res = 0;
    for (int i = 0; i < bits; ++i)
        res = (res << 1) | ((val >> i) & 1);
    return res;
}

uint16_t crc16(const std::vector<uint8_t>& data) {
    uint16_t crc = 0;
    for (auto b : data) {
        crc ^= reverse(b, 8) << 8;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x8005 : crc << 1;
    }
    return reverse16(crc, 16) & 0xFFFF;
}

int open_serial(const char* device) {
    int fd = open(device, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror(device);
        exit(1);
    }

    termios tty{};
    tcgetattr(fd, &tty);

    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;

    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}

std::vector<uint8_t> encode_frame(uint8_t cmd, int board_id, const std::vector<uint8_t>& extra_data) {
    uint8_t dst_mask = 1 << (board_id - 1);
    std::vector<uint8_t> payload = { dst_mask, cmd };
    payload.insert(payload.end(), extra_data.begin(), extra_data.end());

    uint16_t crc = crc16(payload);
    payload.push_back((crc >> 8) & 0xFF);
    payload.push_back(crc & 0xFF);

    std::vector<uint8_t> frame = { DLE, STX };
    for (uint8_t b : payload) {
        if (b == DLE) {
            frame.push_back(DLE);
            frame.push_back(DLE);
        } else {
            frame.push_back(b);
        }
    }
    frame.push_back(DLE);
    frame.push_back(ETX);
    return frame;
}

int main() {
    int fds[NUM_BOARDS];
    for (int i = 0; i < NUM_BOARDS; ++i) {
        fds[i] = open_serial(SERIAL_PORTS[i]);
    }

    while (true) {
        time_t now = time(nullptr);
        struct tm* t = localtime(&now);

        int hour = t->tm_hour;
        bool is_pm = false;
        if (hour >= 12) {
            is_pm = true;
            if (hour > 12) hour -= 12;
        }
        if (hour == 0) hour = 12;

        std::vector<uint8_t> time_payload = {
            static_cast<uint8_t>(t->tm_year % 100),
            static_cast<uint8_t>(t->tm_mon + 1),
            static_cast<uint8_t>(t->tm_mday),
            static_cast<uint8_t>(hour),
            static_cast<uint8_t>(t->tm_min),
            static_cast<uint8_t>(t->tm_sec),
            static_cast<uint8_t>(is_pm ? 1 : 0)
        };

        for (int board_id = 1; board_id <= NUM_BOARDS; ++board_id) {
            auto frame = encode_frame(CMD_SYNC_TIME, board_id, time_payload);
            write(fds[board_id - 1], frame.data(), frame.size());
            tcdrain(fds[board_id - 1]);

            std::cout << "[SENT to Board " << board_id << "] ";
            for (uint8_t b : frame)
                std::cout << std::hex << std::setw(2) << std::setfill('0') << int(b) << ' ';
            std::cout << std::endl;
        }

        sleep(5);
    }

    for (int i = 0; i < NUM_BOARDS; ++i) {
        close(fds[i]);
    }

    return 0;
}


/* g++ test1.cpp -o send_time --std=c++17*/

