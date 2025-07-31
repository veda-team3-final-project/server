#include "tcp_server.hpp"
#include "config_manager.hpp"

#include <unordered_map>
#include <memory>
#include <random>


#include "metadata_parser.hpp"
#include <thread>


/*

OpenSSL 관련

*/

std::mutex ssl_write_mutex;

SSL_CTX* ssl_ctx = nullptr;

// BBox 버퍼링 관련 전역 변수
std::atomic<int> bbox_buffer_delay_ms(2400);   // 기본값 1초
std::atomic<int> bbox_send_interval_ms(50);   // 기본값 200ms
std::queue<TimestampedBBox> bbox_buffer;
std::mutex bbox_buffer_mutex;

// OTP 관련 전역 변수
std::unordered_map<std::string, std::unique_ptr<cotp::TOTP>> user_otps;

// OTP 헬퍼 함수들
std::pair<std::string, std::string> generate_otp_uri(const std::string& id) {
    std::string secret = cotp::OTP::random_base32(16);
    auto totp = std::make_unique<cotp::TOTP>(secret, "SHA1", 6, 30);

    totp->set_account(id);
    totp->set_issuer("CCTV Monitoring System");
    std::string uri = totp->build_uri();

    user_otps[id] = std::move(totp);
    return {uri, secret};
}

bool verify_otp(const std::string& id, int otp) {
    auto it = user_otps.find(id);
    if (it != user_otps.end()) {
        return it->second->verify(otp, time(nullptr), 0);
    }
    return false;
}

std::string generate_qr_code_svg(const std::string& otp_uri) {
    cotp::QR_code qr;
    qr.set_content(otp_uri);
    return qr.get_svg();
}

std::vector<std::string> generate_recovery_codes() {
    static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::vector<std::string> codes;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(alphanum) - 2);

    for (int i = 0; i < 5; ++i) {
        std::string code;
        for (int j = 0; j < 10; ++j) {
            code += alphanum[dis(gen)];
        }
        codes.push_back(code);
    }
    return codes;
}

// SSL 초기화 함수
bool init_openssl() {
  SSL_load_error_strings();
  OpenSSL_add_ssl_algorithms();
  return true;
}

// SSL 정리 함수
void cleanup_openssl() { EVP_cleanup(); }

// SSL 컨텍스트 생성
SSL_CTX* create_ssl_context() {
  const SSL_METHOD* method = TLS_server_method();
  SSL_CTX* ctx = SSL_CTX_new(method);
  if (!ctx) {
    perror("SSL 컨텍스트 생성 실패");
    return nullptr;
  }
  return ctx;
}

// SSL 컨텍스트 설정
void configure_ssl_context(SSL_CTX* ctx) {
  if (SSL_CTX_use_certificate_file(ctx, "fullchain.crt", SSL_FILETYPE_PEM) <=
      0) {
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
  }

  if (SSL_CTX_use_PrivateKey_file(ctx, "server.key", SSL_FILETYPE_PEM) <= 0) {
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
  }
}

/*

OpenSSL 관련 끝

*/

// --- 응답 데이터를 저장할 콜백 함수 ---
// libcurl은 데이터를 작은 청크로 나눠서 이 함수를 여러 번 호출해요.
// userp 인자는 CURLOPT_WRITEDATA로 설정한 포인터를 받아요.
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  // userp는 std::string* 타입으로 캐스팅하여 응답 본문에 데이터를 추가해요.
  ((std::string*)userp)->append((char*)contents, size * nmemb);
  return size * nmemb;
}

string getLines() {
  string response_buffer;
  try {
    CURL* curl_handle;  // curl 통신을 위한 핸들

    // 2. curl_easy 핸들 생성
    // 각 통신 요청마다 이 핸들을 생성하고 설정해요.
    curl_handle = curl_easy_init();
    if (!curl_handle) {
      std::cerr << "curl_easy_init() 실패" << std::endl;
      curl_global_cleanup();
      return NULL;
    }
    // --- curl 명령어 옵션 설정 시작 ---
    // 2.1. URL 설정
    // 'https://192.168.0.137/opensdk/WiseAI/configuration/linecrossing'
    curl_easy_setopt(
        curl_handle, CURLOPT_URL,
        "https://192.168.0.137/opensdk/WiseAI/configuration/linecrossing");

    // 2.2. HTTP 메서드 설정 (GET)
    // -X GET 옵션은 CURLOPT_CUSTOMREQUEST로도 설정 가능하지만,
    // 기본이 GET이라 명시적으로 할 필요는 거의 없어요.
    // curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "GET");

    // 2.3. 인증 설정 (--digest -u admin:admin123@)
    // 다이제스트 인증 (CURLAUTH_DIGEST)과 사용자명:비밀번호 설정
    curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
    curl_easy_setopt(curl_handle, CURLOPT_USERPWD, "admin:admin123@");

    // 2.4. HTTPS 인증서 검증 비활성화 (--insecure)
    // 경고: 개발/테스트 환경에서만 사용하세요. 실제 운영 환경에서는 보안상 매우
    // 위험합니다!
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER,
                     0L);  // 피어(서버) 인증서 검증 안 함
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST,
                     0L);  // 호스트 이름 검증 안 함 (0L은 이전 버전과의
                           // 호환성을 위해 사용됨)
    // 최신 curl에서는 CURLOPT_SSL_VERIFYHOST를 0으로 설정하면 경고를 낼 수
    // 있으며, 1 (CA 파일 검증) 또는 2 (호스트네임과 CA 모두 검증)를 권장합니다.

    // 2.5. 커스텀 헤더 설정 (-H 옵션들)
    // struct curl_slist를 사용하여 헤더 목록을 만들고 추가해요.
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(
        headers, "Cookie: TRACKID=0842ca6f0d90294ea7de995c40a4aac6");
    headers = curl_slist_append(headers, "Origin: https://192.168.0.137");
    headers = curl_slist_append(
        headers,
        "Referer: "
        "https://192.168.0.137/home/setup/opensdk/html/WiseAI/index.html");
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER,
                     headers);  // 헤더 목록을 curl에 설정

    // 2.6. 압축 지원 (--compressed)
    // Accept-Encoding 헤더를 자동으로 추가하여 서버가 압축된 응답을 보내도록
    // 요청해요.
    curl_easy_setopt(
        curl_handle, CURLOPT_ACCEPT_ENCODING,
        "");  // 빈 문자열로 설정하면 curl이 지원하는 압축 방식을 모두 보냄

    // 2.7. 응답 본문을 프로그램 내에서 받기 위한 콜백 함수 설정
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA,
                     &response_buffer);  // 콜백 함수에 전달할 사용자 정의
                                         // 포인터 (응답 버퍼의 주소)

    // 2.8. 자세한 로그 출력 설정 (-v)
    // curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L); // 이 줄을 활성화하면
    // curl의 자세한 디버그 출력이 stderr로 나옴

    // --- curl 명령어 옵션 설정 끝 ---

    // 3. 요청 실행
    CURLcode res_perform = curl_easy_perform(curl_handle);

    // 4. 결과 확인
    if (res_perform != CURLE_OK) {
      std::cerr << "curl_easy_perform() 실패: "
                << curl_easy_strerror(res_perform) << std::endl;
    } else {
      long http_code = 0;
      curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE,
                        &http_code);  // HTTP 응답 코드 얻기

      std::cout << "--- HTTP 응답 수신 ---" << std::endl;
      std::cout << "HTTP Status Code: " << http_code << std::endl;
      std::cout << "--- 응답 본문 ---" << std::endl;
      std::cout << response_buffer << std::endl;
      std::cout << "------------------" << std::endl;
    }

    // 5. 자원 해제
    curl_easy_cleanup(curl_handle);  // curl 핸들 해제
    curl_slist_free_all(headers);  // 설정했던 헤더 목록 해제 (중요!)

  } catch (const std::exception& e) {
    // 예외 처리 (예: new 실패 등)
    std::cerr << "예외 발생: " << e.what() << std::endl;
  }
  return response_buffer;
}

