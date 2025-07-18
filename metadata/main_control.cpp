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
#include <sstream>
#include <deque>
#include <limits>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include "board_control.h"

using namespace std;

// 구조체 정의
struct Point {
    float x;
    float y;
};

struct Line {
    Point start;
    Point end;
    string mode;
    string name;
};


// 보드-포트 매핑 테이블
std::vector<std::pair<int, std::string>> board_info = {
    {1, "/dev/ttyAMA0"},
    {2, "/dev/ttyAMA2"},
    {3, "/dev/ttyAMA1"},
    {4, "/dev/ttyAMA3"}
};

// 보드 ID → 포트 경로 조회 함수
std::string get_uart_port_for_board(int board_id) {
    for (const auto& [id, port] : board_info) {
        if (id == board_id) return port;
    }
    return "";
}

// --- 전역 상태 ---
recursive_mutex data_mutex;
const string RTSP_URL = "rtsp://admin:admin123@@192.168.0.137:554/0/onvif/profile2/media.smp";
const string DB_FILE = "../server_log.db";

// DB에서 로드될 좌표 및 설정값
vector<tuple<int, Point, int, Point>> base_line_pairs;
Point dot_center = {0, 0};
unordered_map<string, Line> rule_lines;

// 설정값
constexpr float dist_threshold = 10.0f;         // 차량 위치 업데이트 판단을 위한 거리 임계값
constexpr float parrallelism_threshold = 0.75f; // 차량 이동 방향과 라인 벡터가 평행한지 판단하는 코사인 유사도 임계값 (1에 가까울수록 평행)

// 프레임 캐시 설정
constexpr size_t FRAME_CACHE_SIZE = 15;
constexpr int HISTORY_SIZE = 10; // 이동 경로 저장 사이즈

// 객체 이동 경로를 저장하는 구조체
struct ObjectState {
    deque<Point> history;
};

// 차량 이동 경로 이력 저장
unordered_map<int, ObjectState> vehicle_trajectory_history;


// --- 함수 선언 ---
void analyze_risk_and_alert(SQLite::Database& db, int human_id, const string& rule_name, const string& utc_time_str);
float compute_cosine_similarity(const Point& a, const Point& b);
void capture_screen_and_save(SQLite::Database& db, const string& utc_time_str);
void control_board(int board_id, uint8_t cmd);


// --- 보드제어 관련 함수 ---

