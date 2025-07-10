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

int tcp_run() {
    SQLite::Database db("server_log.db",SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
            cout << "데이터베이스 파일 'server_log.db'에 연결되었습니다.\n";

    int server_fd, new_socket; // server_fd: 서버 소켓, new_socket: 클라이언트와 통신할 소켓
    struct sockaddr_in address; // 서버 주소 정보를 담을 구조체
    int addrlen = sizeof(address); // 주소 구조체 크기

    // 1. 소켓 생성 (IPv4, TCP 스트림 소켓)
    // AF_INET: IPv4
    // SOCK_STREAM: TCP (스트림 소켓)
    // 0: 프로토콜 (보통 0이면 기본 프로토콜)
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        cerr << "소켓 생성 실패: " << strerror(errno) << endl;
        return -1;
    }
    cout << "서버 소켓 생성 성공" << endl;

    // 주소 구조체 설정
    address.sin_family = AF_INET;           // IPv4
    address.sin_addr.s_addr = INADDR_ANY;   // 모든 사용 가능한 네트워크 인터페이스에서 연결 수락
    address.sin_port = htons(PORT);         // 호스트 바이트 순서를 네트워크 바이트 순서로 변환하여 포트 설정

    // SO_REUSEADDR 옵션 설정
    // 이 옵션은 서버를 재시작할 때 "Address already in use" 에러를 방지합니다.
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        cerr << "setsockopt 실패: " << strerror(errno) << endl;
    }

    // 2. 소켓에 IP 주소와 포트 번호 바인딩
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        cerr << "바인딩 실패: " << strerror(errno) << endl;
        close(server_fd);
        return -1;
    }
    cout << "소켓 바인딩 성공: 포트 " << PORT << endl;

    // 3. 연결 대기 (최대 3개의 동시 연결 요청 대기열)
    if (listen(server_fd, 3) < 0) {
        cerr << "연결 대기 실패: " << strerror(errno) << endl;
        close(server_fd);
        return -1;
    }
    cout << "클라이언트 연결 대기 중..." << endl;

    // 4. 클라이언트 연결 수락 및 통신
    while (true) {
        
        // 클라이언트 연결 요청 수락
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            cerr << "연결 수락 실패: " << strerror(errno) << endl;
            continue;
        }
        cout << "\n클라이언트 연결 수락됨: "
                  << inet_ntoa(address.sin_addr) << ":"
                  << ntohs(address.sin_port) << endl;

        // 5. 클라이언트와 데이터 송수신

        while ((cur_data_size = recv(new_socket, recvBP.buffer, BUFFER_SIZE, 0)) > 0) {


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


            // 수신받은 메시지, 송신 버퍼 처리 파트
            // string str = "";
            // for(int i=0;i<recvBP.cur_data_size;i++){
            //     str += recvBP.buffer[i];
            // }
            // vector<string> parts;
            // cout << "string : " << str << "\n";
            // int start = 0;
            // int end = str.find('/');
            // while(end != string::npos){
            //     cout << "split중 : " << end << "\n";
            //     parts.push_back(str.substr(start,end-start));
            //     start = end+1;
            //     end = str.find('/',start);
            // }
            // parts.push_back(str.substr(start));
            // for(auto p:parts){
            //     cout << p << "\n";
            // }

            // buffer_pack sendBP;
            // parts 구성 : 요청 번호, 성공실패여부, 데이터1, 데이터2, 데이터3
            if(parts[0] == "1"){ // 클라이언트의 이미지&텍스트 요청 신호
                // 데이터 1 : 조회할 데이터의 타임스탬프
                cout << "1번 요청 수신\n";
                string startTimestamp = parts[2];
                string endTimestamp = parts[3];

                vector<Detection> detections = select_data_for_timestamp_range_detections(db, startTimestamp, endTimestamp);

                json root;
                root["request_id"] = 10;
                json data_array = json::array();
                for (const auto& detection : detections) {
                    json detection_obj;
                    // imageBlob를 Base64로 인코딩하여 "image" 필드에 추가
                    detection_obj["image"] = base64_encode(detection.imageBlob);
                    detection_obj["timestamp"] = detection.timestamp;
                    data_array.push_back(detection_obj);
                }
                root["data"] = data_array;
                string json_string = root.dump();
                
                uint32_t json_len = json_string.length();
                uint32_t net_len_header = htonl(json_len); // Host to Network Long

                cout << "========송신========\n";
                cout << "보낼 JSON 데이터 길이: " << json_len << " 바이트\n";
                cout << "보낼 JSON 데이터: " << json_string << endl; // 너무 길면 주석 처리

                // 3. 길이 헤더(4바이트) 전송
                if (sendAll(new_socket, reinterpret_cast<const char*>(&net_len_header), sizeof(net_len_header), 0) == -1) {
                    cerr << "길이 헤더 전송 실패" << endl;
                    break; // 통신 중단
                }

                // 4. 실제 JSON 데이터 전송
                if (sendAll(new_socket, json_string.c_str(), json_len, 0) == -1) {
                    cerr << "JSON 데이터 전송 실패" << endl;
                    break; // 통신 중단
                }
                 cout << "길이 헤더와 JSON 데이터 전송 완료.\n";
            } 
            

            // 버퍼 초기화
            memset(recvBP.buffer, 0, BUFFER_SIZE);
            memset(sendBP.buffer, 0, BUFFER_SIZE);
        }

        if (cur_data_size == 0) {
            cout << "클라이언트 연결 종료됨." << endl;
        } else {
            cerr << "데이터 수신 오류: " << strerror(errno) << endl;
        }

        // 6. 클라이언트 소켓 닫기
        close(new_socket);
        cout << "클라이언트 소켓 닫힘." << endl;
    }

    // 서버 소켓 닫기
    close(server_fd);
    return 0;
}



ssize_t sendAll(int socket_fd, const char* buffer, size_t len, int flags) {
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t bytes_sent;

        // EINTR 처리를 위해 send 호출을 루프 안에 넣을 수 있습니다.
        bytes_sent = send(socket_fd, buffer + total_sent, len - total_sent, flags);

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