string putLines(CrossLine crossLine) {
  string response_buffer;
  try {
    CURL* curl_handle;  // curl 통신을 위한 핸들

    // 2. curl_easy 핸들 생성
    // 각 통신 요청마다 이 핸들을 생성하고 설정해요.
    curl_handle = curl_easy_init();
    if (!curl_handle) {
      std::cerr << "curl_easy_init() 실패" << std::endl;
      curl_global_cleanup();
      return NULL;
    }
    // --- curl 명령어 옵션 설정 시작 ---
    // 2.1. URL 설정
    // 'https://192.168.0.137/opensdk/WiseAI/configuration/linecrossing'
    curl_easy_setopt(
        curl_handle, CURLOPT_URL,
        "https://192.168.0.137/opensdk/WiseAI/configuration/linecrossing");

    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "PUT");

    // 2.3. 인증 설정 (--digest -u admin:admin123@)
    // 다이제스트 인증 (CURLAUTH_DIGEST)과 사용자명:비밀번호 설정
    curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
    curl_easy_setopt(curl_handle, CURLOPT_USERPWD, "admin:admin123@");

    json curlRoot;
    curlRoot["channel"] = 0;
    curlRoot["enable"] = true;
    json lineArray = json::array();
    json line1;
    line1["index"] = crossLine.index;

    json coodArray = json::array();
    json cood1;
    cood1["x"] = crossLine.x1;
    cood1["y"] = crossLine.y1;
    coodArray.push_back(cood1);
    json cood2;
    cood2["x"] = crossLine.x2;
    cood2["y"] = crossLine.y2;
    coodArray.push_back(cood2);

    line1["lineCoordinates"] = coodArray;
    line1["mode"] = crossLine.mode;
    line1["name"] = crossLine.name;
    json otfArray = json::array();
    otfArray.push_back("Person");
    otfArray.push_back("Vehicle.Bicycle");
    otfArray.push_back("Vehicle.Car");
    otfArray.push_back("Vehicle.Motorcycle");
    otfArray.push_back("Vehicle.Bus");
    otfArray.push_back("Vehicle.Truck");
    line1["objectTypeFilter"] = otfArray;
    // ["Person","Vehicle.Bicycle","Vehicle.Car","Vehicle.Motorcycle","Vehicle.Bus","Vehicle.Truck"]
    lineArray.push_back(line1);

    curlRoot["line"] = lineArray;

    string insert_json_string = curlRoot.dump();
    cout << "--- HTTP Payload 원문 ---\n" << insert_json_string << "\n";

    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS,
                     insert_json_string.c_str());  // 데이터 포인터
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE,
                     insert_json_string.length());  // 데이터 길이

    // 2.4. HTTPS 인증서 검증 비활성화 (--insecure)
    // 경고: 개발/테스트 환경에서만 사용하세요. 실제 운영 환경에서는 보안상 매우
    // 위험합니다!
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER,
                     0L);  // 피어(서버) 인증서 검증 안 함
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST,
                     0L);  // 호스트 이름 검증 안 함 (0L은 이전 버전과의
                           // 호환성을 위해 사용됨)
    // 최신 curl에서는 CURLOPT_SSL_VERIFYHOST를 0으로 설정하면 경고를 낼 수
    // 있으며, 1 (CA 파일 검증) 또는 2 (호스트네임과 CA 모두 검증)를 권장합니다.

    // 2.5. 커스텀 헤더 설정 (-H 옵션들)
    // struct curl_slist를 사용하여 헤더 목록을 만들고 추가해요.
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(
        headers, "Cookie: TRACKID=0842ca6f0d90294ea7de995c40a4aac6");
    headers = curl_slist_append(headers, "Origin: https://192.168.0.137");
    headers = curl_slist_append(
        headers,
        "Referer: "
        "https://192.168.0.137/home/setup/opensdk/html/WiseAI/index.html");
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER,
                     headers);  // 헤더 목록을 curl에 설정

    // 2.6. 압축 지원 (--compressed)
    // Accept-Encoding 헤더를 자동으로 추가하여 서버가 압축된 응답을 보내도록
    // 요청해요.
    curl_easy_setopt(
        curl_handle, CURLOPT_ACCEPT_ENCODING,
        "");  // 빈 문자열로 설정하면 curl이 지원하는 압축 방식을 모두 보냄

    // 2.7. 응답 본문을 프로그램 내에서 받기 위한 콜백 함수 설정
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA,
                     &response_buffer);  // 콜백 함수에 전달할 사용자 정의
                                         // 포인터 (응답 버퍼의 주소)

    // --- curl 명령어 옵션 설정 끝 ---

    // 3. 요청 실행
    CURLcode res_perform = curl_easy_perform(curl_handle);

    // 4. 결과 확인
    if (res_perform != CURLE_OK) {
      std::cerr << "curl_easy_perform() 실패: "
                << curl_easy_strerror(res_perform) << std::endl;
    } else {
      long http_code = 0;
      curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE,
                        &http_code);  // HTTP 응답 코드 얻기

      std::cout << "--- HTTP 응답 수신 ---" << std::endl;
      std::cout << "HTTP Status Code: " << http_code << std::endl;
      std::cout << "--- 응답 본문 ---" << std::endl;
      std::cout << response_buffer << std::endl;
      std::cout << "------------------" << std::endl;
    }

    // 5. 자원 해제
    curl_easy_cleanup(curl_handle);  // curl 핸들 해제
    curl_slist_free_all(headers);  // 설정했던 헤더 목록 해제 (중요!)

  } catch (const std::exception& e) {
    // 예외 처리 (예: new 실패 등)
    std::cerr << "예외 발생: " << e.what() << std::endl;
  }

  return response_buffer;
}

