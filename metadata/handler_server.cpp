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
string extract_timestamp(const string& block) {
    regex time_regex("UtcTime=\"([^\"]+)\"");
    smatch match;
    if (regex_search(block, match, time_regex)) {
        return match[1];
    }
    return "unknown_time";
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

/*======================================================================================

아래부터는 캡처 및 DB 저장 관련 코드입니다.
탐지로직은 다음과 같습니다.
일정시간마다(대략 100~200ms) RTSP 스트림에서 메타데이터를 읽어오고,
라인크로싱 이벤트가 발생하면 해당 객체의 ObjectId를 추출합니다.
이 ObjectId를 통해 VideoAnalytics 프레임에서 해당 객체가 인간인지 확인합니다.
인간이 맞다면, 해당 객체의 이동 벡터를 추정하고,
frame 내에 이동 중인 차량이 있는지 판단합니다.

frame 내에 이동 중인 차량이 있다면, 모든 이동중인 차량을 대상으로 인간과 dot product 계산을 실시합니다.
계산은 다음과 같습니다.
1. 차량의 이동벡터와 인간의 이동벡터의 dot product
이를 통해 차량이 인간의 측면방향으로 이동하고 있는지 계산합니다. 결과의 절댓값이 0.5 ~ 0.6 정도의 threshold보다 낮은 값이 나온다면 2번 계산으로 넘어갑니다
2. 차량의 이동벡터와 차량위치-인간위치 벡터의 dot product
이를 통해 차량의 이동 방향이 얼마나 인간을 향해 있는지 계산합니다. 결과값이 0.6 정도의 threshold보다 높다면 차량은 인간을 향해 이동하고 있는것이 되기 때문에 위험 차량으로 인식됩니다

1,2 모든 조건을 만족하는 차량이 한대라도 존재한다면 경고 [ALERT] 메시지를 출력합니다. 이 메시지를 출력하는 구간은 추후 STM에 dot matrix 동작을 요구하게 됩니다

그리고 경고신호가 나타난다면 동시에 ffmpeg를 통해 RTSP 스트림에서 이미지를 캡처하고, SQLite DB에 저장합니다.
캡처된 이미지는 메타데이터의 타임스탬프(UTC)를 KTC로 변환한 것을 파일명으로 하여 저장해야합니다. (현재는 UTC 그대로 저장합니다)
캡처된 이미지는 DB에 저장 후 삭제됩니다.    

======================================================================================*/

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

// VideoAnalytics 블럭 안에서 objectId로 객체가 인간인지 검색하는 함수
// event 블럭 안에서는 사용해도 뭐 안뜸
bool is_human(const string& block, const string& object_id) {
    regex object_block_regex("<tt:Object ObjectId=\"" + object_id + "\"[\\s\\S]*?<tt:Type[^>]*>([^<]*)</tt:Type>");
    smatch match;
    if (regex_search(block, match, object_block_regex)) {
        string type = match[1];
        cout << "[DEBUG] ObjectId=" << object_id << " 의 객체 타입: " << type << endl;
        return type == "Human";
    }
    cout << "[DEBUG] ObjectId=" << object_id << " 객체 정보를 Frame에서 찾지 못함" << endl;
    return false;
}

// 블럭 내에 VideoAnalytics 프레임이 포함되어 있는지 확인하는 함수
bool contains_frame_block(const string& block) {
    return block.find("<tt:VideoAnalytics>") != string::npos &&
           block.find("<tt:Frame") != string::npos;
}

// 라인크로싱 이벤트 블럭인지 확인하고 ObjectId와 RuleName을 추출하는 함수
bool is_linecrossing_event(const string& block, string& object_id, string& rule_name) {
    // [1] Topic 추출
    regex topic_regex("<wsnt:Topic[^>]*>([^<]*)</wsnt:Topic>");
    smatch topic_match;
    if (!regex_search(block, topic_match, topic_regex)) {
        return false;
    }

    string topic = topic_match[1];
    cout << "[DEBUG] 추출된 Topic: " << topic << endl;

    if (topic.find("LineCrossing") == string::npos)
        return false;

    // [2] block 전체에서 SimpleItem들 검색
    regex id_regex("<tt:SimpleItem Name=\"ObjectId\" Value=\"(\\d+)\"");
    regex rule_regex("<tt:SimpleItem Name=\"RuleName\" Value=\"([^\"]+)\"");
    regex state_regex("<tt:SimpleItem Name=\"State\" Value=\"true\"");

    smatch match;
    bool id_ok = regex_search(block, match, id_regex);
    if (id_ok) object_id = match[1];

    bool rule_ok = regex_search(block, match, rule_regex);
    if (rule_ok) rule_name = match[1];

    bool state_ok = regex_search(block, match, state_regex);

    if (id_ok && rule_ok && state_ok) {
        cout << "[DEBUG] LineCrossing 이벤트 확인됨 → ObjectId=" << object_id
             << ", RuleName=" << rule_name << endl;
        return true;
    } else {
        cout << "[DEBUG] LineCrossing 이벤트이지만 매칭 실패 → "
             << "ObjectId:" << (id_ok ? "O" : "X") << ", "
             << "RuleName:" << (rule_ok ? "O" : "X") << ", "
             << "State:true:" << (state_ok ? "O" : "X") << endl;
        cout << "[DEBUG] block 내용:\n" << block << "\n";
        return false;
    }
}



// 설정값
constexpr float direction_threshold = 0.65f;    // 차량 방향 판단을 위한 코사인 유사도 임계값
constexpr float dist_threshold = 10.0f;         // 차량 이동 판단을 위한 거리 임계값

// 차량 이동 경로 이력 저장 (최근 N프레임)
const int HISTORY_SIZE = 2;
unordered_map<int, deque<Point>> trajectory_history;

// 인간 이동 경로 이력 저장 (최근 N프레임)
const int HUMAN_HISTORY_SIZE = 2;
unordered_map<int, deque<Point>> human_history;

unordered_map<int, Point> update_human_positions(const string& frame_block, bool& frame_logged) {
    unordered_map<int, Point> current_human_centers;

    regex human_regex("<tt:Object ObjectId=\"(\\d+)\">[\\s\\S]*?<tt:Type[^>]*?>Human</tt:Type>[\\s\\S]*?<tt:CenterOfGravity x=\"([\\d.]+)\" y=\"([\\d.]+)\"");
    auto human_begin = sregex_iterator(frame_block.begin(), frame_block.end(), human_regex);

    for (auto it = human_begin; it != sregex_iterator(); ++it) {
        int human_id = stoi((*it)[1]);
        Point cog = {stof((*it)[2]), stof((*it)[3])};
        current_human_centers[human_id] = cog;

        bool is_new = human_history.find(human_id) == human_history.end();
        bool is_updated = false;

        if (!is_new && !human_history[human_id].empty()) {
            const Point& prev = human_history[human_id].back();
            float dx = cog.x - prev.x;
            float dy = cog.y - prev.y;
            float distance = sqrt(dx * dx + dy * dy);
            if (distance >= 50.0f) is_updated = true;
        }

        human_history[human_id].push_back(cog);
        if (human_history[human_id].size() > HUMAN_HISTORY_SIZE)
            human_history[human_id].pop_front();

        if (is_new) {
            if (!frame_logged) { cout << "frame update!" << endl; frame_logged = true; }
            cout << "[Human] " << human_id << " appear {x=" << cog.x << ", y=" << cog.y << "}" << endl;
        } else if (is_updated) {
            if (!frame_logged) { cout << "frame update!" << endl; frame_logged = true; }
            cout << "[Human] " << human_id << " {x=" << cog.x << ", y=" << cog.y << "}" << endl;
        }
    }

    for (auto it = human_history.begin(); it != human_history.end(); ) {
        if (current_human_centers.find(it->first) == current_human_centers.end()) {
            if (!frame_logged) { cout << "frame update!" << endl; frame_logged = true; }
            cout << "[Human] " << it->first << " disappear" << endl;
            it = human_history.erase(it);
        } else {
            ++it;
        }
    }

    return current_human_centers;
}



bool estimate_human_vector(const string& event_block, Point& human_vec, Point& human_center) {
    if (event_block.empty()) return false;

    regex id_regex("<tt:SimpleItem Name=\"ObjectId\" Value=\"(\\d+)\"");
    smatch match;
    if (!regex_search(event_block, match, id_regex)) {
        cout << "[DEBUG] ObjectId 추출 실패" << endl;
        return false;
    }

    int human_id = stoi(match[1]);
    auto it = human_history.find(human_id);
    if (it == human_history.end()) {
        cout << "[DEBUG] ObjectId=" << human_id << " 의 위치 이력이 없음" << endl;
        return false;
    }

    const auto& history = it->second;
    if (history.size() < 2) {
        cout << "[DEBUG] human_vec 추정 실패: 위치 이력 부족 (size=" << history.size() << ")" << endl;
        return false;
    }

    float dx = 0.0f, dy = 0.0f;
    for (size_t i = 1; i < history.size(); ++i) {
        dx += history[i].x - history[i - 1].x;
        dy += history[i].y - history[i - 1].y;
    }

    size_t steps = history.size() - 1;
    human_vec = {dx / steps, dy / steps};
    human_center = history.back();
    return true;
}


unordered_map<int, Point> update_vehicle_positions(const string& frame_block, bool& frame_logged) {
    unordered_map<int, Point> current_vehicle_centers;

    regex vehicle_regex("<tt:Object ObjectId=\"(\\d+)\">[\\s\\S]*?<tt:Type[^>]*?>Vehicle</tt:Type>[\\s\\S]*?<tt:CenterOfGravity x=\"([\\d.]+)\" y=\"([\\d.]+)\"");
    auto begin = sregex_iterator(frame_block.begin(), frame_block.end(), vehicle_regex);

    for (auto it = begin; it != sregex_iterator(); ++it) {
        int vehicle_id = stoi((*it)[1]);
        Point cog = {stof((*it)[2]), stof((*it)[3])};
        current_vehicle_centers[vehicle_id] = cog;

        bool is_new = trajectory_history.find(vehicle_id) == trajectory_history.end();
        bool is_updated = false;

        if (!is_new && !trajectory_history[vehicle_id].empty()) {
            const Point& prev = trajectory_history[vehicle_id].back();
            float dx = cog.x - prev.x;
            float dy = cog.y - prev.y;
            float distance = sqrt(dx * dx + dy * dy);
            if (distance >= 50.0f) is_updated = true;
        }

        trajectory_history[vehicle_id].push_back(cog);
        if (trajectory_history[vehicle_id].size() > HISTORY_SIZE)
            trajectory_history[vehicle_id].pop_front();

        if (is_new) {
            if (!frame_logged) { cout << "frame update!" << endl; frame_logged = true; }
            cout << "[Vehicle] " << vehicle_id << " appear {x=" << cog.x << ", y=" << cog.y << "}" << endl;
        } else if (is_updated) {
            if (!frame_logged) { cout << "frame update!" << endl; frame_logged = true; }
            cout << "[Vehicle] " << vehicle_id << " {x=" << cog.x << ", y=" << cog.y << "}" << endl;
        }
    }

    for (auto it = trajectory_history.begin(); it != trajectory_history.end(); ) {
        if (current_vehicle_centers.find(it->first) == current_vehicle_centers.end()) {
            if (!frame_logged) { cout << "frame update!" << endl; frame_logged = true; }
            cout << "[Vehicle] " << it->first << " disappear" << endl;
            it = trajectory_history.erase(it);
        } else {
            ++it;
        }
    }

    return current_vehicle_centers;
}

bool analyze_vehicle_threat(
    const unordered_map<int, Point>& current_vehicle_centers,
    const Point& human_vec,
    const Point& human_center,
    const string& rule_name,
    const string& time_source_block,
    string& direction_info
) {
    bool risk_detected = false;
    bool has_moving_vehicle = false;  // 이동 중인 차량 존재 여부

    for (const auto& [vehicle_id, cog] : current_vehicle_centers) {
        const auto& history = trajectory_history[vehicle_id];
        if (history.size() < 2) continue;

        // 차량 벡터 계산 (과거 평균 위치 기준)
        float sum_x = 0.0f, sum_y = 0.0f;
        for (size_t i = 0; i < history.size() - 1; ++i) {
            sum_x += history[i].x;
            sum_y += history[i].y;
        }
        size_t denom = history.size() - 1;
        Point avg_past = {sum_x / denom, sum_y / denom};
        Point vehicle_vec = {cog.x - avg_past.x, cog.y - avg_past.y};

        // 이동량이 충분한 경우만 판단
        float dist = sqrt(vehicle_vec.x * vehicle_vec.x + vehicle_vec.y * vehicle_vec.y);
        if (dist <= dist_threshold) continue;

        has_moving_vehicle = true;  // 이동 중인 차량 확인

        // dot 계산
        float dot_vehicle_human = compute_cosine_similarity(vehicle_vec, human_vec);
        Point vec_to_human = {
            human_center.x - cog.x,
            human_center.y - cog.y
        };
        float dot_vehicle_to_human = compute_cosine_similarity(vehicle_vec, vec_to_human);

        // 보조 방향 정보 (cross)
        float cross = human_vec.x * vec_to_human.y - human_vec.y * vec_to_human.x;
        string side_info;
        if (cross > 0) side_info = " ← 사람 기준 왼쪽";
        else if (cross < 0) side_info = " → 사람 기준 오른쪽";
        else side_info = " (정면/후면)";

        cout << "[DEBUG] 차량 ID=" << vehicle_id
             << ", dot(vehicle,human)=" << dot_vehicle_human
             << ", dot(vehicle,toHuman)=" << dot_vehicle_to_human
             << ", cross=" << cross << endl;

        // 위험 조건 판단
        if (fabs(dot_vehicle_human) <= 0.7f && dot_vehicle_to_human >= 0.5f) {
            direction_info = "(차량 측면 접근 중)" + side_info;

            cout << "[ALERT] RuleID='" << rule_name << "', 차량 ID=" << vehicle_id
                 << "가 " << side_info << "에서 사람을 향해 접근 중입니다." << endl;

            if (!risk_detected) {
                string timestamp = extract_timestamp(time_source_block);
                capture_and_store(timestamp);
                risk_detected = true;
            }
        } else {
            // 위험 조건 미충족한 차량 정보 출력
            cout << "[차량 " << vehicle_id << "] V⋅H=" << dot_vehicle_human
                 << ", V⋅toH=" << dot_vehicle_to_human << endl;
        }
    }

    // 위험 차량이 없을 경우 디버그 로그 추가
    if (!risk_detected) {
        cout << "[DEBUG] 이동 중인 차량 없음 또는 위험 조건 미충족 → 캡처 생략됨" << endl;
        if (!has_moving_vehicle) {
            cout << "이동 중인 차량이 없습니다" << endl;
        }
    }

    return risk_detected;
}



// 위험 상황 판단하는 로직 함수 
// 블럭 내에 이동 중인 차량이 있는지 판단하고, 위험 상황을 감지하는 함수
bool is_any_vehicle_moving(const string& event_block, const string& frame_block, const string& rule_name, string& direction_info) {
    lock_guard<mutex> lock(data_mutex);

    bool frame_logged = false;

    // [1] 사람 위치 갱신 + 출력
    unordered_map<int, Point> current_humans = update_human_positions(frame_block, frame_logged);

    // [2] 라인크로싱 발생한 사람의 벡터 추정
    Point human_vec = {0, 0};
    Point human_center = {0, 0};
    bool human_ok = estimate_human_vector(event_block, human_vec, human_center);

    // 라인크로싱인데 벡터 추정 실패하면 위험 판단 불가
    if (!human_ok && !event_block.empty()) return false;

    // [3] 차량 위치 갱신 + 출력
    unordered_map<int, Point> current_vehicles = update_vehicle_positions(frame_block, frame_logged);

    // [4] 차량 이동 방향과 사람 벡터 비교하여 위험 판단
    bool risk = analyze_vehicle_threat(current_vehicles, human_vec, human_center, rule_name,
                                       event_block.empty() ? frame_block : event_block, direction_info);

    // [5] 차량 위치 최신화
    prev_vehicle_centers = std::move(current_vehicles);

    return risk;
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

    string current_frame_block;

    while (fgets(buffer, BUFFER_SIZE, pipe)) {
        xml_buffer += buffer;

        if (xml_buffer.find("</tt:MetadataStream>") != string::npos) {
            string block = xml_buffer;
            xml_buffer.clear();

            string object_id, rule_name;

            // [1] 프레임 블럭이면 사람/차량 위치만 갱신
            if (contains_frame_block(block)) {
                current_frame_block = block;

                // 프레임 내 객체 위치 정보만 갱신 (위험 판단 X)
                lock_guard<mutex> lock(data_mutex);
                bool frame_logged = false;
                update_human_positions(current_frame_block, frame_logged);
                update_vehicle_positions(current_frame_block, frame_logged);
            }

            // [2] 이벤트 블럭이면 위험 판단 수행
            if (is_linecrossing_event(block, object_id, rule_name)) {
                // 이 시점의 current_frame_block을 기준으로 판단
                if (is_human(current_frame_block, object_id)) {
                    cout << "[DEBUG] 해당 객체는 Human입니다." << endl;

                    string direction_info;
                    bool risk = is_any_vehicle_moving(block, current_frame_block, rule_name, direction_info);

                    if (risk) {
                        string timestamp = extract_timestamp(block);
                        capture_and_store(timestamp);
                    } else {
                        cout << "[DEBUG] 이동 중인 차량 없음 또는 위험 조건 미충족 → 캡처 생략됨" << endl;
                    }
                } else {
                    cout << "[DEBUG] 해당 객체는 Human이 아님 → 무시됨" << endl;
                }
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
