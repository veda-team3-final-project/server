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
#include <curl/curl.h>
#include <sstream>

#include "SQLiteCpp/SQLiteCpp.h"

using namespace std;

// 구조체 정의
struct Point {
    float x;
    float y;
};

struct Line {
    float x1, y1, x2, y2;
    string mode;
    string name;
};


// 글로벌 변수로 선언 (서버에서 가져온 라인 정보)
unordered_map<string, Line> rule_lines;

// 전역 상태
mutex data_mutex;
unordered_map<int, Point> prev_vehicle_centers;

// 함수 전방 선언
bool parse_line_configuration(const string& json_response);

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


// HTTP 응답을 위한 콜백 함수
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// 간단한 JSON 파싱 함수들
string extract_json_string(const string& json, const string& key) {
    string search_pattern = "\"" + key + "\":\"";
    size_t start = json.find(search_pattern);
    if (start == string::npos) return "";
    
    start += search_pattern.length();
    size_t end = json.find("\"", start);
    if (end == string::npos) return "";
    
    return json.substr(start, end - start);
}

int extract_json_int(const string& json, const string& key) {
    string search_pattern = "\"" + key + "\":";
    size_t start = json.find(search_pattern);
    if (start == string::npos) return 0;
    
    start += search_pattern.length();
    size_t end = json.find_first_of(",}", start);
    if (end == string::npos) return 0;
    
    string value = json.substr(start, end - start);
    return stoi(value);
}

// 서버에서 라인 설정 가져오기
bool fetch_line_configuration() {
    CURL* curl;
    CURLcode res;
    string response_data;
    
    curl = curl_easy_init();
    if (!curl) {
        cerr << "[ERROR] CURL 초기화 실패" << endl;
        return false;
    }
    
    // URL 설정
    curl_easy_setopt(curl, CURLOPT_URL, "https://192.168.0.46/opensdk/WiseAI/configuration/linecrossing");
    
    // 응답 데이터 콜백
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    
    // Digest 인증
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
    curl_easy_setopt(curl, CURLOPT_USERPWD, "admin:admin123@");
    
    // SSL 검증 비활성화
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    // HTTP 헤더 설정
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Accept-Language: ko-KR,ko;q=0.9,en-US;q=0.8,en;q=0.7");
    headers = curl_slist_append(headers, "Connection: keep-alive");
    headers = curl_slist_append(headers, "Cookie: TRACKID=0842ca6f0d90294ea7de995c40a4aac6");
    headers = curl_slist_append(headers, "Referer: https://192.168.0.46/home/setup/opensdk/html/WiseAI/index.html");
    headers = curl_slist_append(headers, "Sec-Fetch-Dest: empty");
    headers = curl_slist_append(headers, "Sec-Fetch-Mode: cors");
    headers = curl_slist_append(headers, "Sec-Fetch-Site: same-origin");
    headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/138.0.0.0 Safari/537.36");
    headers = curl_slist_append(headers, "sec-ch-ua: \"Not)A;Brand\";v=\"8\", \"Chromium\";v=\"138\", \"Google Chrome\";v=\"138\"");
    headers = curl_slist_append(headers, "sec-ch-ua-mobile: ?0");
    headers = curl_slist_append(headers, "sec-ch-ua-platform: \"Windows\"");
    
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // 요청 실행
    res = curl_easy_perform(curl);
    
    // 정리
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        cerr << "[ERROR] HTTP 요청 실패: " << curl_easy_strerror(res) << endl;
        return false;
    }
    
    cout << "[INFO] 서버 응답: " << response_data << endl;
    
    // JSON 파싱하여 라인 정보 추출
    return parse_line_configuration(response_data);
}