string deleteLines(int index) {
  string response_buffer;
  try {
    CURL* curl_handle;  // curl 통신을 위한 핸들

    // 2. curl_easy 핸들 생성
    // 각 통신 요청마다 이 핸들을 생성하고 설정해요.
    curl_handle = curl_easy_init();
    if (!curl_handle) {
      std::cerr << "curl_easy_init() 실패" << std::endl;
      curl_global_cleanup();
      return NULL;
    }
    // --- curl 명령어 옵션 설정 시작 ---
    // 2.1. URL 설정
    string deleteUrl =
        "https://192.168.0.137/opensdk/WiseAI/configuration/linecrossing/"
        "line?channel=0&index=" +
        to_string(index);
    curl_easy_setopt(curl_handle, CURLOPT_URL, deleteUrl.c_str());

    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "DELETE");

    // 2.3. 인증 설정 (--digest -u admin:admin123@)
    // 다이제스트 인증 (CURLAUTH_DIGEST)과 사용자명:비밀번호 설정
    curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
    curl_easy_setopt(curl_handle, CURLOPT_USERPWD, "admin:admin123@");

    // 2.4. HTTPS 인증서 검증 비활성화 (--insecure)
    // 경고: 개발/테스트 환경에서만 사용하세요. 실제 운영 환경에서는 보안상 매우
    // 위험합니다!
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER,
                     0L);  // 피어(서버) 인증서 검증 안 함
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST,
                     0L);  // 호스트 이름 검증 안 함 (0L은 이전 버전과의
                           // 호환성을 위해 사용됨)
    // 최신 curl에서는 CURLOPT_SSL_VERIFYHOST를 0으로 설정하면 경고를 낼 수
    // 있으며, 1 (CA 파일 검증) 또는 2 (호스트네임과 CA 모두 검증)를 권장합니다.

    // 2.5. 커스텀 헤더 설정 (-H 옵션들)
    // struct curl_slist를 사용하여 헤더 목록을 만들고 추가해요.
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(
        headers, "Cookie: TRACKID=0842ca6f0d90294ea7de995c40a4aac6");
    headers = curl_slist_append(headers, "Origin: https://192.168.0.137");
    headers = curl_slist_append(
        headers,
        "Referer: "
        "https://192.168.0.137/home/setup/opensdk/html/WiseAI/index.html");
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER,
                     headers);  // 헤더 목록을 curl에 설정

    // 2.6. 압축 지원 (--compressed)
    // Accept-Encoding 헤더를 자동으로 추가하여 서버가 압축된 응답을 보내도록
    // 요청해요.
    curl_easy_setopt(
        curl_handle, CURLOPT_ACCEPT_ENCODING,
        "");  // 빈 문자열로 설정하면 curl이 지원하는 압축 방식을 모두 보냄

    // 2.7. 응답 본문을 프로그램 내에서 받기 위한 콜백 함수 설정
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA,
                     &response_buffer);  // 콜백 함수에 전달할 사용자 정의
                                         // 포인터 (응답 버퍼의 주소)

    // --- curl 명령어 옵션 설정 끝 ---

    // 3. 요청 실행
    CURLcode res_perform = curl_easy_perform(curl_handle);

    // 4. 결과 확인
    if (res_perform != CURLE_OK) {
      std::cerr << "curl_easy_perform() 실패: "
                << curl_easy_strerror(res_perform) << std::endl;
    } else {
      long http_code = 0;
      curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE,
                        &http_code);  // HTTP 응답 코드 얻기

      std::cout << "--- HTTP 응답 수신 ---" << std::endl;
      std::cout << "HTTP Status Code: " << http_code << std::endl;
      std::cout << "--- 응답 본문 ---" << std::endl;
      std::cout << response_buffer << std::endl;
      std::cout << "------------------" << std::endl;
    }

    // 5. 자원 해제
    curl_easy_cleanup(curl_handle);  // curl 핸들 해제
    curl_slist_free_all(headers);  // 설정했던 헤더 목록 해제 (중요!)

  } catch (const std::exception& e) {
    // 예외 처리 (예: new 실패 등)
    std::cerr << "예외 발생: " << e.what() << std::endl;
  }

  return response_buffer;
}

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
void handle_client(int client_socket, SQLite::Database& db,
                   std::mutex& db_mutex) {


  // SSL 접근 안전
  mutex ssl_write_mutex;

  // SSL 연결 설정
  SSL* ssl = SSL_new(ssl_ctx);
  if (!ssl) {
    ERR_print_errors_fp(stderr);
    close(client_socket);
    return;
  }

  SSL_set_fd(ssl, client_socket);
  if (SSL_accept(ssl) <= 0) {
    ERR_print_errors_fp(stderr);
    SSL_free(ssl);
    close(client_socket);
    return;
  }

  printNowTimeKST();
  cout << " [Thread " << std::this_thread::get_id()
       << "] SSL 클라이언트 처리 시작." << endl;

  create_table_detections(db);
  create_table_lines(db);
  create_table_baseLines(db);
  create_table_verticalLineEquations(db);
  create_table_accounts(db);
  create_table_recovery_codes(db);


  atomic<bool> bbox_push_enabled(false);
  atomic<bool> metadata_parsing_enabled(false);
  // BBox push 쓰레드와 메타데이터 파싱 쓰레드
  thread push_thread;
  thread metadata_thread;



  while (true) {
    uint32_t net_len;
    if (!recvAll(ssl, reinterpret_cast<char*>(&net_len), sizeof(net_len))) {
      break;
    }

    uint32_t json_len = ntohl(net_len);
    if (json_len == 0) {
      cerr << "[Thread " << std::this_thread::get_id()
           << "] 비정상적인 데이터 길이 수신: " << json_len << endl;
      break;
    }

    vector<char> json_buffer(json_len);
    if (!recvAll(ssl, json_buffer.data(), json_len)) {
      break;
    }

    try {
      json received_json = json::parse(json_buffer);
      printNowTimeKST();
      cout << " [Thread " << std::this_thread::get_id() << "] 수신 성공:\n"
           << received_json.dump(2) << endl;


      string json_string;
      if (received_json.value("request_id", -1) == 1) {
        // 클라이언트의 이미지&텍스트 요청(select) 신호
        string start_ts = received_json["data"].value("start_timestamp", "");
        string end_ts = received_json["data"].value("end_timestamp", "");

        vector<Detection> detections;
        // --- DB 접근 시 Mutex로 보호 ---
        {
          std::lock_guard<std::mutex> lock(db_mutex);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 조회 시작 (Lock 획득)" << endl;
          detections =
              select_data_for_timestamp_range_detections(db, start_ts, end_ts);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 조회 완료 (Lock 해제)" << endl;
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
        json_string = root.dump();

        uint32_t res_len = json_string.length();
        uint32_t net_res_len = htonl(res_len);
        sendAll(ssl, reinterpret_cast<const char*>(&net_res_len),
                sizeof(net_res_len), 0);
        sendAll(ssl, json_string.c_str(), res_len, 0);

        cout << "[Thread " << std::this_thread::get_id() << "] 응답 전송 완료."
             << endl;

        json res = {{"request_id", 1}, {"result", "ok"}};
        std::string res_str = res.dump();
        uint32_t len = htonl(res_str.size());

        {   // ✅ write 보호
          std::lock_guard<std::mutex> lock(ssl_write_mutex);
          sendAll(ssl, reinterpret_cast<const char*>(&len), sizeof(len), 0);
          sendAll(ssl, res_str.c_str(), res_str.size(), 0);
        }
      }

      else if (received_json.value("request_id", 2) == 2) {
        // 클라이언트의 감지선 좌표값 삽입(insert) 신호

        int index = received_json["data"].value("index", -1);
        int x1 = received_json["data"].value("x1", -1);
        int y1 = received_json["data"].value("y1", -1);
        int x2 = received_json["data"].value("x2", -1);
        int y2 = received_json["data"].value("y2", -1);
        string name = received_json["data"].value("name", "name1");
        string mode = received_json["data"].value("mode", "BothDirections");
        string camera_type = received_json.value("camera_type", "CCTV");

        CrossLine curlCrossLine = {index,  x1 * 4, y1 * 4, x2 * 4,
                                   y2 * 4, name,   mode};
        CrossLine insertCrossLine = {index, x1, y1, x2, y2, name, mode};

        bool mappingSuccess;
        // --- DB 접근 시 Mutex로 보호 ---
        {
          std::lock_guard<std::mutex> lock(db_mutex);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 삽입 시작 (Lock 획득)" << endl;
          mappingSuccess = insert_data_lines(db, insertCrossLine);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 삽입 완료 (Lock 해제)" << endl;
        }
        // --- 보호 끝 ---

        if (camera_type == "CCTV") {
          putLines(curlCrossLine);

          json root;
          root["request_id"] = 11;
          root["mapping_success"] = (mappingSuccess == true) ? 1 : 0;
          json_string = root.dump();

          uint32_t res_len = json_string.length();
          uint32_t net_res_len = htonl(res_len);
          sendAll(ssl, reinterpret_cast<const char*>(&net_res_len),
                  sizeof(net_res_len), 0);
          sendAll(ssl, json_string.c_str(), res_len, 0);
        }

        else {
          vector<CrossLine> dbLines;
          {
            std::lock_guard<std::mutex> lock(db_mutex);
            cout << "[Thread " << std::this_thread::get_id()
                 << "] DB 조회 시작 (Lock 획득)" << endl;
            dbLines = select_all_data_lines(db);
            cout << "[Thread " << std::this_thread::get_id()
                 << "] DB 조회 완료 (Lock 해제)" << endl;
          }

          json root;
          root["request_id"] = 18;
          json data_array = json::array();
          for (const auto& line : dbLines) {
            json d_obj;
            d_obj["index"] = line.index;
            d_obj["x1"] = line.x1;
            d_obj["y1"] = line.y1;
            d_obj["x2"] = line.x2;
            d_obj["y2"] = line.y2;
            d_obj["name"] = line.name;
            d_obj["mode"] = line.mode;
            data_array.push_back(d_obj);
          }
          root["data"] = data_array;
          json_string = root.dump();

          uint32_t res_len = json_string.length();
          uint32_t net_res_len = htonl(res_len);
          sendAll(ssl, reinterpret_cast<const char*>(&net_res_len),
                  sizeof(net_res_len), 0);
          sendAll(ssl, json_string.c_str(), res_len, 0);
        }

        cout << "[Thread " << std::this_thread::get_id() << "] 응답 전송 완료."
             << endl;

      }

      else if (received_json.value("request_id", -1) == 3) {
        // 클라이언트의 감지선 좌표값 요청(select all) 신호
        // getLine해서 기존 라인 정보 들고오기
        // db의 select all해서 라인 정보 들고오기

        // db에는 delete all하고 일치하는 부분만 다시 insert
        // 패킷은 일치하는 것만 전송

        vector<CrossLine> httpLines;
        // 3. JSON 문자열 파싱
        json j = json::parse(getLines());

        // 4. 데이터 추출 및 벡터에 추가
        // "lineCrossing" 배열의 첫 번째 요소 안에 있는 "line" 배열을 순회
        for (const auto& item : j["lineCrossing"][0]["line"]) {
          CrossLine cl;  // 임시 CrossLine 객체 생성

          // 각 필드 값 추출
          cl.index = item["index"];
          cl.name = item["name"];
          cl.mode = item["mode"];

          // lineCoordinates 배열에서 좌표 추출
          cl.x1 = item["lineCoordinates"][0]["x"];
          cl.y1 = item["lineCoordinates"][0]["y"];
          cl.x2 = item["lineCoordinates"][1]["x"];
          cl.y2 = item["lineCoordinates"][1]["y"];

          // 완성된 객체를 벡터에 추가
          httpLines.push_back(cl);
        }

        vector<CrossLine> dbLines;
        // --- DB 접근 시 Mutex로 보호 ---
        {
          std::lock_guard<std::mutex> lock(db_mutex);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 조회 시작 (Lock 획득)" << endl;
          dbLines = select_all_data_lines(db);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 조회 완료 (Lock 해제)" << endl;
        }
        // --- 보호 끝 ---

        vector<CrossLine> realLines;
        for (auto httpLine : httpLines) {
          for (auto dbLine : dbLines) {
            if (httpLine.index == dbLine.index) {
              realLines.push_back(dbLine);
            }
          }
        }

        // lines 테이블 비우고 실제 CCTV에 있는 가상선으로만 DB 채우기
        {
          std::lock_guard<std::mutex> lock(db_mutex);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 삭제 시작 (Lock 획득)" << endl;
          delete_all_data_lines(db);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 삭제 완료 (Lock 해제)" << endl;
        }
        for (auto realLine : realLines) {
          {
            std::lock_guard<std::mutex> lock(db_mutex);
            cout << "[Thread " << std::this_thread::get_id()
                 << "] DB 삽입 시작 (Lock 획득)" << endl;
            insert_data_lines(db, realLine);
            cout << "[Thread " << std::this_thread::get_id()
                 << "] DB 삽입 완료 (Lock 해제)" << endl;
          }
        }

        json root;
        root["request_id"] = 12;
        json data_array = json::array();
        for (const auto& line : realLines) {
          json d_obj;
          d_obj["index"] = line.index;
          d_obj["x1"] = line.x1;
          d_obj["y1"] = line.y1;
          d_obj["x2"] = line.x2;
          d_obj["y2"] = line.y2;
          d_obj["name"] = line.name;
          d_obj["mode"] = line.mode;
          data_array.push_back(d_obj);
        }
        root["data"] = data_array;
        json_string = root.dump();

        uint32_t res_len = json_string.length();
        uint32_t net_res_len = htonl(res_len);
        sendAll(ssl, reinterpret_cast<const char*>(&net_res_len),
                sizeof(net_res_len), 0);
        sendAll(ssl, json_string.c_str(), res_len, 0);
        cout << "[Thread " << std::this_thread::get_id() << "] 응답 전송 완료."
             << endl;
      }

      else if (received_json.value("request_id", -1) == 4) {
        // 클라이언트의 감지선, 기준선, 수직선 전체 삭제 신호
        // getLines로 라인들 인덱스 들고오기
        // cctv에선 들고온 인덱스로 deleteline(index) 반복 호출해서 삭제
        // db에선 delete_all 호출해서 전부 삭제

        vector<int> indexs;
        json j = json::parse(getLines());

        for (const auto& item : j["lineCrossing"][0]["line"]) {
          indexs.push_back(item["index"]);
        }

        for (int index : indexs) {
          deleteLines(index);
        }

        bool deleteSuccess;
        // --- DB 접근 시 Mutex로 보호 ---
        {
          std::lock_guard<std::mutex> lock(db_mutex);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 조회 시작 (Lock 획득)" << endl;
          deleteSuccess = delete_all_data_lines(db);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 조회 완료 (Lock 해제)" << endl;
        }
        // --- 보호 끝 ---
        {
          std::lock_guard<std::mutex> lock(db_mutex);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 조회 시작 (Lock 획득)" << endl;
          deleteSuccess = delete_all_data_baseLines(db);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 조회 완료 (Lock 해제)" << endl;
        }
        {
          std::lock_guard<std::mutex> lock(db_mutex);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 조회 시작 (Lock 획득)" << endl;
          deleteSuccess = delete_all_data_verticalLineEquations(db);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 조회 완료 (Lock 해제)" << endl;
        }

        json root;
        root["request_id"] = 13;
        root["delete_success"] = (deleteSuccess == true) ? 1 : 0;
        json_string = root.dump();

        uint32_t res_len = json_string.length();
        uint32_t net_res_len = htonl(res_len);
        sendAll(ssl, reinterpret_cast<const char*>(&net_res_len),
                sizeof(net_res_len), 0);
        sendAll(ssl, json_string.c_str(), res_len, 0);
        cout << "[Thread " << std::this_thread::get_id() << "] 응답 전송 완료."
             << endl;
      }

      else if (received_json.value("request_id", -1) == 5) {
        // 클라이언트의 도로기준선 insert 신호
        int index = received_json["data"].value("index", -1);
        int matrixNum1 = received_json["data"].value("matrixNum1", -1);
        int x1 = received_json["data"].value("x1", -1);
        int y1 = received_json["data"].value("y1", -1);
        int matrixNum2 = received_json["data"].value("matrixNum2", -1);
        int x2 = received_json["data"].value("x2", -1);
        int y2 = received_json["data"].value("y2", -1);

        BaseLine baseLine = {index, matrixNum1, x1, y1, matrixNum2, x2, y2};

        bool insertSuccess;
        // --- DB 접근 시 Mutex로 보호 ---
        {
          std::lock_guard<std::mutex> lock(db_mutex);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 삽입 시작 (Lock 획득)" << endl;
          insertSuccess = insert_data_baseLines(db, baseLine);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 삽입 완료 (Lock 해제)" << endl;
        }
        // --- 보호 끝 ---
        // 바뀐 매트릭스 번호가 있다면 업데이트
        bool updateSuccess;
        {
          std::lock_guard<std::mutex> lock(db_mutex);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 수정 시작 (Lock 획득)" << endl;
          updateSuccess = update_data_baseLines(db, baseLine);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 수정 완료 (Lock 해제)" << endl;
        }

        json root;
        root["request_id"] = 14;
        root["insert_success"] = (insertSuccess == true) ? 1 : 0;
        root["update_success"] = (updateSuccess == true) ? 1 : 0;
        json_string = root.dump();

        uint32_t res_len = json_string.length();
        uint32_t net_res_len = htonl(res_len);
        sendAll(ssl, reinterpret_cast<const char*>(&net_res_len),
                sizeof(net_res_len), 0);
        sendAll(ssl, json_string.c_str(), res_len, 0);
        cout << "[Thread " << std::this_thread::get_id() << "] 응답 전송 완료."
             << endl;
      }

      else if (received_json.value("request_id", -1) == 6) {
        // 클라이언트 감지선의 수직선 방정식 insert 신호
        int index = received_json["data"].value("index", -1);
        double a = received_json["data"].value("a", -1.0);  // ax+b = y
        double b = received_json["data"].value("b", -1.0);

        VerticalLineEquation verticalLineEquation = {index, a, b};

        bool insertSuccess;
        // --- DB 접근 시 Mutex로 보호 ---
        {
          std::lock_guard<std::mutex> lock(db_mutex);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 삽입 시작 (Lock 획득)" << endl;
          insertSuccess =
              insert_data_verticalLineEquations(db, verticalLineEquation);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 삽입 완료 (Lock 해제)" << endl;
        }
        // --- 보호 끝 ---

        json root;
        root["request_id"] = 14;
        root["insert_success"] = (insertSuccess == true) ? 1 : 0;
        json_string = root.dump();

        uint32_t res_len = json_string.length();
        uint32_t net_res_len = htonl(res_len);
        sendAll(ssl, reinterpret_cast<const char*>(&net_res_len),
                sizeof(net_res_len), 0);
        sendAll(ssl, json_string.c_str(), res_len, 0);
        cout << "[Thread " << std::this_thread::get_id() << "] 응답 전송 완료."
             << endl;
      }

      else if (received_json.value("request_id", -1) == 7) {
        // 클라이언트 도로기준선 select all(동기화) 신호

        vector<BaseLine> baseLines;
        // --- DB 접근 시 Mutex로 보호 ---
        {
          std::lock_guard<std::mutex> lock(db_mutex);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 조회 시작 (Lock 획득)" << endl;
          baseLines = select_all_data_baseLines(db);
          cout << "[Thread " << std::this_thread::get_id()
               << "] DB 조회 완료 (Lock 해제)" << endl;
        }
        // --- 보호 끝 ---

        json root;
        root["request_id"] = 16;
        json data_array = json::array();
        for (const auto& baseLine : baseLines) {
          json d_obj;
          d_obj["index"] = baseLine.index;
          d_obj["matrixNum1"] = baseLine.matrixNum1;
          d_obj["x1"] = baseLine.x1;
          d_obj["y1"] = baseLine.y1;
          d_obj["matrixNum2"] = baseLine.matrixNum2;
          d_obj["x2"] = baseLine.x2;
          d_obj["y2"] = baseLine.y2;
          data_array.push_back(d_obj);
        }
        root["data"] = data_array;
        json_string = root.dump();

        uint32_t res_len = json_string.length();
        uint32_t net_res_len = htonl(res_len);
        sendAll(ssl, reinterpret_cast<const char*>(&net_res_len),
                sizeof(net_res_len), 0);
        sendAll(ssl, json_string.c_str(), res_len, 0);
        cout << "[Thread " << std::this_thread::get_id() << "] 응답 전송 완료."
             << endl;
      }

      else if (received_json.value("request_id", -1) == 8) {
          cout << "[로그인] 1단계 로그인 요청 수신 (ID/PW 검증)" << endl;
          string id = received_json["data"].value("id", "");
          string passwd = received_json["data"].value("passwd", "");

          // 1. 입력 값 유효성 검사
          if (id.empty() || passwd.empty()) {
              cerr << "[에러] 클라이언트가 보낸 ID 또는 비밀번호가 비어 있습니다." << endl;
              continue;
          }

          Account* accountPtr = nullptr;
          {
              std::lock_guard<std::mutex> lock(db_mutex);
              cout << "[DB] ID로 계정 조회 시도 (ID: " << id << ")" << endl;
              accountPtr = get_account_by_id(db, id);
          }

          json root;
          root["request_id"] = 19;
          bool step1Success = false;

          if (accountPtr == nullptr) {
            cout << "[로그인] 1단계 실패: 존재하지 않는 ID (" << id << ")" << endl;
            step1Success = false;
        } else {
            if (verify_password(accountPtr->passwd, passwd)) {
                cout << "[로그인] 1단계 성공: ID/PW 검증 완료 (" << id << ")" << endl;
                step1Success = true;
                
                // OTP 객체 생성 (DB에서 secret 로드)
                if (!accountPtr->otp_secret.empty()) {
                    auto totp = std::make_unique<cotp::TOTP>(accountPtr->otp_secret, "SHA1", 6, 30);
                    totp->set_account(id);
                    totp->set_issuer("CCTV Monitoring System");
                    user_otps[id] = std::move(totp);
                }
            } else {
                cout << "[로그인] 1단계 실패: 비밀번호 불일치 (ID: " << id << ")" << endl;
                step1Success = false;
            }
        }

        // 평문 비밀번호 메모리에서 즉시 삭제
        memset(&passwd[0], 0, passwd.length());
        passwd.clear();
        cout << "[Debug] 평문 비밀번호 메모리에서 삭제 완료." << endl;

        root["step1_success"] = step1Success ? 1 : 0;
        if (step1Success) {
            root["requires_otp"] = (accountPtr->use_otp ? 1 : 0);
            if (accountPtr->use_otp)
                root["message"] = "ID/PW 검증 완료. OTP 또는 복구 코드를 입력하세요.";
            else
                root["message"] = "ID/PW 검증 완료. 바로 로그인 가능합니다.";
        }
        delete accountPtr;
        json_string = root.dump();

        uint32_t res_len = json_string.length();
        uint32_t net_res_len = htonl(res_len);
        sendAll(ssl, reinterpret_cast<const char*>(&net_res_len), sizeof(net_res_len), 0);
        sendAll(ssl, json_string.c_str(), res_len, 0);
        cout << "[응답] 1단계 로그인 결과 전송 완료." << endl;
      }

      // --- OTP/복구코드 검증 요청 (request_id: 22) ---
      else if (received_json.value("request_id", -1) == 22) {
          cout << "[로그인] 2단계 로그인 요청 수신 (OTP/복구코드 검증)" << endl;
          string id = received_json["data"].value("id", "");
          string input = received_json["data"].value("input", "");

          json root;
          root["request_id"] = 23;
          bool finalLoginSuccess = false;
          bool is_recovery_code = false;

          try {
              // 입력값이 숫자인지 확인 (OTP는 숫자, 복구코드는 문자열)
              bool is_numeric = !input.empty() && std::all_of(input.begin(), input.end(), ::isdigit);

              if (is_numeric && input.length() == 6) {
                  // OTP 검증
                  int otp = std::stoi(input);
                  if (verify_otp(id, otp)) {
                      cout << "[로그인] OTP 검증 성공 (ID: " << id << ")" << endl;
                      finalLoginSuccess = true;
                  } else {
                      cout << "[로그인] OTP 검증 실패 (ID: " << id << ")" << endl;
                  }
              } else {
                  // 복구 코드 검증
                  {
                      std::lock_guard<std::mutex> lock(db_mutex);
                      cout << "[DB] 복구 코드 검증 시도 (ID: " << id << ")" << endl;
                      if (verify_recovery_code(db, id, input)) {
                          cout << "[로그인] 복구 코드 검증 성공 (ID: " << id << ")" << endl;
                          finalLoginSuccess = true;
                          is_recovery_code = true;
                          // 사용된 복구 코드 무효화
                          invalidate_recovery_code(db, id, input);
                          cout << "[DB] 사용된 복구 코드 무효화 완료 (ID: " << id << ")" << endl;
                      } else {
                          cout << "[로그인] 복구 코드 검증 실패 (ID: " << id << ")" << endl;
                      }
                  }
              }

          } catch (const std::exception& e) {
              cerr << "[에러] OTP/복구코드 검증 실패: " << e.what() << endl;
              finalLoginSuccess = false;
          }

          root["final_login_success"] = finalLoginSuccess ? 1 : 0;
          if (finalLoginSuccess) {
              if (is_recovery_code) {
                  root["message"] = "복구 코드로 로그인 성공. 해당 복구 코드는 무효화되었습니다.";
              } else {
                  root["message"] = "OTP 검증 성공. 로그인 완료.";
              }
          } else {
              root["message"] = "OTP 또는 복구 코드가 올바르지 않습니다.";
          }
          json_string = root.dump();

          uint32_t res_len = json_string.length();
          uint32_t net_res_len = htonl(res_len);
          sendAll(ssl, reinterpret_cast<const char*>(&net_res_len), sizeof(net_res_len), 0);
          sendAll(ssl, json_string.c_str(), res_len, 0);
          cout << "[응답] 2단계 로그인 결과 전송 완료." << endl;
      }

      // --- 회원가입 요청 (request_id: 9) ---
      else if (received_json.value("request_id", -1) == 9) {
          cout << "[회원가입] 회원가입 요청 수신" << endl;
          string id = received_json["data"].value("id", "");
          string passwd = received_json["data"].value("passwd", "");
          bool use_otp = received_json["data"].value("use_otp", true); // 기본값 true

          json root;
          root["request_id"] = 20;
          bool signUpSuccess = false;
          string qr_code_svg = "";
          string otp_uri = "";
          vector<string> recovery_codes;
          string otp_secret = "";

          try {
              string hashed_passwd = hash_password(passwd);
              cout << "[회원가입] 비밀번호 해싱 완료 (ID: " << id << ")" << endl;

              // 평문 비밀번호 메모리에서 즉시 삭제
              memset(&passwd[0], 0, passwd.length());
              passwd.clear();
              cout << "[Debug] 평문 비밀번호 메모리에서 삭제 완료." << endl;

              if (use_otp) {
                  // OTP URI 생성
                  auto otp_result = generate_otp_uri(id);
                  otp_uri = otp_result.first;
                  otp_secret = otp_result.second;
                  cout << "[회원가입] OTP URI 생성 완료 (ID: " << id << ")" << endl;

                  // QR 코드 SVG 생성
                  qr_code_svg = generate_qr_code_svg(otp_uri);
                  cout << "[회원가입] QR 코드 생성 완료 (ID: " << id << ")" << endl;

                  // 복구 코드 생성
                  recovery_codes = generate_recovery_codes();
                  cout << "[회원가입] 복구 코드 생성 완료 (ID: " << id << ")" << endl;
              }

              Account account = {id, hashed_passwd, otp_secret, use_otp};
              {
                  std::lock_guard<std::mutex> lock(db_mutex);
                  cout << "[DB] 계정 정보 삽입 시도 (ID: " << id << ")" << endl;
                  signUpSuccess = insert_data_accounts(db, account);

                  if (signUpSuccess && use_otp) {
                      // OTP secret 저장
                      store_otp_secret(db, id, otp_secret);
                      // 복구 코드 저장
                      auto hashed_codes = hash_recovery_codes(recovery_codes);
                      store_recovery_codes(db, id, hashed_codes);
                  }
              }

              if (signUpSuccess) {
                  cout << "[회원가입] 성공 (ID: " << id << ")" << endl;
              } else {
                  cout << "[회원가입] 실패: ID 중복 또는 DB 오류 (ID: " << id << ")" << endl;
              }

          } catch (const std::runtime_error& e) {
              cerr << "[에러] 회원가입 처리 실패: " << e.what() << endl;
              signUpSuccess = false;
          }

          root["sign_up_success"] = signUpSuccess ? 1 : 0;
          if (signUpSuccess && use_otp) {
              root["qr_code_svg"] = qr_code_svg;
              root["otp_uri"] = otp_uri;
              json recovery_codes_json = json::array();
              for (const auto& code : recovery_codes) {
                  recovery_codes_json.push_back(code);
              }
              root["recovery_codes"] = recovery_codes_json;
          }
          json_string = root.dump();

          uint32_t res_len = json_string.length();
          uint32_t net_res_len = htonl(res_len);
          sendAll(ssl, reinterpret_cast<const char*>(&net_res_len), sizeof(net_res_len), 0);
          sendAll(ssl, json_string.c_str(), res_len, 0);
          cout << "[응답] 회원가입 결과 전송 완료." << endl;

          // --- 복구 코드 메모리에서 삭제 ---
          for (auto& code : recovery_codes) {
              if (!code.empty()) {
                  memset(&code[0], 0, code.size());
              }
          }
          recovery_codes.clear();
          cout << "[Debug] 복구 코드 메모리에서 삭제 완료." << endl;
      }
      else if (received_json.value("request_id", -1) == 31 && !bbox_push_enabled) {
        // 메타데이터 파싱 시작
        cout << "[TCP Server] Starting metadata parser..." << endl;
        start_metadata_parser();
        metadata_thread = std::thread(parse_metadata);

        // bbox push 스레드 시작
        bbox_push_enabled = true;
        cout << "[TCP Server] Starting bbox push thread..." << endl;
        push_thread = std::thread([ssl, &bbox_push_enabled, &ssl_write_mutex]() {
            cout << "[TCP Server] Bbox push thread started with " 
                 << bbox_send_interval_ms.load() << "ms interval and " 
                 << bbox_buffer_delay_ms.load() << "ms delay" << endl;
            
            auto next_send_time = std::chrono::steady_clock::now();
            
            while (bbox_push_enabled) {
                auto now = std::chrono::steady_clock::now();
                if (now >= next_send_time) {
                    bool success = send_bboxes_to_client(ssl);
                    if (!success) {
                        cout << "[TCP Server] Failed to send bboxes, stopping thread" << endl;
                        break;
                    }
                    // 다음 전송 시간 설정
                    next_send_time = now + std::chrono::milliseconds(bbox_send_interval_ms.load());
                }
                // 더 자주 체크하도록 sleep 시간 단축
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            cout << "[TCP Server] Bbox push thread ended" << endl;
        });
      }

      else if (received_json.value("request_id", -1) == 32 && bbox_push_enabled) {
          // bbox push 중지
          cout << "[TCP Server] Stopping bbox push..." << endl;
          bbox_push_enabled = false;
          if (push_thread.joinable()) push_thread.join();

          // 메타데이터 파싱 중지
          cout << "[TCP Server] Stopping metadata parser..." << endl;
          stop_metadata_parser();
          if (metadata_thread.joinable()) metadata_thread.join();
          
          // 버퍼 정리 (오래된 데이터 제거)
          clear_bbox_buffer();
          
          cout << "[TCP Server] Bbox push and metadata parser stopped" << endl;
      }

      cout << "JSON 송신 성공 : (" << json_string.size() << " 바이트):\n"
           << json_string.substr(0, 300) << " # 이후 데이터 출력 생략" << endl;
    } catch (const json::parse_error& e) {
      cerr << "[Thread " << std::this_thread::get_id()
           << "] JSON 파싱 에러: " << e.what() << endl;
    }
  }

  // 연결 종료 직전 정리
  bbox_push_enabled = false;
  metadata_parsing_enabled = false;
  
  if (push_thread.joinable()) push_thread.join();
  if (metadata_thread.joinable()) metadata_thread.join();

  close(client_socket);
  printNowTimeKST();
  cout << " [Thread " << std::this_thread::get_id()
       << "] 클라이언트 연결 종료 및 스레드 정리." << endl;
}

// --- 메인 TCP 서버 로직 (수정됨) ---
int tcp_run() {
  // 설정 로드
  if (!load_all_config()) {
    cerr << "[ERROR] 설정 로드 실패" << endl;
    return -1;
  }

  // OpenSSL 초기화
  if (!init_openssl()) {
    cerr << "OpenSSL 초기화 실패" << endl;
    return -1;
  }

  // SSL 컨텍스트 생성
  ssl_ctx = create_ssl_context();
  if (!ssl_ctx) {
    cleanup_openssl();
    return -1;
  }

  // SSL 컨텍스트 설정
  configure_ssl_context(ssl_ctx);

  SQLite::Database db("server_log.db",
                      SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
  cout << "데이터베이스 파일 'server_log.db'에 연결되었습니다.\n";

  // 1. libcurl 전역 초기화
  // 프로그램 시작 시 한 번만 호출하면 돼요.
  CURLcode res_global_init = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (res_global_init != CURLE_OK) {
    std::cerr << "curl_global_init() 실패: "
              << curl_easy_strerror(res_global_init) << std::endl;
    return -1;
  }

  std::mutex db_mutex;  // DB 접근을 보호할 뮤텍스 객체

  int server_fd;
  struct sockaddr_in address;
  socklen_t addrlen = sizeof(address);

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);
  if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
    cerr << "Bind Failed" << endl;
    return -1;
  }
  if (listen(server_fd, 10) < 0) {
    cerr << "Listen Failed" << endl;
    return -1;
  }

  printNowTimeKST();
  cout << " 멀티스레드 서버 시작. 클라이언트 연결 대기 중... (Port: " << PORT
       << ")" << endl;

  while (true) {
    int new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);
    if (new_socket < 0) {
      cerr << "연결 수락 실패" << endl;
      continue;
    }

    printNowTimeKST();
    cout << " 메인 스레드: 클라이언트 연결 수락됨. 처리 스레드 생성..." << endl;

    // 새 클라이언트를 처리할 스레드 생성 및 분리
    std::thread client_thread(handle_client, new_socket, std::ref(db),
                              std::ref(db_mutex));
    client_thread.detach();  // 메인 스레드는 기다리지 않고 바로 다음
                             // 클라이언트를 받으러 감
  }

  close(server_fd);
  curl_global_cleanup();
  return 0;
}

