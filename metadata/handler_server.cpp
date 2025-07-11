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

#include <chrono>   
#include <ctime>    
#include <iomanip>

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

// 설정값
constexpr float direction_threshold = 0.45f; // 차량 방향 판단을 위한 코사인 유사도 임계값
constexpr float dist_threshold = 5.0f; // 차량 이동 판단을 위한 거리 임계값

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

                    float dot = compute_cosine_similarity(move_vec, line_vec);
                    float cross = line_vec.x * move_vec.y - line_vec.y * move_vec.x;

                    if (fabs(dot) > direction_threshold) {
                        cout << "[DEBUG] 측면 이동 판단: fabs(dot)=" << fabs(dot) << " > " << direction_threshold << ", 측면 이동으로 간주" << endl;
                        if (cross > 0) {
                            cout << "[DEBUG] cross > 0 → 차량이 라인 기준 왼쪽에서 오른쪽 방향으로 이동 중" << endl;
                            direction_info = "(측면 이동: ← 사람 기준 왼쪽에서 차량 등장)";
                        } else if (cross < 0) {
                            cout << "[DEBUG] cross < 0 → 차량이 라인 기준 오른쪽에서 왼쪽 방향으로 이동 중" << endl;
                            direction_info = "(측면 이동: → 사람 기준 오른쪽에서 차량 등장)";
                        } else {
                            cout << "[DEBUG] cross == 0 → 차량이 라인 벡터와 완전히 정렬됨" << endl;
                            direction_info = "(측면 이동: 정렬)";
                        }
                    }
                    else if (fabs(dot) < 0.3f) {
                        cout << "[DEBUG] 정면/등 뒤 이동 판단: fabs(dot)=" << fabs(dot) << " < 0.3 → 수직 이동으로 간주" << endl;
                        direction_info = "(정면 또는 등 뒤 이동)";
                    }
                    else {
                        cout << "[DEBUG] 사선 이동 판단: fabs(dot)=" << fabs(dot) << " → 방향성이 애매하므로 사선 이동으로 간주" << endl;
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

// UTC 타임스탬프 문자열을 KST 타임스탬프 문자열로 변환하는 함수
// 입력 형식: "YYYY-MM-DDTHH:MM:SS.sssZ" (UTC)
// 출력 형식: "YYYY-MM-DDTHH:MM:SS.sssKST" (KST)
string utcToKstString(const string& utc_timestamp_str) {
    // 1. UTC 문자열 파싱 (년, 월, 일, 시, 분, 초, 밀리초)
    int year, month, day, hour, minute, second, millisecond;

#if defined(_WIN32) || defined(_WIN64)
    // Windows (MSVC)용 sscanf_s
    int scan_count = sscanf_s(utc_timestamp_str.c_str(), "%d-%d-%dT%d:%d:%d.%dZ", 
                              &year, &month, &day, &hour, &minute, &second, &millisecond);
#else
    // POSIX/Linux/macOS용 sscanf
    int scan_count = sscanf(utc_timestamp_str.c_str(), "%d-%d-%dT%d:%d:%d.%dZ", 
                            &year, &month, &day, &hour, &minute, &second, &millisecond);
#endif

    if (scan_count < 7) { // 밀리초까지 모두 파싱되었는지 확인 (최소 7개 항목)
        cerr << "오류: 타임스탬프 문자열 형식 불일치 또는 파싱 실패: " << utc_timestamp_str << endl;
        return ""; // 빈 문자열 반환 또는 예외 발생
    }

    // 2. 파싱된 UTC 정보를 tm 구조체에 채우기
    tm tm_utc = {};
    tm_utc.tm_year = year - 1900; // tm_year는 1900년으로부터의 연도
    tm_utc.tm_mon = month - 1;   // tm_mon은 0(1월)부터 11(12월)
    tm_utc.tm_mday = day;
    tm_utc.tm_hour = hour;
    tm_utc.tm_min = minute;
    tm_utc.tm_sec = second;
    tm_utc.tm_isdst = 0; // UTC는 일광 절약 시간 적용 안 함

    // 3. tm (UTC)을 time_t (Unix timestamp)로 변환 (로컬 시간대 오프셋 보정)
    // mktime은 로컬 시간을 기준으로 time_t를 계산하므로,
    // UTC tm_struct를 time_t로 변환하려면 로컬 시간과 UTC 시간의 오프셋을 조정해야 합니다.
    time_t utc_time_c = mktime(&tm_utc); // 이 time_c는 로컬 시간대 기준 time_t
    
#if defined(_WIN32) || defined(_WIN64)
    utc_time_c -= _timezone; // Windows에서 UTC와의 차이 (초 단위)
#else
    utc_time_c -= timezone;  // POSIX에서 UTC와의 차이 (초 단위)
#endif


    // 4. time_t에 KST 오프셋(9시간) 더하기
    const long KST_OFFSET_SECONDS = 9 * 60 * 60; // 9시간을 초 단위로
    time_t kst_time_c = utc_time_c + KST_OFFSET_SECONDS;

    // 5. KST time_t를 tm 구조체로 변환
    // gmtime 함수는 항상 UTC를 기준으로 tm 구조체를 채웁니다.
    // kst_time_c는 이미 UTC+9이므로 gmtime을 사용하면 KST를 정확히 얻을 수 있습니다.
    tm *tm_kst_ptr = gmtime(&kst_time_c); 
    if (tm_kst_ptr == nullptr) {
        cerr << "오류: KST 시간 변환 실패." << endl;
        return "";
    }
    tm tm_kst = *tm_kst_ptr; // 포인터가 가리키는 값 복사 (정적 버퍼 사용 문제 우회)


    // 6. tm 구조체와 밀리초를 사용하여 최종 KST 문자열 포맷팅
    char buffer[100]; // 출력 버퍼
    // strftime을 사용하여 년월일 시분초 부분 포맷팅
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &tm_kst);

    // 밀리초 부분을 문자열에 추가 (stringstream 사용)
    stringstream ss;
    ss << buffer << "." << setfill('0') << setw(3) << millisecond << "KST";

    return ss.str();
}

// 이벤트 프레임 처리
void main_loop(const string& xml) {
    string rule_name;
    if (check_linecrossing_event(xml, rule_name)) {
        cout << "[DEBUG] Line crossing 감지됨, RuleName: " << rule_name << endl;

        string direction_info;
        if (is_any_vehicle_moving(xml, rule_name, direction_info)) {
            cout << "[DEBUG] 이동 중인 차량 감지됨, 방향 정보: " << direction_info << endl;

            string utcTimestamp = extract_timestamp(xml);

            string kstTimestamp = utcToKstString(utcTimestamp);
            cout << "[ALERT] Human crossed line '" << rule_name
                 << "' while vehicle was moving " << direction_info
                 << " [Time: " << kstTimestamp << "]" << endl;

            capture_and_store(kstTimestamp);
        } else {
            cout << "[DEBUG] 이동 중인 차량 없음 → 캡처 및 저장 생략됨" << endl;
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
g++ handler_server.cpp -o handler_server \
    -lSQLiteCpp -lsqlite3 -lcurl -std=c++17
*/