std::vector<uint8_t> encode_frame(uint8_t cmd, int board_id) {
    uint8_t dst_mask = (1 << (board_id - 1));

    std::vector<uint8_t> payload = {dst_mask, cmd};

    std::vector<uint8_t> frame = {DLE, STX};
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


// 보드 제어 함수
void control_board(int board_id, uint8_t cmd) {
    std::string port = get_uart_port_for_board(board_id);
    if (port.empty()) {
        cerr << "[ERROR] Unknown board ID: " << board_id << endl;
        return;
    }

    BoardController controller(port, board_id);
    bool ok = false;
    if (cmd == CMD_LCD_ON) {
        ok = controller.send_lcd_on_with_ack(3, 500); // 3회 재시도, 500ms 타임아웃
    } else if (cmd == CMD_LCD_OFF) {
        ok = controller.send_lcd_off_with_ack(3, 500);
    }

    if (ok) {
        cout << "[INFO] Command 0x" << hex << int(cmd) << " succeeded for board " << board_id << endl;
    } else {
        cerr << "[ERROR] Command 0x" << hex << int(cmd) << " failed for board " << board_id << endl;
    }
}



// --- DB 관련 함수 ---

// Detections 테이블 생성
void create_detections_table(SQLite::Database& db) {
    db.exec("CREATE TABLE IF NOT EXISTS detections (" 
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "image BLOB, "
            "timestamp DATETIME NOT NULL)");
    cout << "[INFO] 'detections' table is ready." << endl;
}

// DB에 이미지 삽입
void insert_data(SQLite::Database& db, const vector<unsigned char>& image_data, const string& timestamp) {
    try {
        SQLite::Statement query(db, "INSERT INTO detections (image, timestamp) VALUES (?, ?)");
        query.bind(1, image_data.data(), image_data.size());
        query.bind(2, timestamp);
        query.exec();
        cout << "[INFO] Image data inserted to DB with timestamp: " << timestamp << endl;
    } catch (const exception& e) {
        cerr << "[ERROR] Failed to insert data into DB: " << e.what() << endl;
    }
}

// 점과 점으로 이루어진 두 직선의 교차점 구하는 함수, dot_center 구하기
bool calculate_intersection(const Point& a1, const Point& a2,
                            const Point& b1, const Point& b2,
                            Point& intersection) {
    float A1 = a2.y - a1.y;
    float B1 = a1.x - a2.x;
    float C1 = A1 * a1.x + B1 * a1.y;

    float A2 = b2.y - b1.y;
    float B2 = b1.x - b2.x;
    float C2 = A2 * b1.x + B2 * b1.y;

    float det = A1 * B2 - A2 * B1;
    if (fabs(det) < 1e-5) return false; // 평행

    intersection.x = (B2 * C1 - B1 * C2) / det;
    intersection.y = (A1 * C2 - A2 * C1) / det;
    return true;
}

// DB에서 Dots(보조선) 좌표 로드 및 dot_center 계산
void load_dots_and_center(SQLite::Database& db) {
    cout << "[INFO] Loading baseLines from DB..." << endl;

    const float scale_x = 3840.0f / 960.0f;
    const float scale_y = 2160.0f / 540.0f;
    
    //const float scale_x = 1;
    //const float scale_y = 1;
    base_line_pairs.clear();

    try {
        SQLite::Statement query(db, "SELECT matrixNum1, x1, y1, matrixNum2, x2, y2 FROM baseLines");

        while (query.executeStep()) {
            int id1 = query.getColumn(0).getInt();
            Point p1 = {query.getColumn(1).getInt() * scale_x, query.getColumn(2).getInt() * scale_y};
            int id2 = query.getColumn(3).getInt();
            Point p2 = {query.getColumn(4).getInt() * scale_x, query.getColumn(5).getInt() * scale_y};

            base_line_pairs.emplace_back(id1, p1, id2, p2);

            cout << "[DEBUG] Loaded dot pair: " << id1 << "<->" << id2
                 << " (" << p1.x << "," << p1.y << ") <-> (" << p2.x << "," << p2.y << ")" << endl;
        }

        // dot_center 계산
        if (base_line_pairs.size() >= 2) {
            Point inter;
            bool found = calculate_intersection(
                get<1>(base_line_pairs[0]), get<3>(base_line_pairs[0]),
                get<1>(base_line_pairs[1]), get<3>(base_line_pairs[1]),
                inter
            );
            if (found) {
                dot_center = inter;
            } else {
                dot_center = {(get<1>(base_line_pairs[0]).x + get<3>(base_line_pairs[0]).x) / 2,
                              (get<1>(base_line_pairs[0]).y + get<3>(base_line_pairs[0]).y) / 2};
            }
        } else if (base_line_pairs.size() == 1) {
            dot_center = {
                (get<1>(base_line_pairs[0]).x + get<3>(base_line_pairs[0]).x) / 2,
                (get<1>(base_line_pairs[0]).y + get<3>(base_line_pairs[0]).y) / 2
            };
        } else {
            cerr << "[WARNING] No baseLines found. dot_center = (0, 0)" << endl;
        }

        cout << "[INFO] Calculated dot_center: (" << dot_center.x << ", " << dot_center.y << ")" << endl;

    } catch (const exception& e) {
        cerr << "[ERROR] Failed to load baseLines: " << e.what() << endl;
    }
}


// DB에서 Rule Lines(가상선) 정보 로드
void load_rule_lines(SQLite::Database& db) {
    cout << "[INFO] Loading rule lines from DB..." << endl;

    const float scale_x = 3840.0f / 960.0f;
    const float scale_y = 2160.0f / 540.0f;

    try {
        SQLite::Statement query(db, "SELECT x1, y1, x2, y2, name, mode FROM lines LIMIT 8");

        while (query.executeStep()) {
            Line line;
            line.start = { query.getColumn(0).getInt() * scale_x, query.getColumn(1).getInt() * scale_y };
            line.end   = { query.getColumn(2).getInt() * scale_x, query.getColumn(3).getInt() * scale_y };
            line.name = query.getColumn(4).getString();
            line.mode = query.getColumn(5).getString();

            rule_lines[line.name] = line;

            cout << "[DEBUG] Loaded line (scaled): " << line.name
                 << " [(" << line.start.x << "," << line.start.y << ") -> ("
                 << line.end.x << "," << line.end.y << ")] Mode: " << line.mode << endl;
        }

        if (rule_lines.empty()) {
            cerr << "[WARNING] No rule lines found in DB. 'rule_lines' is empty." << endl;
        }

    } catch (const std::exception& e) {
        cerr << "[ERROR] Failed to load from 'lines' table: " << e.what() << endl;
    }
}


// --- 핵심 로직 함수 ---

// 화면을 캡처하고 DB에 저장
void capture_screen_and_save(SQLite::Database& db, const string& utc_time_str) {
    if (utc_time_str.empty()) {
        cerr << "[ERROR] Cannot capture screen. UTC time is empty." << endl;
        return;
    }

    // 1. UTC 시간 문자열 파싱 및 KST로 변환
    std::tm tm = {};
    std::stringstream ss(utc_time_str);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) {
        cerr << "[ERROR] Failed to parse UTC time string: " << utc_time_str << endl;
        return;
    }

    time_t time_utc = timegm(&tm);
    if (time_utc == -1) {
        cerr << "[ERROR] Failed to convert UTC tm to time_t." << endl;
        return;
    }

    time_t time_kst = time_utc + (9 * 3600);
    char kst_buffer[100];
    struct tm kst_tm;
    gmtime_r(&time_kst, &kst_tm);
    std::strftime(kst_buffer, sizeof(kst_buffer), "%Y-%m-%dT%H:%M:%SKST", &kst_tm);
    string kst_timestamp_str(kst_buffer);

    // 2. ffmpeg 캡처 명령어 생성 (이미지를 stdout으로 출력)
    string cmd = "ffmpeg -i " + RTSP_URL + " -vframes 1 -c:v mjpeg -f image2pipe - 2>/dev/null";
    
    cout << "[INFO] Capturing screen to save into database..." << endl;

    // 3. ffmpeg 실행 및 이미지 데이터 읽기
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        cerr << "[ERROR] Failed to open ffmpeg pipe for capture." << endl;
        return;
    }

    vector<unsigned char> image_data;
    char read_buffer[4096];
    size_t bytes_read;
    while ((bytes_read = fread(read_buffer, 1, sizeof(read_buffer), pipe)) > 0) {
        image_data.insert(image_data.end(), read_buffer, read_buffer + bytes_read);
    }
    pclose(pipe);

    // 4. DB에 데이터 삽입
    if (!image_data.empty()) {
        insert_data(db, image_data, kst_timestamp_str);
    } else {
        cerr << "[ERROR] Failed to read image data from ffmpeg pipe." << endl;
    }
}