// SSL 버전의 송수신 함수
bool recvAll(SSL* ssl, char* buffer, size_t len) {
  size_t total_received = 0;
  while (total_received < len) {
    int bytes_received =
        SSL_read(ssl, buffer + total_received, len - total_received);

    if (bytes_received <= 0) {
      int error = SSL_get_error(ssl, bytes_received);
      if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
        continue;
      }
      ERR_print_errors_fp(stderr);
      return false;
    }
    total_received += bytes_received;
  }
  return true;
}

ssize_t sendAll(SSL* ssl, const char* buffer, size_t len, int flags) {
  size_t total_sent = 0;
  while (total_sent < len) {
    int bytes_sent = SSL_write(ssl, buffer + total_sent, len - total_sent);

    if (bytes_sent <= 0) {
      int error = SSL_get_error(ssl, bytes_sent);
      if (error == SSL_ERROR_WANT_WRITE || error == SSL_ERROR_WANT_READ) {
        continue;
      }
      ERR_print_errors_fp(stderr);
      return -1;
    }

    total_sent += bytes_sent;
  }

  return total_sent;
}

void printNowTimeKST() {
  // 한국 시간 (KST), 밀리초 포함 출력
  chrono::system_clock::time_point now = chrono::system_clock::now();
  time_t now_c = chrono::system_clock::to_time_t(now);
  auto duration_since_epoch = now.time_since_epoch();
  auto milliseconds =
      chrono::duration_cast<chrono::milliseconds>(duration_since_epoch)
          .count() %
      1000;
  const long KST_OFFSET_SECONDS = 9 * 60 * 60;
  time_t kst_now_c = now_c + KST_OFFSET_SECONDS;
  tm* kst_tm = gmtime(&kst_now_c);
  cout << "\n[" << put_time(kst_tm, "%Y-%m-%d %H:%M:%S") << "." << setfill('0')
       << setw(3) << milliseconds << " KST]" << endl;
}



