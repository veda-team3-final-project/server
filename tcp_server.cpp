#include "tcp_server.hpp" 
#include "db_management.hpp"

int tcp_run() {
    

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
        buffer_pack recvBP;
        while ((recvBP.cur_data_size = recv(new_socket, recvBP.buffer, BUFFER_SIZE, 0)) > 0) {

            chrono::system_clock::time_point now = chrono::system_clock::now();

            // 2. time_point를 time_t 타입으로 변환 (Unix timestamp와 유사)
            // time_t는 C 스타일 시간 관리에 사용되는 정수 타입
            time_t now_c = chrono::system_clock::to_time_t(now);

            // 밀리초/마이크로초/나노초 등 더 정밀한 시간도 출력 가능 (C++11/14/17 방식으로는 복잡)
            // chrono::duration_cast를 사용
            auto duration_since_epoch = now.time_since_epoch();
            auto milliseconds = chrono::duration_cast<chrono::milliseconds>(duration_since_epoch).count() % 1000;

            // --- 한국 시간 (KST) 출력 부분 추가 ---
            // 한국 시간은 UTC + 9시간 (9 * 60 * 60 초)
            const long KST_OFFSET_SECONDS = 9 * 60 * 60; 

            // UTC 시간을 기준으로 9시간을 더하여 KST time_t 값 계산
            time_t kst_now_c = now_c + KST_OFFSET_SECONDS;
            
            // 계산된 KST time_t 값을 tm 구조체로 변환 (UTC 기준 tm 구조체를 재사용)
            tm* kst_tm = gmtime(&kst_now_c); // gmtime은 UTC 기반 시간을 tm으로 변환

            // 한국 시간 (KST), 밀리초 포함 출력
            // 밀리초는 epoch_seconds 기준으로 계산되었으므로, KST 시간에 그대로 적용
            cout << "["
                    << put_time(kst_tm, "%Y-%m-%d %H:%M:%S") 
                    << "." << setfill('0') << setw(3) << milliseconds << " KST]" << endl;
            // ------------------------------------------

            cout << "========수신========\n";
            for(int i=0;i<recvBP.cur_data_size;i++){
                cout << i << " : " << recvBP.buffer[i] << "\n";
            }

            // 수신받은 메시지, 송신 버퍼 처리 파트
            buffer_pack sendBP = buffer_process(recvBP.buffer,recvBP.cur_data_size);

            send(new_socket, sendBP.buffer, sendBP.cur_data_size, 0);
            cout << "========송신========\n";
            for(int i=0;i<sendBP.cur_data_size;i++){
                cout << i << " : " << sendBP.buffer[i] << "\n";
            }

            // 버퍼 초기화
            memset(recvBP.buffer, 0, BUFFER_SIZE);
            memset(sendBP.buffer, 0, BUFFER_SIZE);
        }

        if (recvBP.cur_data_size == 0) {
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

// 수신 버퍼를 통해 내부 처리를 수행하고 송신 버퍼를 반환하는 함수
buffer_pack buffer_process(char buffer[],int bytes_received){
    SQLite::Database db("server_log.db",SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    cout << "데이터베이스 파일 'server_log.db'에 연결되었습니다.\n";

    string str = "";
    for(int i=0;i<bytes_received;i++){
        str += buffer[i];
    }
    vector<string> parts;
    cout << "string : " << str << "\n";
    int start = 0;
    int end = str.find('/');
    while(end != string::npos){
        cout << "split중 : " << end << "\n";
        parts.push_back(str.substr(start,end-start));
        start = end+1;
        end = str.find('/',start);
    }
    parts.push_back(str.substr(start));
    for(auto p:parts){
        cout << p << "\n";
    }

    buffer_pack bp;
    // parts 구성 : 요청 번호, 성공실패여부, 데이터1, 데이터2, 데이터3
    if(parts[0] == "1"){ // 클라이언트의 이미지&텍스트 요청 신호
        // 데이터 1 : 조회할 데이터의 타임스탬프
        cout << "1번 요청 수신\n";
        string startTimestamp = parts[2];
        string endTimestamp = parts[3];
        vector<LogData> logDatas = select_data_for_timestamp_range(db, startTimestamp, endTimestamp);
        
        // 클라이언트에게 보낼 이미지&텍스트 데이터
        bp.buffer[0] = '\0';
        memcpy(bp.buffer,string("10/1/").data(),string("10/1/").size());
        bp.cur_data_size += string("10/1/").size();
        memcpy(bp.buffer+bp.cur_data_size,logDatas.data(),logDatas.size());
        bp.cur_data_size += logDatas.size();
    }
    return bp;
}