// 블럭 내에 VideoAnalytics 프레임이 포함되어 있는지 확인
bool contains_frame_block(const string& block) {
    return block.find("<tt:VideoAnalytics>") != string::npos &&
           block.find("<tt:Frame") != string::npos;
}

// 라인크로싱 이벤트 블럭인지 확인하고 ObjectId와 RuleName을 추출
bool is_linecrossing_event(const string& event_block, string& object_id, string& rule_name) {
    regex topic_regex("<wsnt:Topic[^>]*>([^<]*)</wsnt:Topic>");
    smatch topic_match;
    if (!regex_search(event_block, topic_match, topic_regex) || string(topic_match[1]).find("LineCrossing") == string::npos) {
        return false;
    }

    regex id_regex("<tt:SimpleItem Name=\"ObjectId\" Value=\"(\\d+)\"/>");
    regex rule_regex("<tt:SimpleItem Name=\"RuleName\" Value=\"([^\"]+)\"/>");
    regex state_regex("<tt:SimpleItem Name=\"State\" Value=\"true\"");

    smatch match;
    bool id_ok = regex_search(event_block, match, id_regex);
    if (id_ok) object_id = match[1];

    bool rule_ok = regex_search(event_block, match, rule_regex);
    if (rule_ok) rule_name = match[1];

    bool state_ok = regex_search(event_block, match, state_regex);

    if (id_ok && rule_ok && state_ok) {
        cout << "[DEBUG] LineCrossing Event: ObjectId=" << object_id << ", RuleName=" << rule_name << endl;
        return true;
    }
    return false;
}