/* 
 * ===================================================================
 * 바운딩 박스 전송 함수
 * 이 함수는 최신 바운딩 박스를 클라이언트에게 JSON 형식으로 전송합니다.
 * SSL을 사용하여 안전하게 데이터를 전송합니다.
 * ===================================================================
*/

// BBox 버퍼 정리 함수
void clear_bbox_buffer() {
    std::lock_guard<std::mutex> lock(bbox_buffer_mutex);
    
    // 버퍼의 모든 데이터 제거
    std::queue<TimestampedBBox> empty_queue;
    bbox_buffer.swap(empty_queue);
    
    std::cout << "[BUFFER] Buffer cleared completely" << std::endl;
}

// BBox 버퍼 업데이트 함수
void update_bbox_buffer(const std::vector<ServerBBox>& new_bboxes) {
    std::lock_guard<std::mutex> lock(bbox_buffer_mutex);
    
    // 새로운 bbox 데이터를 타임스탬프와 함께 버퍼에 저장
    TimestampedBBox data{
        std::chrono::steady_clock::now(),
        new_bboxes
    };
    bbox_buffer.push(data);
    
    // 버퍼 크기 관리 (너무 많은 데이터가 쌓이지 않도록)
    auto now = std::chrono::steady_clock::now();
    int removed_count = 0;
    
    // 1. 10초보다 오래된 데이터 제거
    while (!bbox_buffer.empty()) {
        const auto& oldest = bbox_buffer.front();
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - oldest.timestamp).count();
            
        if (age > 10000) {  // 10초보다 오래된 데이터 제거
            bbox_buffer.pop();
            removed_count++;
        } else {
            break;
        }
    }
    
    // 2. 버퍼 크기가 너무 크면 강제로 오래된 데이터 제거 (최대 50개 유지)
    while (bbox_buffer.size() > 50) {
        bbox_buffer.pop();
        removed_count++;
    }
    
    // 디버그 로그
    if (removed_count > 0 || bbox_buffer.size() > 10) {
        std::cout << "[BUFFER] Added 1 item, removed " << removed_count 
                 << " items, current size: " << bbox_buffer.size() << std::endl;
    }
}

