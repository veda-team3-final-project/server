#include "board_control.h"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <iostream>

#define DLE 0x10
#define STX 0x02
#define ETX 0x03
#define ACK  0xAA
#define NACK 0x55
#define CMD_LCD_ON  0x01
#define CMD_LCD_OFF 0x02

uint8_t reverse(uint8_t val, int bits);
uint16_t crc16(const std::vector<uint8_t>& data);
uint16_t reverse16(uint16_t val, int bits);

BoardController::BoardController(const std::string& device, int board_id)
    : id(board_id)
{
    open_port(device);
}

BoardController::~BoardController() {
    close(fd);
}

void BoardController::open_port(const std::string& device) {
    fd = open(device.c_str(), O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("open");
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
    tcsetattr(fd, TCSANOW, &tty);
}

void BoardController::send_lcd_on() {
    send_frame(CMD_LCD_ON);
}

void BoardController::send_lcd_off() {
    send_frame(CMD_LCD_OFF);
}

void BoardController::send_frame(uint8_t command) {
    auto frame = encode_frame(command);
    write(fd, frame.data(), frame.size());
}

//ACK/NACK 신뢰성 송신 확장
bool BoardController::send_lcd_on_with_ack(int retries, int timeout_ms) {
    return send_frame_with_ack(CMD_LCD_ON, retries, timeout_ms);
}

bool BoardController::send_lcd_off_with_ack(int retries, int timeout_ms) {
    return send_frame_with_ack(CMD_LCD_OFF, retries, timeout_ms);
}

bool BoardController::send_frame_with_ack(uint8_t command, int retries, int timeout_ms) {
    auto frame = encode_frame(command);
    for (int attempt = 0; attempt < retries; ++attempt) {
        // flush input buffer before sending
        tcflush(fd, TCIFLUSH);
        write(fd, frame.data(), frame.size());

        // FSM state
        enum { WAIT_DLE, WAIT_STX, IN_FRAME, WAIT_ETX } state = WAIT_DLE;
        std::vector<uint8_t> payload;
        uint8_t last_byte = 0;
        int waited = 0;
        const int step_ms = 10;

        while (waited < timeout_ms) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(fd, &readfds);

            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = step_ms * 1000;

            int ret = select(fd + 1, &readfds, NULL, NULL, &tv);
            if (ret > 0 && FD_ISSET(fd, &readfds)) {
                uint8_t rx;
                ssize_t n = read(fd, &rx, 1);
                if (n == 1) {
                    switch (state) {
                        case WAIT_DLE:
                            if (rx == DLE) state = WAIT_STX;
                            break;
                        case WAIT_STX:
                            if (rx == STX) {
                                payload.clear();
                                state = IN_FRAME;
                            } else if (rx == DLE) {
                                // stay in WAIT_STX
                            } else {
                                state = WAIT_DLE;
                            }
                            break;
                        case IN_FRAME:
                            if (rx == DLE) {
                                state = WAIT_ETX;
                            } else {
                                payload.push_back(rx);
                            }
                            break;
                        case WAIT_ETX:
                            if (rx == ETX) {
                                // 프레임 종료, payload 파싱
                                if (payload.size() == 3) {
                                    uint8_t resp_cmd = payload[0];
                                    uint16_t recv_crc = (payload[1] << 8) | payload[2];
                                    std::vector<uint8_t> crc_input = { resp_cmd };
                                    uint16_t calc_crc = crc16(crc_input);
                                    if (recv_crc == calc_crc) {
                                        if (resp_cmd == ACK) {
                                            std::cout << "[Board " << id << "] ACK received" << std::endl;
                                            return true;
                                        } else if (resp_cmd == NACK) {
                                            std::cout << "[Board " << id << "] NACK received, retrying..." << std::endl;
                                            break;
                                        }
                                    } else {
                                        std::cout << "[Board " << id << "] Bad CRC in response" << std::endl;
                                    }
                                } else {
                                    std::cout << "[Board " << id << "] Bad response frame size" << std::endl;
                                }
                                // 프레임 끝, 다시 대기
                                state = WAIT_DLE;
                                payload.clear();
                            } else if (rx == DLE) {
                                // DLE DLE 이스케이프: 실제 DLE 데이터
                                payload.push_back(DLE);
                                state = IN_FRAME;
                            } else {
                                // 잘못된 패턴
                                state = WAIT_DLE;
                            }
                            break;
                    }
                }
            }
            waited += step_ms;
        }
        std::cout << "[Board " << id << "] Timeout waiting for ACK/NACK, retrying..." << std::endl;
        usleep(100 * 1000);
    }
    std::cout << "[Board " << id << "] Failed to get ACK after " << retries << " attempts!" << std::endl;
    return false;
}

std::vector<uint8_t> BoardController::encode_frame(uint8_t command) {
    uint8_t dst_mask = 1 << (id - 1);
    std::vector<uint8_t> payload = { dst_mask, command };
    uint16_t crc = crc16(payload);
    payload.push_back((crc >> 8) & 0xFF);
    payload.push_back(crc & 0xFF);

    std::vector<uint8_t> frame = { DLE, STX };
    for (auto b : payload) {
        if (b == DLE) frame.insert(frame.end(), { DLE, DLE });
        else frame.push_back(b);
    }
    frame.push_back(DLE);
    frame.push_back(ETX);
    return frame;
}

// --- CRC Functions ---
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
