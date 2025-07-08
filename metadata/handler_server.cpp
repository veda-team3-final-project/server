#include <iostream>
#include <unordered_map>
#include <string>
#include <regex>
#include <cmath>
#include <mutex>
#include <cstdio>
#include <fstream>
#include <vector>
#include <algorithm>

#include "SQLiteCpp/SQLiteCpp.h"

using namespace std;

// 구조체 정의
struct Point {
    float x;
    float y;
};

struct Line {
    float x1, y1, x2, y2;
};

// 설정값
constexpr float direction_threshold = 0.6f; // 차량 방향 판단을 위한 코사인 유사도 임계값
constexpr float dist_threshold = 10.0f; // 차량 이동 판단을 위한 거리 임계값
// 임의의 라인 정의
const unordered_map<string, Line> rule_lines = {
    {"name1", {500, 100, 100, 500}},
    {"name2", {200, 250, 500, 600}}
};

// 전역 상태
mutex data_mutex;
unordered_map<int, Point> prev_vehicle_centers;

// 코사인 유사도 계산
float compute_cosine_similarity(const Point& a, const Point& b) {
    float dot = a.x * b.x + a.y * b.y;
    float mag_a = sqrt(a.x * a.x + a.y * a.y);
    float mag_b = sqrt(b.x * b.x + b.y * b.y);
    if (mag_a == 0 || mag_b == 0) return 0.0f;
    return dot / (mag_a * mag_b);
}

// 타임스탬프 추출
string extract_timestamp(const string& xml) {
    regex time_regex("UtcTime=\"([^\"]+)\"");
    smatch match;
    if (regex_search(xml, match, time_regex)) {
        return match[1];
    }
    return "unknown_time";
}

// DB 테이블 생성
void create_table(SQLite::Database& db) {
    db.exec("CREATE TABLE IF NOT EXISTS detections ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "image BLOB, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP NOT NULL)");
    cout << "'detections' 테이블이 준비되었습니다.\n";
}

// DB에 이미지 삽입
void insert_data(SQLite::Database& db, vector<unsigned char> image, string timestamp) {
    try {
        SQLite::Statement query(db, "INSERT INTO detections (image, timestamp) VALUES (?, ?)");
        query.bind(1, image.data(), image.size());
        query.bind(2, timestamp);
        query.exec();
        cout << "데이터 추가: (시간: " << timestamp << ")" << endl;
    } catch (const exception& e) {
        cerr << "데이터 저장 실패: " << e.what() << endl;
    }
}