// JSON 응답에서 라인 설정 파싱
bool parse_line_configuration(const string& json_response) {
    cout << "[DEBUG] JSON 응답 길이: " << json_response.length() << endl;
    
    // "line":[] 패턴 확인 (빈 라인 배열)
    if (json_response.find("\"line\":[]") != string::npos) {
        cout << "[INFO] 서버에 설정된 라인이 없습니다. 프로그램을 종료합니다." << endl;
        return false;
    }
    
    // 라인 정보 파싱
    rule_lines.clear();
    
    // "line":[{...}] 패턴에서 각 라인 객체 추출
    size_t line_start = json_response.find("\"line\":[");
    if (line_start == string::npos) {
        cerr << "[ERROR] 라인 정보를 찾을 수 없습니다." << endl;
        return false;
    }
    
    cout << "[DEBUG] line_start 위치: " << line_start << endl;
    
    line_start += 8; // "line":[ 길이
    
    // 중첩된 배열을 고려해서 올바른 라인 배열의 끝 찾기
    int bracket_count = 0;
    size_t line_end = line_start;
    for (size_t i = line_start; i < json_response.length(); i++) {
        if (json_response[i] == '[') bracket_count++;
        else if (json_response[i] == ']') {
            bracket_count--;
            if (bracket_count == -1) {  // line 배열의 끝
                line_end = i;
                break;
            }
        }
    }
    
    if (line_end == line_start) {
        cerr << "[ERROR] 라인 배열 종료를 찾을 수 없습니다." << endl;
        return false;
    }
    
    cout << "[DEBUG] line_end 위치: " << line_end << endl;
    
    string lines_section = json_response.substr(line_start, line_end - line_start);
    cout << "[DEBUG] lines_section: " << lines_section << endl;
    
    // 각 라인 객체 파싱 (간단한 방식)
    size_t pos = 0;
    int line_count = 0;
    while ((pos = lines_section.find("{", pos)) != string::npos) {
        line_count++;
        cout << "[DEBUG] 라인 객체 " << line_count << " 파싱 시작, pos: " << pos << endl;
        
        size_t obj_start = pos;
        
        // 중첩된 객체를 고려한 객체 끝 찾기
        int brace_count = 0;
        size_t obj_end = obj_start;
        for (size_t i = obj_start; i < lines_section.length(); i++) {
            if (lines_section[i] == '{') brace_count++;
            else if (lines_section[i] == '}') {
                brace_count--;
                if (brace_count == 0) {
                    obj_end = i;
                    break;
                }
            }
        }
        
        if (obj_end == obj_start) {
            cout << "[DEBUG] 객체 끝을 찾을 수 없음" << endl;
            break;
        }
        
        string line_obj = lines_section.substr(obj_start, obj_end - obj_start + 1);
        cout << "[DEBUG] 라인 객체: " << line_obj << endl;
        
        // name 추출
        string name = extract_json_string(line_obj, "name");
        cout << "[DEBUG] 추출된 name: '" << name << "'" << endl;
        
        // mode 추출
        string mode = extract_json_string(line_obj, "mode");
        cout << "[DEBUG] 추출된 mode: '" << mode << "'" << endl;
        
        // lineCoordinates 추출
        size_t coord_start = line_obj.find("\"lineCoordinates\":[");
        if (coord_start != string::npos) {
            cout << "[DEBUG] lineCoordinates 찾음" << endl;
            coord_start += 19; // "lineCoordinates":[ 길이
            size_t coord_end = line_obj.find("]", coord_start);
            if (coord_end != string::npos) {
                string coords = line_obj.substr(coord_start, coord_end - coord_start);
                cout << "[DEBUG] 좌표 문자열: " << coords << endl;
                
                // 첫 번째와 두 번째 좌표 추출
                size_t first_obj = coords.find("{");
                size_t second_obj = coords.find("{", first_obj + 1);
                
                if (first_obj != string::npos && second_obj != string::npos) {
                    size_t first_end = coords.find("}", first_obj);
                    size_t second_end = coords.find("}", second_obj);
                    
                    if (first_end != string::npos && second_end != string::npos) {
                        string first_coord = coords.substr(first_obj, first_end - first_obj + 1);
                        string second_coord = coords.substr(second_obj, second_end - second_obj + 1);
                        
                        cout << "[DEBUG] 첫 번째 좌표: " << first_coord << endl;
                        cout << "[DEBUG] 두 번째 좌표: " << second_coord << endl;
                        
                        float x1 = static_cast<float>(extract_json_int(first_coord, "x"));
                        float y1 = static_cast<float>(extract_json_int(first_coord, "y"));
                        float x2 = static_cast<float>(extract_json_int(second_coord, "x"));
                        float y2 = static_cast<float>(extract_json_int(second_coord, "y"));
                        
                        cout << "[DEBUG] 파싱된 좌표: (" << x1 << "," << y1 << ")-(" << x2 << "," << y2 << ")" << endl;
                        
                        // 라인 정보 저장
                        Line line = {x1, y1, x2, y2, mode, name};
                        rule_lines[name] = line;
                        
                        cout << "[INFO] 라인 추가: " << name 
                             << " (" << x1 << "," << y1 << ")-(" << x2 << "," << y2 << ") "
                             << "모드: " << mode << endl;
                    } else {
                        cout << "[DEBUG] 좌표 객체 끝을 찾을 수 없음" << endl;
                    }
                } else {
                    cout << "[DEBUG] 두 번째 좌표 객체를 찾을 수 없음" << endl;
                }
            } else {
                cout << "[DEBUG] lineCoordinates 배열 끝을 찾을 수 없음" << endl;
            }
        } else {
            cout << "[DEBUG] lineCoordinates를 찾을 수 없음" << endl;
        }
        
        pos = obj_end + 1;
    }
    
    cout << "[DEBUG] 총 처리된 라인 수: " << line_count << endl;
    cout << "[DEBUG] rule_lines 크기: " << rule_lines.size() << endl;
    
    if (rule_lines.empty()) {
        cout << "[INFO] 유효한 라인이 없습니다. 프로그램을 종료합니다." << endl;
        return false;
    }
    
    cout << "[INFO] 총 " << rule_lines.size() << "개의 라인을 로드했습니다." << endl;
    return true;
}

