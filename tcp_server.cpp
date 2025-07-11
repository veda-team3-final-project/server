#include "tcp_server.hpp" 
#include "db_management.hpp"

string base64_encode(const vector<unsigned char>& in) {
    string out;
    const string b64_chars =
                 "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                 "abcdefghijklmnopqrstuvwxyz"
                 "0123456789+/";

    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(b64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(b64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

// --- 클라이언트 처리 스레드 함수 ---
void handle_client(int client_socket, SQLite::Database& db, std::mutex& db_mutex) {
    printNowTimeKST();
    cout << " [Thread " << std::this_thread::get_id() << "] 클라이언트 처리 시작." << endl;

    while (true) {
        uint32_t net_len;
        if (!recvAll(client_socket, reinterpret_cast<char*>(&net_len), sizeof(net_len))) {
            break; 
        }

        uint32_t json_len = ntohl(net_len);
        if (json_len == 0) {
            cerr << "[Thread " << std::this_thread::get_id() << "] 비정상적인 데이터 길이 수신: " << json_len << endl;
            break;
        }

        vector<char> json_buffer(json_len);
        if (!recvAll(client_socket, json_buffer.data(), json_len)) {
            break;
        }

        try {
            json received_json = json::parse(json_buffer);
            printNowTimeKST();
            cout << " [Thread " << std::this_thread::get_id() << "] 수신 성공:\n" << received_json.dump(2) << endl;

            if (received_json.value("request_id", -1) == 1) {
                string start_ts = received_json["data"].value("start_timestamp", "");
                string end_ts = received_json["data"].value("end_timestamp", "");
                
                vector<Detection> detections;
                // --- DB 접근 시 Mutex로 보호 ---
                {
                    std::lock_guard<std::mutex> lock(db_mutex);
                    cout << "[Thread " << std::this_thread::get_id() << "] DB 조회 시작 (Lock 획득)" << endl;
                    detections = select_data_for_timestamp_range_detections(db, start_ts, end_ts);
                    cout << "[Thread " << std::this_thread::get_id() << "] DB 조회 완료 (Lock 해제)" << endl;
                }
                // --- 보호 끝 ---

                json root;
                root["request_id"] = 10;
                json data_array = json::array();
                for (const auto& detection : detections) {
                    json d_obj;
                    d_obj["image"] = base64_encode(detection.imageBlob);
                    d_obj["timestamp"] = detection.timestamp;
                    data_array.push_back(d_obj);
                }
                root["data"] = data_array;
                string json_string = root.dump();
                
                uint32_t res_len = json_string.length();
                uint32_t net_res_len = htonl(res_len);
                sendAll(client_socket, reinterpret_cast<const char*>(&net_res_len), sizeof(net_res_len), 0);
                sendAll(client_socket, json_string.c_str(), res_len, 0);
                cout << "[Thread " << std::this_thread::get_id() << "] 응답 전송 완료." << endl;
            }
        } catch (const json::parse_error& e) {
            cerr << "[Thread " << std::this_thread::get_id() << "] JSON 파싱 에러: " << e.what() << endl;
        }
    }

    close(client_socket);
    printNowTimeKST();
    cout << " [Thread " << std::this_thread::get_id() << "] 클라이언트 연결 종료 및 스레드 정리." << endl;
}


// --- 메인 TCP 서버 로직 (수정됨) ---
int tcp_run() {
    SQLite::Database db("server_log.db", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    cout << "데이터베이스 파일 'server_log.db'에 연결되었습니다.\n";
    
    std::mutex db_mutex; // DB 접근을 보호할 뮤텍스 객체

    int server_fd;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) { cerr << "Bind Failed" << endl; return -1; }
    if (listen(server_fd, 10) < 0) { cerr << "Listen Failed" << endl; return -1; }
    
    printNowTimeKST();
    cout << " 멀티스레드 서버 시작. 클라이언트 연결 대기 중... (Port: " << PORT << ")" << endl;

    while (true) {
        int new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (new_socket < 0) {
            cerr << "연결 수락 실패" << endl;
            continue;
        }
        
        printNowTimeKST();
        cout << " 메인 스레드: 클라이언트 연결 수락됨. 처리 스레드 생성..." << endl;

        // 새 클라이언트를 처리할 스레드 생성 및 분리
        std::thread client_thread(handle_client, new_socket, std::ref(db), std::ref(db_mutex));
        client_thread.detach(); // 메인 스레드는 기다리지 않고 바로 다음 클라이언트를 받으러 감
    }

    close(server_fd);
    return 0;
}

bool recvAll(int socket_fd, char* buffer, size_t len) {
    size_t total_received = 0;
    while (total_received < len) {
        ssize_t bytes_received = recv(socket_fd, buffer + total_received, len - total_received, 0);
        
        if (bytes_received == -1) {
            if (errno == EINTR) continue;
            cerr << "recv 에러: " << strerror(errno) << endl;
            return false;
        }
        if (bytes_received == 0) {
            cerr << "데이터 수신 중 클라이언트 연결 종료" << endl;
            return false;
        }
        total_received += bytes_received;
    }
    return true;
}

ssize_t sendAll(int new_socket, const char* buffer, size_t len, int flags) {
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t bytes_sent;

        // EINTR 처리를 위해 send 호출을 루프 안에 넣을 수 있습니다.
        bytes_sent = send(new_socket, buffer + total_sent, len - total_sent, flags);

        if (bytes_sent == -1) {
            // EINTR은 실제 에러가 아니므로, 재시도합니다.
            if (errno == EINTR) {
                continue; // while 루프의 처음으로 돌아가 send를 다시 시도
            }
            
            // 그 외의 에러는 치명적인 오류로 간주하고 -1을 반환합니다.
            // perror()는 제거하고, 호출자가 errno를 확인하도록 합니다.
            return -1;
        }

        if (bytes_sent == 0) {
            // 연결이 끊어진 경우, 지금까지 보낸 만큼만 반환합니다.
            // 이 부분은 기존 로직이 합리적이므로 유지합니다.
            return total_sent;
        }

        total_sent += bytes_sent;
    }

    return total_sent;
}

void printNowTimeKST(){
    // 한국 시간 (KST), 밀리초 포함 출력
    chrono::system_clock::time_point now = chrono::system_clock::now();
    time_t now_c = chrono::system_clock::to_time_t(now);
    auto duration_since_epoch = now.time_since_epoch();
    auto milliseconds = chrono::duration_cast<chrono::milliseconds>(duration_since_epoch).count() % 1000;
    const long KST_OFFSET_SECONDS = 9 * 60 * 60; 
    time_t kst_now_c = now_c + KST_OFFSET_SECONDS;
    tm* kst_tm = gmtime(&kst_now_c);
    cout << "["
            << put_time(kst_tm, "%Y-%m-%d %H:%M:%S") 
            << "." << setfill('0') << setw(3) << milliseconds << " KST]" << endl;
}