#pragma once

#include <string>
#include <map>

using namespace std;

// 설정 구조체
struct AppConfig {
    // .env에서 로드되는 값들
    string username;
    string password;
    string host;
    string rtsp_port;
    string rtsp_path;
    string db_file;
    string trackid;
    
    // config.json에서 로드되는 값들
    float dist_threshold;
    float parallelism_threshold;
    size_t frame_cache_size;
    int history_size;
    float scale_x;
    float scale_y;
    float base_x;
    float base_y;
    map<string, string> board_ports;
    int retry_count;
    int timeout_ms;
};

// 전역 설정 인스턴스
extern AppConfig g_config;

// 설정 로드 함수들
bool load_env_variables();
bool load_json_config();
bool load_all_config();

// RTSP URL 생성 함수
string get_rtsp_url();
