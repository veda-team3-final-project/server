#include "tcp_server.hpp" // 클래스 선언이 포함된 헤더 파일 포함



// 서버가 사용할 포트 번호
const int PORT = 8080;
// 버퍼 크기
const int BUFFER_SIZE = 1024;

int tcp_run() {
    int server_fd, new_socket; // server_fd: 서버 소켓, new_socket: 클라이언트와 통신할 소켓
    struct sockaddr_in address; // 서버 주소 정보를 담을 구조체
    int addrlen = sizeof(address); // 주소 구조체 크기
    char buffer[BUFFER_SIZE] = {0}; // 데이터 수신을 위한 버퍼

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
                                            // 특정 IP만 사용하려면 inet_addr("192.168.1.100") 사용
    address.sin_port = htons(PORT);         // 호스트 바이트 순서를 네트워크 바이트 순서로 변환하여 포트 설정

    // SO_REUSEADDR 옵션 설정 (선택 사항이지만 권장)
    // 이 옵션은 서버를 재시작할 때 "Address already in use" 에러를 방지합니다.
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        cerr << "setsockopt 실패: " << strerror(errno) << endl;
        // 치명적인 에러는 아니므로 계속 진행할 수 있음
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

    // 4. 클라이언트 연결 수락 및 통신 (무한 루프)
    while (true) {
        // 클라이언트 연결 요청 수락
        // 연결이 수락되면 새로운 소켓 디스크립터 (new_socket)가 반환됩니다.
        // 이 소켓을 통해 해당 클라이언트와 통신합니다.
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            cerr << "연결 수락 실패: " << strerror(errno) << endl;
            continue; // 다음 연결을 위해 계속 대기
        }
        cout << "\n클라이언트 연결 수락됨: "
                  << inet_ntoa(address.sin_addr) << ":"
                  << ntohs(address.sin_port) << endl;

        // 5. 클라이언트와 데이터 송수신 (이 예제에서는 에코 기능)
        int bytes_received;
        while ((bytes_received = recv(new_socket, buffer, BUFFER_SIZE, 0)) > 0) {
            // 수신된 데이터를 문자열로 변환 (널 종료 문자 고려)
            buffer[bytes_received] = '\0';
            cout << "수신: " << buffer << endl;

            // 수신된 데이터를 그대로 클라이언트에 다시 전송 (에코)
            send(new_socket, buffer, bytes_received, 0);
            cout << "전송: " << buffer << endl;

            // 버퍼 초기화
            memset(buffer, 0, BUFFER_SIZE);
        }

        if (bytes_received == 0) {
            cout << "클라이언트 연결 종료됨." << endl;
        } else { // bytes_received < 0
            cerr << "데이터 수신 오류: " << strerror(errno) << endl;
        }

        // 6. 클라이언트 소켓 닫기
        close(new_socket);
        cout << "클라이언트 소켓 닫힘." << endl;
    }

    // 서버 소켓 닫기 (이 예제에서는 무한 루프이므로 실제로 도달하지 않음)
    close(server_fd);
    return 0;
}