//======================================================================================

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

bool is_human(const string& xml, const string& object_id) {
    regex object_block_regex("<tt:Object ObjectId=\"" + object_id + "\"[\\s\\S]*?<tt:Type[^>]*>([^<]*)</tt:Type>");
    smatch match;
    if (regex_search(xml, match, object_block_regex)) {
        string type = match[1];
        cout << "[DEBUG] ObjectId=" << object_id << " 의 객체 타입: " << type << endl;
        return type == "Human";
    }
    cout << "[DEBUG] ObjectId=" << object_id << " 객체 정보를 Frame에서 찾지 못함" << endl;
    return false;
}

bool contains_frame_block(const string& xml) {
    return xml.find("<tt:VideoAnalytics>") != string::npos &&
           xml.find("<tt:Frame") != string::npos;
}

bool is_linecrossing_event(const string& xml, string& object_id, string& rule_name) {
    //cout << "[DEBUG] 이벤트 블럭 수신 (길이: " << xml.length() << ")" << endl;

    regex topic_regex("<wsnt:Topic[^>]*>([^<]*)</wsnt:Topic>");
    smatch topic_match;
    if (!regex_search(xml, topic_match, topic_regex)) {
        //cout << "[DEBUG] <wsnt:Topic> 추출 실패 → 이벤트 아님" << endl;
        return false;
    }

    string topic = topic_match[1];
    cout << "[DEBUG] 추출된 Topic: " << topic << endl;

    if (topic.find("LineCrossing") == string::npos)
        return false;

    // 각각 개별 추출
    smatch match;

    regex id_regex("<tt:SimpleItem Name=\"ObjectId\" Value=\"(\\d+)\"[^>]*/?>");
    regex name_regex("<tt:SimpleItem Name=\"RuleName\" Value=\"([^\"]+)\"[^>]*/?>");
    regex state_regex("<tt:SimpleItem Name=\"State\" Value=\"true\"[^>]*/?>");

    bool id_ok = regex_search(xml, match, id_regex);
    if (id_ok) object_id = match[1];

    bool name_ok = regex_search(xml, match, name_regex);
    if (name_ok) rule_name = match[1];

    bool state_ok = regex_search(xml, match, state_regex);

    if (id_ok && name_ok && state_ok) {
        cout << "[DEBUG] LineCrossing 이벤트 확인됨 → ObjectId=" << object_id
             << ", RuleName=" << rule_name << endl;
        return true;
    } else {
        cout << "[DEBUG] LineCrossing 이벤트이지만 매칭 실패 → "
             << "ObjectId:" << (id_ok ? "O" : "X") << ", "
             << "RuleName:" << (name_ok ? "O" : "X") << ", "
             << "State:true:" << (state_ok ? "O" : "X") << endl;
        return false;
    }
}


// 설정값
constexpr float direction_threshold = 0.65f; // 차량 방향 판단을 위한 코사인 유사도 임계값
constexpr float dist_threshold = 10.0f; // 차량 이동 판단을 위한 거리 임계값

// 차량 이동 경로 이력 저장 (최근 N프레임)
const int HISTORY_SIZE = 3;
unordered_map<int, deque<Point>> trajectory_history;

const int HUMAN_HISTORY_SIZE = 3;
unordered_map<int, deque<Point>> human_history;

unordered_map<int, Point> current_human_centers;  // 현재 프레임에 감지된 인간들