// ObjectId로 객체 타입이 Human인지 확인
bool is_human(const string& block, const string& object_id) {
    regex object_block_regex("<tt:Object ObjectId=\"" + object_id + "\"[\\s\\S]*?<tt:Type[^>]*>([^<]*)</tt:Type>");
    smatch match;
    if (regex_search(block, match, object_block_regex)) {
        string type = match[1];
        return type == "Human";
    }
    return false;
}

// 프레임에서 차량 위치 업데이트
void update_vehicle_positions(const string& frame_block, const deque<string>& frame_cache) {
    //cout << "[DEBUG] Entering update_vehicle_positions()" << endl;

    unordered_map<int, bool> seen_vehicles;

    // [1] Object 블럭 단위로 분리
    regex object_block_regex("<tt:Object ObjectId=\"(\\d+)\">([\\s\\S]*?)</tt:Object>");
    auto block_begin = sregex_iterator(frame_block.begin(), frame_block.end(), object_block_regex);
    auto block_end = sregex_iterator();

    for (auto it = block_begin; it != block_end; ++it) {
        int id = stoi((*it)[1]);
        string object_content = (*it)[2].str();

        // [2] 타입 추출
        smatch type_match;
        if (!regex_search(object_content, type_match, regex("<tt:Type[^>]*>([^<]*)</tt:Type>")))
            continue;

        string type = type_match[1];
        if (type != "Vehicle" && type != "Vehical")
            continue;

        // [3] CenterOfGravity 추출
        smatch cog_match;
        if (!regex_search(object_content, cog_match, regex("<tt:CenterOfGravity[^>]*x=\"([\\d.]+)\" y=\"([\\d.]+)\"")))
            continue;

        Point cog = {stof(cog_match[1]), stof(cog_match[2])};

        cout << "[DEBUG] Tracking Vehicle " << id << " at (" << cog.x << ", " << cog.y << ")" << endl;

        seen_vehicles[id] = true;
        auto& state = vehicle_trajectory_history[id];
        state.history.push_back(cog);
        if (state.history.size() > HISTORY_SIZE) {
            state.history.pop_front();
        }
    }

    // [4] 캐시를 기반으로 사라진 차량 객체 정리
    for (auto it = vehicle_trajectory_history.begin(); it != vehicle_trajectory_history.end(); ) {
        if (seen_vehicles.find(it->first) == seen_vehicles.end()) {
            bool found_in_cache = false;
            for (const auto& cached_frame : frame_cache) {
                if (cached_frame.find("<tt:Object ObjectId=\"" + to_string(it->first) + "\"") != string::npos) {
                    found_in_cache = true;
                    break;
                }
            }

            if (!found_in_cache) {
                cout << "[DEBUG] Vehicle " << it->first << " disappeared from all cache frames. Erasing." << endl;
                it = vehicle_trajectory_history.erase(it);
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }

    //cout << "[DEBUG] Exiting update_vehicle_positions()" << endl;
}

// 위험 분석 및 경고 로직
void analyze_risk_and_alert(SQLite::Database& db, int human_id, const string& rule_name, const string& utc_time_str) {
    lock_guard<recursive_mutex> lock(data_mutex);

    cout << "[DEBUG] Analyzing risk for human_id: " << human_id << " crossing line: " << rule_name << endl;

    // 1. 이벤트 라인 정보 확인
    if (rule_lines.find(rule_name) == rule_lines.end()) {
        cout << "[DEBUG] Step Failed: RuleName '" << rule_name << "' not found in predefined lines." << endl;
        return;
    }

    Line crossed_line = rule_lines.at(rule_name);
    Point line_vector = {
        crossed_line.end.x - crossed_line.start.x,
        crossed_line.end.y - crossed_line.start.y
    };

    // 2. 차량 이력 존재 확인
    cout << "[DEBUG] Vehicle history size: " << vehicle_trajectory_history.size() << endl;
    if (vehicle_trajectory_history.empty()) {
        cout << "[DEBUG] Step Failed: No vehicles detected to analyze." << endl;
        return;
    }

    // 3. 각 차량 반복
    for (const auto& [vehicle_id, vehicle_state] : vehicle_trajectory_history) {
        cout << "[DEBUG] Vehicle " << vehicle_id << " history size: " << vehicle_state.history.size() << endl;

        if (vehicle_state.history.size() < 2) {
            cout << "[DEBUG] Vehicle " << vehicle_id << ": Insufficient history. Skipping." << endl;
            continue;
        }

        // 가장 오래된 위치와 최근 위치 추출
        const Point& oldest_pos = vehicle_state.history.front();
        const Point& newest_pos = vehicle_state.history.back();

        // 4. 가장 가까운 dot 쌍 탐색 (양쪽 모두 검사)
        Point closest_dot;
        int matched_id = -1;    // 차량과 가장 가까운 dot의 ID
        int board_id = -1;      // 켜야 하는 dot의 id, 차량과 가까운 dot의 반대쪽 dot
        float min_dist_sq = numeric_limits<float>::max();

        for (const auto& [id1, p1, id2, p2] : base_line_pairs) {
            float d1 = pow(oldest_pos.x - p1.x, 2) + pow(oldest_pos.y - p1.y, 2);
            float d2 = pow(oldest_pos.x - p2.x, 2) + pow(oldest_pos.y - p2.y, 2);

            if (d1 < min_dist_sq) {
                min_dist_sq = d1;
                closest_dot = p1;
                matched_id = id1;
                board_id = id2;
            }

            if (d2 < min_dist_sq) {
                min_dist_sq = d2;
                closest_dot = p2;
                matched_id = id2;
                board_id = id1;
            }
        }

        cout << "[DEBUG] Vehicle " << vehicle_id << ": Closest dot = (" << closest_dot.x << "," << closest_dot.y << ")" << endl;
        cout << "[DEBUG] Oldest Pos = (" << oldest_pos.x << "," << oldest_pos.y << "), Newest Pos = (" << newest_pos.x << "," << newest_pos.y << ")" << endl;

        // 5. dot_center 접근 여부 확인
        float dist_old = hypot(oldest_pos.x - dot_center.x, oldest_pos.y - dot_center.y);
        float dist_new = hypot(newest_pos.x - dot_center.x, newest_pos.y - dot_center.y);

        cout << "[DEBUG] Vehicle " << vehicle_id << ": Old dist to dot_center = " << dist_old << ", New dist = " << dist_new << endl;

        if (dist_new > dist_old - dist_threshold) {
            cout << "[DEBUG] Step Failed (Vehicle " << vehicle_id << "): Not approaching dot_center enough. Threshold = " << dist_threshold << endl;
            continue;
        }

        // 6. 벡터 유사도 분석
        Point vehicle_vector = {dot_center.x - closest_dot.x, dot_center.y - closest_dot.y};
        float similarity = compute_cosine_similarity(vehicle_vector, line_vector);

        cout << "[DEBUG] Vehicle " << vehicle_id << ": Cosine similarity = " << similarity << ", Threshold = " << parrallelism_threshold << endl;

        if (abs(similarity) >= parrallelism_threshold) {
            cout << "\n[ALERT] " << vehicle_id << " 차량이 " << human_id << " 인간을 향해 측면에서 접근 중입니다." << endl;
            cout << board_id << " dot matrix를 가동합니다." << endl;
            cout << "(코사인 유사도 : " << similarity << ")\n" << endl;
            
            // 7. 보드 제어
            control_board(board_id, 0x01); // 0x01 명령어   
            sleep(5);
            control_board(board_id, 0x02); // 0x02 명령어

            // 화면 캡처 및 DB 저장
            capture_screen_and_save(db, utc_time_str);
        } else {
            cout << "[DEBUG] Step Failed (Vehicle " << vehicle_id << "): Cosine similarity not high enough." << endl;
        }
    }
}



// 코사인 유사도 계산
float compute_cosine_similarity(const Point& a, const Point& b) {
    float dot = a.x * b.x + a.y * b.y;
    float mag_a = sqrt(a.x * a.x + a.y * a.y);
    float mag_b = sqrt(b.x * b.x + b.y * b.y);
    if (mag_a == 0 || mag_b == 0) return -2.0f; // 불가능한 값 리턴
    return dot / (mag_a * mag_b);
}


// ffmpeg 메타데이터 처리 루프
void metadata_thread(SQLite::Database& db) {
    cout << "[INFO] FFmpeg 메타데이터 스트림을 시작합니다..." << endl;

    const string cmd = "ffmpeg -i " + RTSP_URL + " -map 0:1 -f data - 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        cerr << "[ERROR] Failed to open ffmpeg pipe" << endl;
        return;
    }

    constexpr int BUFFER_SIZE = 8192;
    char buffer[BUFFER_SIZE];
    string xml_buffer;

    deque<string> frame_cache;

    while (fgets(buffer, BUFFER_SIZE, pipe)) {
        xml_buffer += buffer;

        // XML 블럭 완성 여부 확인
        if (xml_buffer.find("</tt:MetadataStream>") != string::npos) {
            string block = xml_buffer;
            xml_buffer.clear();

            {
                lock_guard<recursive_mutex> lock(data_mutex);

                // Frame block 처리
                if (contains_frame_block(block)) {
                    frame_cache.push_back(block);
                    if (frame_cache.size() > FRAME_CACHE_SIZE) {
                        frame_cache.pop_front();
                    }
                    update_vehicle_positions(block, frame_cache);
                }

                // Event block 처리
                string object_id, rule_name;
                if (is_linecrossing_event(block, object_id, rule_name)) {
                    bool human_found = false;
                    string utc_time_str; // 프레임의 UTC 시간을 저장할 변수

                    for (auto it = frame_cache.rbegin(); it != frame_cache.rend(); ++it) {
                        if (is_human(*it, object_id)) {
                            human_found = true;
                            
                            // Human이 발견된 프레임에서 UTC 시간 추출
                            regex utc_regex("<tt:Frame[^>]*UtcTime=\"([^\"]+)\"[^>]*>");
                            smatch utc_match;
                            if (regex_search(*it, utc_match, utc_regex) && utc_match.size() > 1) {
                                utc_time_str = utc_match[1].str();
                            }
                            break;
                        }
                    }

                    if (human_found) {
                        //cout << "[DEBUG] Human " << object_id << " crossed a line. Triggering analysis." << endl;
                        // 분석 함수에 DB 객체와 UTC 시간 전달
                        analyze_risk_and_alert(db, stoi(object_id), rule_name, utc_time_str);
                    } else {
                        //cout << "[DEBUG] A line was crossed by object " << object_id << ", but it was not identified as a human in recent frames." << endl;
                    }
                }
            }
        }
    }

    pclose(pipe);
    cout << "[INFO] 메타데이터 스트림이 종료되었습니다." << endl;
}


// --- 메인 진입점 ---
int main() {
    cout << "[INFO] 메타데이터 모니터링을 시작합니다..." << endl;
    try {
        // DB 파일 열기
        SQLite::Database db(DB_FILE, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        
        // DB 테이블 생성 (없으면)
        create_detections_table(db);

        // DB에서 설정값 로드
        load_dots_and_center(db);
        load_rule_lines(db);

        // 설정값 로드 확인
        if (base_line_pairs.empty() || rule_lines.empty()) {
            cerr << "[FATAL] Failed to load configuration from database. Check 'baseLines' and 'lines' tables." << endl;
            return 1;
        }

        // 메타데이터 처리 스레드 시작 (DB 객체 전달)
        metadata_thread(db);

    } catch (const std::exception& e) {
        cerr << "[FATAL] SQLite exception: " << e.what() << endl;
        return 1;
    }
    cout << "[INFO] 메타데이터 모니터링을 종료합니다." << endl;
    return 0;
}


/*compile with:
 g++ main_control.cpp board_control.cpp -o control -lSQLiteCpp -lsqlite3 --std=c++17   
*/