// 바운딩 박스 전송 함수 (수정됨)
bool send_bboxes_to_client(SSL* ssl) {
    std::vector<ServerBBox> bboxes_to_send;
    int buffer_size = 0;
    int processed_count = 0;
    
    {
        std::lock_guard<std::mutex> lock(bbox_buffer_mutex);
        
        auto now = std::chrono::steady_clock::now();
        buffer_size = bbox_buffer.size();
        
        // 버퍼에서 지연 시간이 지난 가장 오래된 데이터 찾기
        while (!bbox_buffer.empty()) {
            const auto& oldest = bbox_buffer.front();
            auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - oldest.timestamp).count();
                
            if (age >= bbox_buffer_delay_ms.load()) {
                bboxes_to_send = oldest.bboxes;
                bbox_buffer.pop();
                processed_count++;
                break;  // 하나만 처리하고 나감
            }
            break;  // 지연 시간이 지나지 않은 데이터는 아직 전송하지 않음
        }
        
        // 디버그 로그 추가
        if (buffer_size > 5) {  // 버퍼에 5개 이상 쌓이면 경고
            std::cout << "[DEBUG] Buffer getting large: " << buffer_size 
                     << " items, processed: " << processed_count << std::endl;
        }
    }
    
    if (bboxes_to_send.empty()) {
        return true;  // 전송할 데이터가 없음 (에러 아님)
    }

    json bbox_array = json::array();
    for (const ServerBBox& box : bboxes_to_send) {
        json j = {
            {"id", box.object_id},
            {"type", box.type},
            {"confidence", box.confidence},
            {"x", box.left},
            {"y", box.top},
            {"width", box.right - box.left},
            {"height", box.bottom - box.top}
        };
        bbox_array.push_back(j);
    }
    
    json response = {
        {"response_id", 200},
        {"bboxes", bbox_array},
        {"buffer_info", {
            {"buffer_size", buffer_size},
            {"processed_count", processed_count}
        }}
    };

    std::string json_str = response.dump();
    uint32_t res_len = json_str.length();
    uint32_t net_res_len = htonl(res_len);

    {
        std::lock_guard<std::mutex> lock(ssl_write_mutex);
        // 먼저 4바이트 길이 접두사 전송
        if (sendAll(ssl, reinterpret_cast<const char*>(&net_res_len), sizeof(net_res_len), 0) == -1) {
            std::cout << "[TCP Server] Failed to send length prefix" << std::endl;
            return false;
        }
        // 그 다음 실제 JSON 데이터 전송
        int ret = sendAll(ssl, json_str.c_str(), res_len, 0);
        if (ret == -1) {
            std::cout << "[TCP Server] Failed to send JSON data" << std::endl;
        }
        return ret != -1;
    }
}