// 캡처 및 DB 저장
void capture_and_store(const string& timestamp) {
    string safe_time = timestamp;
    replace(safe_time.begin(), safe_time.end(), ':', '-');
    string filename = safe_time + ".jpg";

    string cmd = "ffmpeg -y -rtsp_transport tcp -i rtsp://admin:admin123@192.168.0.46:554/0/onvif/profile2/media.smp "
                 "-frames:v 1 -q:v 2 -update true " + filename + " > /dev/null 2>&1";
    system(cmd.c_str());

    ifstream file(filename, ios::binary);
    if (!file) {
        cerr << "[ERROR] 캡처 이미지 파일 열기 실패: " << filename << endl;
        return;
    }

    vector<unsigned char> buffer((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
    file.close();
    // DB 저장 성공 후
    remove(filename.c_str());

    try {
        SQLite::Database db("../server_log.db",SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        create_table(db);
        insert_data(db, buffer, timestamp);
    } catch (const exception& e) {
        cerr << "[ERROR] DB 처리 실패: " << e.what() << endl;
    }
}

// 차량 이동 감지 + 방향 판단
bool is_any_vehicle_moving(const string& xml, const string& rule_name, string& direction_info) {
    lock_guard<mutex> lock(data_mutex);
    unordered_map<int, Point> current_vehicle_centers;

    regex vehicle_regex("<tt:Object ObjectId=\"(\\d+)\">[\\s\\S]*?<tt:Type[^>]*?>Vehicle</tt:Type>[\\s\\S]*?<tt:CenterOfGravity x=\"([\\d.]+)\" y=\"([\\d.]+)\"");
    auto begin = sregex_iterator(xml.begin(), xml.end(), vehicle_regex);
    auto end = sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        int object_id = stoi((*it)[1]);
        Point cog = {stof((*it)[2]), stof((*it)[3])};
        current_vehicle_centers[object_id] = cog;

        auto prev_it = prev_vehicle_centers.find(object_id);
        if (prev_it != prev_vehicle_centers.end()) {
            Point move_vec = {cog.x - prev_it->second.x, cog.y - prev_it->second.y};
            float dist = sqrt(move_vec.x * move_vec.x + move_vec.y * move_vec.y);

            if (dist > dist_threshold) {
                auto rule_it = rule_lines.find(rule_name);
                if (rule_it != rule_lines.end()) {
                    const Line& line = rule_it->second;
                    Point line_vec = {line.x2 - line.x1, line.y2 - line.y1};

                    // 내적 기반 cosine similarity
                    float dot = compute_cosine_similarity(move_vec, line_vec);

                    // 외적 값 계산
                    float cross = line_vec.x * move_vec.y - line_vec.y * move_vec.x;

                    if (fabs(dot) > direction_threshold) {
                        // 측면 방향 (라인 방향 ≈ 사람 기준 좌우 방향)
                        if (cross > 0)
                            direction_info = "(측면 이동: ← 사람 기준 왼쪽)";
                        else if (cross < 0)
                            direction_info = "(측면 이동: → 사람 기준 오른쪽)";
                        else
                            direction_info = "(측면 이동: 정렬)";
                    }
                    else if (fabs(dot) < 0.3f) {
                        // 정면 또는 등 뒤 방향
                        direction_info = "(정면 또는 등 뒤 이동)"; 
                    }
                    else {
                        direction_info = "(사선 이동, 차량 이동 방향 불명확)";
                    }
                }
                prev_vehicle_centers = move(current_vehicle_centers);
                return true;
            }
        } else {
            direction_info = "(새로운 차량 발견, 이동 없음)";
        }
    }

    prev_vehicle_centers = move(current_vehicle_centers);
    return false;
}

// 사람 선 넘기 이벤트 확인
bool check_linecrossing_event(const string& xml, string& out_rule_name) {
    regex event_regex("<tt:Type[^>]*?>Human</tt:Type>[\\s\\S]*?<tt:SimpleItem Name=\"RuleName\" Value=\"([^\"]+)\"/>");
    smatch match;
    if (regex_search(xml, match, event_regex)) {
        out_rule_name = match[1];
        return true;
    }
    return false;
}

// 이벤트 프레임 처리
void main_loop(const string& xml) {
    string rule_name;
    if (check_linecrossing_event(xml, rule_name)) {
        string direction_info;
        if (is_any_vehicle_moving(xml, rule_name, direction_info)) {
            string timestamp = extract_timestamp(xml);
            cout << "[ALERT] Human crossed line '" << rule_name
                 << "' while vehicle was moving " << direction_info
                 << " [Time: " << timestamp << "]" << endl;

            capture_and_store(timestamp);
        }
    }
}

// ffmpeg 메타데이터 처리 루프
void metadata_thread() {
    const string cmd =
        "ffmpeg -i rtsp://admin:admin123@192.168.0.46:554/0/onvif/profile2/media.smp "
        "-map 0:1 -f data - 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        cerr << "[ERROR] Failed to open ffmpeg pipe" << endl;
        return;
    }

    constexpr int BUFFER_SIZE = 8192;
    char buffer[BUFFER_SIZE];
    string xml_buffer;

    while (fgets(buffer, BUFFER_SIZE, pipe)) {
        xml_buffer += buffer;
        if (xml_buffer.find("</tt:Frame>") != string::npos) {
            main_loop(xml_buffer);
            xml_buffer.clear();
        }
    }

    pclose(pipe);
}

// 메인 진입점
int main() {
    metadata_thread();
    return 0;
}

/*compile with:
g++ handler_server.cpp -o handler_server \
    -I/home/park/vcpkg/installed/arm64-linux/include \
    -L/home/park/vcpkg/installed/arm64-linux/lib \
    -lSQLiteCpp -lsqlite3 -std=c++17
*/
