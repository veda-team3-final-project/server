#include "config_manager.hpp"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdlib>

using json = nlohmann::json;

// 전역 설정 인스턴스
AppConfig g_config;

// .env 파일을 읽어서 환경 변수로 설정하는 함수
bool load_env_variables() {
    ifstream file(".env");
    string line;
    
    if (!file.is_open()) {
        cout << "[WARNING] .env 파일을 열 수 없습니다." << endl;
        return false;
    }
    
    while (getline(file, line)) {
        // 주석과 빈 줄 제거
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        size_t pos = line.find('=');
        if (pos != string::npos) {
            string key = line.substr(0, pos);
            string value = line.substr(pos + 1);
            
            // 앞뒤 공백 제거
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            // 환경 변수로 설정
            setenv(key.c_str(), value.c_str(), 1);
        }
    }
    
    file.close();
    
    // 환경 변수에서 설정값 읽기
    const char* username = getenv("USERNAME");
    const char* password = getenv("PASSWORD");
    const char* host = getenv("HOST");
    const char* rtsp_port = getenv("RTSP_PORT");
    const char* rtsp_path = getenv("RTSP_PATH");
    const char* db_file = getenv("DB_FILE");
    const char* trackid = getenv("TRACKID");
    
    if (!username || !password || !host || !rtsp_port || !rtsp_path || !db_file) {
        cout << "[ERROR] 필수 환경 변수가 설정되지 않았습니다." << endl;
        return false;
    }
    
    g_config.username = username;
    g_config.password = password;
    g_config.host = host;
    g_config.rtsp_port = rtsp_port;
    g_config.rtsp_path = rtsp_path;
    g_config.db_file = db_file;
    g_config.trackid = trackid ? trackid : "";
    
    cout << "[INFO] .env 파일에서 환경 변수를 로드했습니다." << endl;
    return true;
}

// config.json 파일을 읽어서 설정값 로드
bool load_json_config() {
    ifstream config_file("config.json");
    if (!config_file.is_open()) {
        cout << "[ERROR] config.json 파일을 열 수 없습니다." << endl;
        return false;
    }
    
    json config;
    try {
        config_file >> config;
        
        // detection 설정
        g_config.dist_threshold = config["detection"]["dist_threshold"].get<float>();
        g_config.parallelism_threshold = config["detection"]["parallelism_threshold"].get<float>();
        
        // cache 설정
        g_config.frame_cache_size = config["cache"]["frame_cache_size"].get<size_t>();
        g_config.history_size = config["cache"]["history_size"].get<int>();
        
        // scale 설정
        g_config.scale_x = config["scale"]["x"].get<float>();
        g_config.scale_y = config["scale"]["y"].get<float>();
        g_config.base_x = config["scale"]["base_x"].get<float>();
        g_config.base_y = config["scale"]["base_y"].get<float>();
        
        // board 설정
        g_config.retry_count = config["board"]["retry_count"].get<int>();
        g_config.timeout_ms = config["board"]["timeout_ms"].get<int>();
        
        // 포트 매핑
        g_config.board_ports.clear();
        for (int i = 1; i <= 4; i++) {
            string port = config["board"]["ports"][to_string(i)].get<string>();
            g_config.board_ports[to_string(i)] = port;
        }
        
        cout << "[INFO] config.json 파일을 로드했습니다." << endl;
        return true;
        
    } catch (const exception& e) {
        cout << "[ERROR] config.json 파싱 오류: " << e.what() << endl;
        return false;
    }
}

// 모든 설정 로드
bool load_all_config() {
    bool env_ok = load_env_variables();
    bool json_ok = load_json_config();
    
    if (env_ok && json_ok) {
        cout << "[INFO] 모든 설정이 성공적으로 로드되었습니다." << endl;
        return true;
    }
    
    return false;
}

// RTSP URL 생성
string get_rtsp_url() {
    string password = g_config.password;
    
    // @ 문자를 URL 인코딩 (%40)
    size_t at_pos = password.find('@');
    if (at_pos != string::npos) {
        password.replace(at_pos, 1, "%40");
    }
    
    stringstream rtsp_url;
    rtsp_url << "rtsp://" << g_config.username << ":" << password 
             << "@" << g_config.host << ":" << g_config.rtsp_port << g_config.rtsp_path;
    
    return rtsp_url.str();
}
