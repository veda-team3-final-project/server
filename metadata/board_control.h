#pragma once
#include <vector>
#include <string>
#include <cstdint>


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

class BoardController {
public:
    BoardController(const std::string& device, int board_id);
    ~BoardController();

    void send_lcd_on();
    void send_lcd_off();

    // 확장: ACK/NACK 신뢰성 송신
    bool send_lcd_on_with_ack(int retries = 3, int timeout_ms = 1000);
    bool send_lcd_off_with_ack(int retries = 3, int timeout_ms = 1000);
    
private:
    int fd;
    int id;

    void open_port(const std::string& device);
    void send_frame(uint8_t command);

    // 확장: ACK/NACK 신뢰성 송신
    bool send_frame_with_ack(uint8_t command, int retries, int timeout_ms);

    std::vector<uint8_t> encode_frame(uint8_t command);
};
