#include <iostream>
#include <unordered_map>
#include <string>
#include <regex>
#include <cmath>
#include <mutex>
#include <cstdio>

// 구조체 정의
struct Point {
    float x;
    float y;
};

struct Line {
    float x1, y1, x2, y2;
};

// 설정 값
constexpr float direction_threshold = 0.6f; // cosine similarity threshold

const std::unordered_map<std::string, Line> rule_lines = {
    {"name1", {500, 100, 100, 500}},
    {"name2", {200, 250, 500, 600}}
};

// 전역 상태
std::mutex data_mutex;
std::unordered_map<int, Point> prev_vehicle_centers;

// 코사인 유사도 계산 함수
float compute_cosine_similarity(const Point& a, const Point& b) {
    float dot = a.x * b.x + a.y * b.y;
    float mag_a = std::sqrt(a.x * a.x + a.y * a.y);
    float mag_b = std::sqrt(b.x * b.x + b.y * b.y);
    if (mag_a == 0 || mag_b == 0) return 0.0f;
    return dot / (mag_a * mag_b);
}

// 차량 이동 여부 + 방향 정보 판단
bool is_any_vehicle_moving(const std::string& xml, const std::string& rule_name, std::string& direction_info) {
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
            Point move_vec = { cog.x - prev_it->second.x, cog.y - prev_it->second.y };
            float dist = std::sqrt(move_vec.x * move_vec.x + move_vec.y * move_vec.y);

            if (dist > 15.0f) {
                auto rule_it = rule_lines.find(rule_name);
                if (rule_it != rule_lines.end()) {
                    const Line& line = rule_it->second;
                    Point line_vec = { line.x2 - line.x1, line.y2 - line.y1 };
                    float cosine_sim = compute_cosine_similarity(move_vec, line_vec);

                    if (std::fabs(cosine_sim) >= direction_threshold) {
                        char sign = (cosine_sim >= 0) ? '+' : '-';
                        direction_info = "(Direction: ";
                        direction_info += sign;
                        direction_info += ", cosine: ";
                        direction_info += std::to_string(cosine_sim);
                        direction_info += ")";
                    }
                }
                prev_vehicle_centers = std::move(current_vehicle_centers);
                return true;
            }
        }
    }

    prev_vehicle_centers = std::move(current_vehicle_centers);
    return false;
}

// 사람 선 넘기 이벤트 확인
bool check_linecrossing_event(const std::string& xml, std::string& out_rule_name) {
    std::regex event_regex("<tt:Type(?: [^>]*)?>Human</tt:Type>[\\s\\S]*?<tt:SimpleItem Name=\"RuleName\" Value=\"([^\"]+)\"/>");
    std::smatch match;
    if (std::regex_search(xml, match, event_regex)) {
        out_rule_name = match[1];
        return true;
    }
    return false;
}

// 한 프레임의 이벤트 처리
void main_loop(const std::string& xml) {
    std::string rule_name;
    if (check_linecrossing_event(xml, rule_name)) {
        std::string direction_info;
        if (is_any_vehicle_moving(xml, rule_name, direction_info)) {
            std::cout << "[ALERT] Human crossed line '" << rule_name
                      << "' while vehicle was moving " << direction_info << std::endl;
            // 캡처 사진 저장 로직 추가
            // 캡처 사진 이름을 타임스탬프로 지정
        }
    }
}

// ffmpeg 메타데이터 처리 루프
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

        if (xml_buffer.find("</tt:Frame>") != std::string::npos) {
            main_loop(xml_buffer);
            xml_buffer.clear();
        }
    }

    pclose(pipe);
}

// 메인 함수
int main() {
    metadata_thread(); // 실시간 처리 시작
    return 0;
}
//경고 뜰때 타임스탬프랑 캡쳐
//캠쳐사진 이름을 타임스탬프로