// 차량이 이동 중인지 판단하는 함수
bool is_any_vehicle_moving(const string& xml, const string& rule_name, string& direction_info) {
    lock_guard<mutex> lock(data_mutex);

    unordered_map<int, Point> current_vehicle_centers;
    bool frame_logged = false;

    // [1] 사람 위치 업데이트
    regex human_regex("<tt:Object ObjectId=\"(\\d+)\">[\\s\\S]*?<tt:Type[^>]*?>Human</tt:Type>[\\s\\S]*?<tt:CenterOfGravity x=\"([\\d.]+)\" y=\"([\\d.]+)\"");
    auto human_begin = sregex_iterator(xml.begin(), xml.end(), human_regex);
    for (auto it = human_begin; it != sregex_iterator(); ++it) {
        int human_id = stoi((*it)[1]);
        Point cog = {stof((*it)[2]), stof((*it)[3])};

        bool is_new = human_history.find(human_id) == human_history.end();
        bool is_updated = !is_new && (human_history[human_id].empty() || 
                            cog.x != human_history[human_id].back().x || 
                            cog.y != human_history[human_id].back().y);

        human_history[human_id].push_back(cog);
        if (human_history[human_id].size() > HUMAN_HISTORY_SIZE)
            human_history[human_id].pop_front();

        if (is_new || is_updated) {
            if (!frame_logged) {
                cout << "frame update!" << endl;
                frame_logged = true;
            }
            cout << "[Human] " << human_id << " {x=" << cog.x << ", y=" << cog.y << "}" << endl;
        }
    }

    // [2] 사람 이동 벡터 및 최근 위치 계산
    Point human_vec = {0, 0};
    Point human_pos = {0, 0};
    if (!human_history.empty()) {
        auto& history = human_history.begin()->second;
        if (history.size() >= 2) {
            float dx = 0.0f, dy = 0.0f;
            for (size_t i = 1; i < history.size(); ++i) {
                dx += (history[i].x - history[i - 1].x);
                dy += (history[i].y - history[i - 1].y);
            }
            size_t steps = history.size() - 1;
            human_vec = {dx / steps, dy / steps};
            human_pos = history.back();
        } else {
            cout << "[DEBUG] human_vec 추정 실패: 사람 위치 이력 부족 (size=" << history.size() << ")" << endl;
        }
    }

    // [3] 차량 위치 및 이동 판단
    regex vehicle_regex("<tt:Object ObjectId=\"(\\d+)\">[\\s\\S]*?<tt:Type[^>]*?>Vehicle</tt:Type>[\\s\\S]*?<tt:CenterOfGravity x=\"([\\d.]+)\" y=\"([\\d.]+)\"");
    auto begin = sregex_iterator(xml.begin(), xml.end(), vehicle_regex);

    for (auto it = begin; it != sregex_iterator(); ++it) {
        int vehicle_id = stoi((*it)[1]);
        Point cog = {stof((*it)[2]), stof((*it)[3])};

        bool is_new = trajectory_history.find(vehicle_id) == trajectory_history.end();
        bool is_updated = !is_new && (trajectory_history[vehicle_id].empty() || 
                            cog.x != trajectory_history[vehicle_id].back().x || 
                            cog.y != trajectory_history[vehicle_id].back().y);

        current_vehicle_centers[vehicle_id] = cog;
        trajectory_history[vehicle_id].push_back(cog);
        if (trajectory_history[vehicle_id].size() > HISTORY_SIZE)
            trajectory_history[vehicle_id].pop_front();

        if (is_new || is_updated) {
            if (!frame_logged) {
                cout << "frame update!" << endl;
                frame_logged = true;
            }
            cout << "[Vehicle] " << vehicle_id << " {x=" << cog.x << ", y=" << cog.y << "}" << endl;
        }

        if (trajectory_history[vehicle_id].size() >= 2 && human_history.size() > 0) {
            float sum_x = 0.0f, sum_y = 0.0f;
            for (size_t i = 0; i < trajectory_history[vehicle_id].size() - 1; ++i) {
                sum_x += trajectory_history[vehicle_id][i].x;
                sum_y += trajectory_history[vehicle_id][i].y;
            }
            size_t denom = trajectory_history[vehicle_id].size() - 1;
            Point avg_past = {sum_x / denom, sum_y / denom};
            Point vehicle_vec = {cog.x - avg_past.x, cog.y - avg_past.y};

            float dist = sqrt(vehicle_vec.x * vehicle_vec.x + vehicle_vec.y * vehicle_vec.y);
            if (dist <= dist_threshold) continue;

            // [4] 사람 기준으로 cross 계산
            Point vec_to_vehicle = {cog.x - human_pos.x, cog.y - human_pos.y};
            float cross = human_vec.x * vec_to_vehicle.y - human_vec.y * vec_to_vehicle.x;

            string side_info;
            if (cross > 0) side_info = " → 사람 기준 왼쪽";
            else if (cross < 0) side_info = " ← 사람 기준 오른쪽";
            else side_info = " (정면/후면)";

            float danger_dot = compute_cosine_similarity(vehicle_vec, human_vec);

            cout << "[DEBUG] 차량 ID=" << vehicle_id
                 << ", dot(vehicle,human)=" << danger_dot
                 << ", cross=" << cross << endl;

            if (fabs(danger_dot) < direction_threshold) {
                direction_info = "(차량 측면 접근 중)" + side_info;
                cout << "[ALERT] RuleID='" << rule_name << "', 차량 ID=" << vehicle_id
                     << "가 " << side_info << "에서 사람을 향해 접근 중입니다." << endl;
                prev_vehicle_centers = move(current_vehicle_centers);
                return true;
            } else {
                cout << "[DEBUG] 차량 이동 중이나 위험 조건 미충족 (|dot|=" << fabs(danger_dot)
                     << " ≥ threshold=" << direction_threshold << ")" << endl;
                direction_info = "(차량 이동 중이나 충돌 위험 낮음)" + side_info;
            }
        }
    }

    prev_vehicle_centers = move(current_vehicle_centers);
    return false;
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

        // 블럭 끝은 항상 </tt:MetadataStream> 으로 구분됨
        if (xml_buffer.find("</tt:MetadataStream>") != string::npos) {
            string block = xml_buffer;
            xml_buffer.clear();

            // [1] 라인크로싱 이벤트 블럭인지 확인
            string object_id, rule_name;
            if (is_linecrossing_event(block, object_id, rule_name)) {
                //cout << "[DEBUG] LineCrossing 이벤트 확인됨 → ObjectId=" << object_id << ", RuleName=" << rule_name << endl;
                //cout << "[DEBUG] LineCrossing 이벤트 감지됨: ObjectId=" << object_id << ", RuleName=" << rule_name << endl;

                if (contains_frame_block(block)) {
                    if (is_human(block, object_id)) {
                        //cout << "[DEBUG] ObjectId=" << object_id << " 의 객체 타입: Human" << endl;
                        cout << "[DEBUG] 해당 객체는 Human입니다." << endl;

                        string direction_info;
                        if (is_any_vehicle_moving(block, rule_name, direction_info)) {
                            string timestamp = extract_timestamp(block);
                            capture_and_store(timestamp);

                        } else {
                            cout << "[DEBUG] 이동 중인 차량 없음 → 캡처 및 저장 생략됨" << endl;
                        }
                    } else {
                        cout << "[DEBUG] 해당 객체는 Human이 아님 → 무시됨" << endl;
                    }
                } else {
                    cout << "[DEBUG] LineCrossing 이벤트이지만 매칭 실패 → ObjectId:" << (object_id.empty() ? "X" : "O")
                         << ", RuleName:" << (rule_name.empty() ? "X" : "O")
                         << ", State:true:" << (block.find("Value=\"true\"") != string::npos ? "O" : "X") << endl;
                }

            } else if (contains_frame_block(block)) {
                // [2] 일반 VideoAnalytics 프레임도 추적 처리
                string dummy;
                is_any_vehicle_moving(block, "", dummy);  // 추적만 수행, 라인 정보 없음
            }

            // 이벤트 로그 추출 여부
            regex topic_regex("<wsnt:Topic[^>]*>([^<]*)</wsnt:Topic>");
            smatch topic_match;
            if (regex_search(block, topic_match, topic_regex)) {
                cout << "[DEBUG] 추출된 Topic: " << topic_match[1] << endl;
            } else {
                //cout << "[DEBUG] <wsnt:Topic> 추출 실패 → 이벤트 아님" << endl;
            }
        }
    }

    pclose(pipe);
}



// 메인 진입점
int main() {
    // CURL 라이브러리 초기화
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    cout << "[INFO] 서버에서 라인 설정을 가져오는 중..." << endl;
    
    // 서버에서 라인 설정 가져오기
    if (!fetch_line_configuration()) {
        cout << "[ERROR] 라인 설정을 가져올 수 없어 프로그램을 종료합니다." << endl;
        curl_global_cleanup();
        return 1;
    }
    
    cout << "[INFO] 메타데이터 모니터링을 시작합니다..." << endl;
    metadata_thread();
    
    // CURL 라이브러리 정리
    curl_global_cleanup();
    return 0;
}

/*compile with:
g++ handler_server.cpp -o handler_server\
     -I/home/park/vcpkg/installed/arm64-linux/include\
     -L/home/park/vcpkg/installed/arm64-linux/lib\
     -lSQLiteCpp -lsqlite3 -lcurl -std=c++17
*/
