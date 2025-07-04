#include <iostream>
#include <unordered_map>
#include <string>
#include <regex>
#include <cmath>
#include <mutex>
#include <cstdio>

struct Point {
    float x;
    float y;
};

// Global state
std::mutex data_mutex;
std::unordered_map<int, Point> prev_vehicle_centers;

bool is_any_vehicle_moving(const std::string& xml) {
    std::lock_guard<std::mutex> lock(data_mutex);
    std::unordered_map<int, Point> current_vehicle_centers;

    std::regex vehicle_regex("<tt:Object ObjectId=\"(\\d+)\">[\\s\\S]*?<tt:Type(?: [^>]*)?>Vehicle</tt:Type>[\\s\\S]*?<tt:CenterOfGravity x=\"([\\d.]+)\" y=\"([\\d.]+)\"");
    auto begin = std::sregex_iterator(xml.begin(), xml.end(), vehicle_regex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        int object_id = std::stoi((*it)[1]);
        Point cog = { std::stof((*it)[2]), std::stof((*it)[3]) };
        current_vehicle_centers[object_id] = cog;

        auto prev_it = prev_vehicle_centers.find(object_id);
        if (prev_it != prev_vehicle_centers.end()) {
            float dx = cog.x - prev_it->second.x;
            float dy = cog.y - prev_it->second.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > 15.0f) {
                prev_vehicle_centers = std::move(current_vehicle_centers);
                return true;
            }
        }
    }

    prev_vehicle_centers = std::move(current_vehicle_centers);
    return false;
}

bool check_linecrossing_event(const std::string& xml, std::string& out_rule_name) {
    std::regex event_regex("<tt:Type(?: [^>]*)?>Human</tt:Type>[\\s\\S]*?<tt:SimpleItem Name=\"RuleName\" Value=\"([^\"]+)\"/>");
    std::smatch match;
    if (std::regex_search(xml, match, event_regex)) {
        out_rule_name = match[1];
        return true;
    }
    return false;
}

void main_loop(const std::string& xml) {
    std::string rule_name;
    if (check_linecrossing_event(xml, rule_name)) {
        if (is_any_vehicle_moving(xml)) {
            std::cout << "[ALERT] Human crossed line '" << rule_name
                      << "' while vehicle was moving!" << std::endl;
        }
    }
}

void metadata_thread() {
    const std::string cmd =
        "ffmpeg -i rtsp://admin:admin123@192.168.0.46:554/0/onvif/profile2/media.smp "
        "-map 0:1 -f data - 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "[ERROR] Failed to open ffmpeg pipe" << std::endl;
        return;
    }

    constexpr int BUFFER_SIZE = 8192;
    char buffer[BUFFER_SIZE];
    std::string xml_buffer;

    while (fgets(buffer, BUFFER_SIZE, pipe)) {
        xml_buffer += buffer;

        // 메타데이터 블록의 끝을 특정 태그로 인식 (Frame 단위 처리)
        if (xml_buffer.find("</tt:Frame>") != std::string::npos) {
            main_loop(xml_buffer);  // 1 프레임 단위 이벤트 처리
            xml_buffer.clear();
        }
    }

    pclose(pipe);
}

int main() {
    metadata_thread(); // 실시간 메타데이터 처리 시작
    return 0;
}
