#include "tcp_server.hpp" 

/*

OpenSSL 관련

*/

SSL_CTX* ssl_ctx = nullptr;

// SSL 초기화 함수
bool init_openssl() {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
    return true;
}

// SSL 정리 함수
void cleanup_openssl() {
    EVP_cleanup();
}

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
    if (SSL_CTX_use_certificate_file(ctx, "fullchain.crt", SSL_FILETYPE_PEM) <= 0) {
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

string getLines(){
    
    string response_buffer;
    try {
        CURL *curl_handle; // curl 통신을 위한 핸들

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
        curl_easy_setopt(curl_handle, CURLOPT_URL, "https://192.168.0.137/opensdk/WiseAI/configuration/linecrossing");

        // 2.2. HTTP 메서드 설정 (GET)
        // -X GET 옵션은 CURLOPT_CUSTOMREQUEST로도 설정 가능하지만,
        // 기본이 GET이라 명시적으로 할 필요는 거의 없어요.
        // curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "GET");

        // 2.3. 인증 설정 (--digest -u admin:admin123@)
        // 다이제스트 인증 (CURLAUTH_DIGEST)과 사용자명:비밀번호 설정
        curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
        curl_easy_setopt(curl_handle, CURLOPT_USERPWD, "admin:admin123@");

        // 2.4. HTTPS 인증서 검증 비활성화 (--insecure)
        // 경고: 개발/테스트 환경에서만 사용하세요. 실제 운영 환경에서는 보안상 매우 위험합니다!
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L); // 피어(서버) 인증서 검증 안 함
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L); // 호스트 이름 검증 안 함 (0L은 이전 버전과의 호환성을 위해 사용됨)
        // 최신 curl에서는 CURLOPT_SSL_VERIFYHOST를 0으로 설정하면 경고를 낼 수 있으며,
        // 1 (CA 파일 검증) 또는 2 (호스트네임과 CA 모두 검증)를 권장합니다.

        // 2.5. 커스텀 헤더 설정 (-H 옵션들)
        // struct curl_slist를 사용하여 헤더 목록을 만들고 추가해요.
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Cookie: TRACKID=0842ca6f0d90294ea7de995c40a4aac6");
        headers = curl_slist_append(headers, "Origin: https://192.168.0.137");
        headers = curl_slist_append(headers, "Referer: https://192.168.0.137/home/setup/opensdk/html/WiseAI/index.html");
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers); // 헤더 목록을 curl에 설정

        // 2.6. 압축 지원 (--compressed)
        // Accept-Encoding 헤더를 자동으로 추가하여 서버가 압축된 응답을 보내도록 요청해요.
        curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, ""); // 빈 문자열로 설정하면 curl이 지원하는 압축 방식을 모두 보냄

        // 2.7. 응답 본문을 프로그램 내에서 받기 위한 콜백 함수 설정
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &response_buffer); // 콜백 함수에 전달할 사용자 정의 포인터 (응답 버퍼의 주소)

        // 2.8. 자세한 로그 출력 설정 (-v)
        // curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L); // 이 줄을 활성화하면 curl의 자세한 디버그 출력이 stderr로 나옴

        // --- curl 명령어 옵션 설정 끝 ---

        // 3. 요청 실행
        CURLcode res_perform = curl_easy_perform(curl_handle);

        // 4. 결과 확인
        if (res_perform != CURLE_OK) {
            std::cerr << "curl_easy_perform() 실패: " << curl_easy_strerror(res_perform) << std::endl;
        } else {
            long http_code = 0;
            curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code); // HTTP 응답 코드 얻기
            
            std::cout << "--- HTTP 응답 수신 ---" << std::endl;
            std::cout << "HTTP Status Code: " << http_code << std::endl;
            std::cout << "--- 응답 본문 ---" << std::endl;
            std::cout << response_buffer << std::endl;
            std::cout << "------------------" << std::endl;
        }

        // 5. 자원 해제
        curl_easy_cleanup(curl_handle); // curl 핸들 해제
        curl_slist_free_all(headers); // 설정했던 헤더 목록 해제 (중요!)

    } catch (const std::exception& e) {
        // 예외 처리 (예: new 실패 등)
        std::cerr << "예외 발생: " << e.what() << std::endl;
    }
    return response_buffer;
}

string putLines(CrossLine crossLines){
    string response_buffer;
    try {
        CURL *curl_handle; // curl 통신을 위한 핸들

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
        curl_easy_setopt(curl_handle, CURLOPT_URL, "https://192.168.0.137/opensdk/WiseAI/configuration/linecrossing");

        curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "PUT");

        // 2.3. 인증 설정 (--digest -u admin:admin123@)
        // 다이제스트 인증 (CURLAUTH_DIGEST)과 사용자명:비밀번호 설정
        curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
        curl_easy_setopt(curl_handle, CURLOPT_USERPWD, "admin:admin123@");

        
        json curlRoot;
        curlRoot["channel"] = 0;
        curlRoot["enable"] = true;
        json lineArray = json::array();
        for(const auto& crossLine:crossLines){
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
        }
        
        curlRoot["line"] = lineArray;

        string insert_json_string = curlRoot.dump();
        cout << "--- HTTP Payload 원문 ---\n" << insert_json_string << "\n"; 
        
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, insert_json_string.c_str()); // 데이터 포인터
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, insert_json_string.length()); // 데이터 길이

        // 2.4. HTTPS 인증서 검증 비활성화 (--insecure)
        // 경고: 개발/테스트 환경에서만 사용하세요. 실제 운영 환경에서는 보안상 매우 위험합니다!
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L); // 피어(서버) 인증서 검증 안 함
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L); // 호스트 이름 검증 안 함 (0L은 이전 버전과의 호환성을 위해 사용됨)
        // 최신 curl에서는 CURLOPT_SSL_VERIFYHOST를 0으로 설정하면 경고를 낼 수 있으며,
        // 1 (CA 파일 검증) 또는 2 (호스트네임과 CA 모두 검증)를 권장합니다.

        // 2.5. 커스텀 헤더 설정 (-H 옵션들)
        // struct curl_slist를 사용하여 헤더 목록을 만들고 추가해요.
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Cookie: TRACKID=0842ca6f0d90294ea7de995c40a4aac6");
        headers = curl_slist_append(headers, "Origin: https://192.168.0.137");
        headers = curl_slist_append(headers, "Referer: https://192.168.0.137/home/setup/opensdk/html/WiseAI/index.html");
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers); // 헤더 목록을 curl에 설정

        // 2.6. 압축 지원 (--compressed)
        // Accept-Encoding 헤더를 자동으로 추가하여 서버가 압축된 응답을 보내도록 요청해요.
        curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, ""); // 빈 문자열로 설정하면 curl이 지원하는 압축 방식을 모두 보냄

        // 2.7. 응답 본문을 프로그램 내에서 받기 위한 콜백 함수 설정
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &response_buffer); // 콜백 함수에 전달할 사용자 정의 포인터 (응답 버퍼의 주소)

        // --- curl 명령어 옵션 설정 끝 ---

        // 3. 요청 실행
        CURLcode res_perform = curl_easy_perform(curl_handle);

        // 4. 결과 확인
        if (res_perform != CURLE_OK) {
            std::cerr << "curl_easy_perform() 실패: " << curl_easy_strerror(res_perform) << std::endl;
        } else {
            long http_code = 0;
            curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code); // HTTP 응답 코드 얻기
            
            std::cout << "--- HTTP 응답 수신 ---" << std::endl;
            std::cout << "HTTP Status Code: " << http_code << std::endl;
            std::cout << "--- 응답 본문 ---" << std::endl;
            std::cout << response_buffer << std::endl;
            std::cout << "------------------" << std::endl;
        }

        // 5. 자원 해제
        curl_easy_cleanup(curl_handle); // curl 핸들 해제
        curl_slist_free_all(headers); // 설정했던 헤더 목록 해제 (중요!)

    } catch (const std::exception& e) {
        // 예외 처리 (예: new 실패 등)
        std::cerr << "예외 발생: " << e.what() << std::endl;
    }

    return response_buffer;
}

string deleteLines(int index){
    string response_buffer;
    try {
        CURL *curl_handle; // curl 통신을 위한 핸들

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
        string deleteUrl = "https://192.168.0.137/opensdk/WiseAI/configuration/linecrossing/line?channel=0&index="+to_string(index);
        curl_easy_setopt(curl_handle, CURLOPT_URL, deleteUrl.c_str());

        curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "DELETE");

        // 2.3. 인증 설정 (--digest -u admin:admin123@)
        // 다이제스트 인증 (CURLAUTH_DIGEST)과 사용자명:비밀번호 설정
        curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
        curl_easy_setopt(curl_handle, CURLOPT_USERPWD, "admin:admin123@");


        // 2.4. HTTPS 인증서 검증 비활성화 (--insecure)
        // 경고: 개발/테스트 환경에서만 사용하세요. 실제 운영 환경에서는 보안상 매우 위험합니다!
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L); // 피어(서버) 인증서 검증 안 함
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L); // 호스트 이름 검증 안 함 (0L은 이전 버전과의 호환성을 위해 사용됨)
        // 최신 curl에서는 CURLOPT_SSL_VERIFYHOST를 0으로 설정하면 경고를 낼 수 있으며,
        // 1 (CA 파일 검증) 또는 2 (호스트네임과 CA 모두 검증)를 권장합니다.
        

        // 2.5. 커스텀 헤더 설정 (-H 옵션들)
        // struct curl_slist를 사용하여 헤더 목록을 만들고 추가해요.
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Cookie: TRACKID=0842ca6f0d90294ea7de995c40a4aac6");
        headers = curl_slist_append(headers, "Origin: https://192.168.0.137");
        headers = curl_slist_append(headers, "Referer: https://192.168.0.137/home/setup/opensdk/html/WiseAI/index.html");
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers); // 헤더 목록을 curl에 설정

        // 2.6. 압축 지원 (--compressed)
        // Accept-Encoding 헤더를 자동으로 추가하여 서버가 압축된 응답을 보내도록 요청해요.
        curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, ""); // 빈 문자열로 설정하면 curl이 지원하는 압축 방식을 모두 보냄

        // 2.7. 응답 본문을 프로그램 내에서 받기 위한 콜백 함수 설정
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &response_buffer); // 콜백 함수에 전달할 사용자 정의 포인터 (응답 버퍼의 주소)

        // --- curl 명령어 옵션 설정 끝 ---

        // 3. 요청 실행
        CURLcode res_perform = curl_easy_perform(curl_handle);

        // 4. 결과 확인
        if (res_perform != CURLE_OK) {
            std::cerr << "curl_easy_perform() 실패: " << curl_easy_strerror(res_perform) << std::endl;
        } else {
            long http_code = 0;
            curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code); // HTTP 응답 코드 얻기
            
            std::cout << "--- HTTP 응답 수신 ---" << std::endl;
            std::cout << "HTTP Status Code: " << http_code << std::endl;
            std::cout << "--- 응답 본문 ---" << std::endl;
            std::cout << response_buffer << std::endl;
            std::cout << "------------------" << std::endl;
        }

        // 5. 자원 해제
        curl_easy_cleanup(curl_handle); // curl 핸들 해제
        curl_slist_free_all(headers); // 설정했던 헤더 목록 해제 (중요!)

    } catch (const std::exception& e) {
        // 예외 처리 (예: new 실패 등)
        std::cerr << "예외 발생: " << e.what() << std::endl;
    }

    return response_buffer;
}

string deleteAllLines(){
    string response_buffer;
    try {
        CURL *curl_handle; // curl 통신을 위한 핸들

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
        string deleteUrl = "https://192.168.0.137/opensdk/WiseAI/configuration/linecrossing";
        curl_easy_setopt(curl_handle, CURLOPT_URL, deleteUrl.c_str());

        curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "PUT");

        // 2.3. 인증 설정 (--digest -u admin:admin123@)
        // 다이제스트 인증 (CURLAUTH_DIGEST)과 사용자명:비밀번호 설정
        curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
        curl_easy_setopt(curl_handle, CURLOPT_USERPWD, "admin:admin123@");


        // 2.4. HTTPS 인증서 검증 비활성화 (--insecure)
        // 경고: 개발/테스트 환경에서만 사용하세요. 실제 운영 환경에서는 보안상 매우 위험합니다!
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L); // 피어(서버) 인증서 검증 안 함
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L); // 호스트 이름 검증 안 함 (0L은 이전 버전과의 호환성을 위해 사용됨)
        // 최신 curl에서는 CURLOPT_SSL_VERIFYHOST를 0으로 설정하면 경고를 낼 수 있으며,
        // 1 (CA 파일 검증) 또는 2 (호스트네임과 CA 모두 검증)를 권장합니다.

        // 2.5. 커스텀 헤더 설정 (-H 옵션들)
        // struct curl_slist를 사용하여 헤더 목록을 만들고 추가해요.
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Cookie: TRACKID=0842ca6f0d90294ea7de995c40a4aac6");
        headers = curl_slist_append(headers, "Origin: https://192.168.0.137");
        headers = curl_slist_append(headers, "Referer: https://192.168.0.137/home/setup/opensdk/html/WiseAI/index.html");
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers); // 헤더 목록을 curl에 설정

        // 2.6. 압축 지원 (--compressed)
        // Accept-Encoding 헤더를 자동으로 추가하여 서버가 압축된 응답을 보내도록 요청해요.
        curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, ""); // 빈 문자열로 설정하면 curl이 지원하는 압축 방식을 모두 보냄

        // 2.7. 응답 본문을 프로그램 내에서 받기 위한 콜백 함수 설정
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &response_buffer); // 콜백 함수에 전달할 사용자 정의 포인터 (응답 버퍼의 주소)

        // curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, insert_json_string.c_str()); // 데이터 포인터
        // curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, insert_json_string.length()); // 데이터 길이
        // --- curl 명령어 옵션 설정 끝 ---

        // 3. 요청 실행
        CURLcode res_perform = curl_easy_perform(curl_handle);

        // 4. 결과 확인
        if (res_perform != CURLE_OK) {
            std::cerr << "curl_easy_perform() 실패: " << curl_easy_strerror(res_perform) << std::endl;
        } else {
            long http_code = 0;
            curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code); // HTTP 응답 코드 얻기
            
            std::cout << "--- HTTP 응답 수신 ---" << std::endl;
            std::cout << "HTTP Status Code: " << http_code << std::endl;
            std::cout << "--- 응답 본문 ---" << std::endl;
            std::cout << response_buffer << std::endl;
            std::cout << "------------------" << std::endl;
        }

        // 5. 자원 해제
        curl_easy_cleanup(curl_handle); // curl 핸들 해제
        curl_slist_free_all(headers); // 설정했던 헤더 목록 해제 (중요!)

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
void handle_client(int client_socket, SQLite::Database& db, std::mutex& db_mutex) {
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
    cout << " [Thread " << std::this_thread::get_id() << "] SSL 클라이언트 처리 시작." << endl;

    create_table_detections(db);
    create_table_lines(db);
    create_table_baseLineCoordinates(db);
    create_table_verticalLineEquation(db);
   

    while (true) {
        uint32_t net_len;
        if (!recvAll(ssl, reinterpret_cast<char*>(&net_len), sizeof(net_len))) {
            break; 
        }

        uint32_t json_len = ntohl(net_len);
        if (json_len == 0) {
            cerr << "[Thread " << std::this_thread::get_id() << "] 비정상적인 데이터 길이 수신: " << json_len << endl;
            break;
        }

        vector<char> json_buffer(json_len);
        if (!recvAll(ssl, json_buffer.data(), json_len)) {
            break;
        }

        try {
            json received_json = json::parse(json_buffer);
            printNowTimeKST();
            cout << " [Thread " << std::this_thread::get_id() << "] 수신 성공:\n" << received_json.dump(2) << endl;

            string json_string;
            if (received_json.value("request_id", -1) == 1) {
                // 클라이언트의 이미지&텍스트 요청(select) 신호
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
                json_string = root.dump();
                
                uint32_t res_len = json_string.length();
                uint32_t net_res_len = htonl(res_len);
                sendAll(ssl, reinterpret_cast<const char*>(&net_res_len), sizeof(net_res_len), 0);
                sendAll(ssl, json_string.c_str(), res_len, 0);
                
                cout << "[Thread " << std::this_thread::get_id() << "] 응답 전송 완료." << endl;
            } 
            
            else if(received_json.value("request_id", 2) == 2){
                // 클라이언트의 가상 라인 좌표값 - 도트 매트릭스 매핑 요청(insert) 신호
                // getlines해서 기존 라인정보 들고오기
                // 기존 라인정보와 새로운 라인정보에 겹치는 라인이 있는 검사

                // 겹치면 
                // putlines, db insert 수행 X
                
                // 겹치지 않으면
                // putlines에 새로운 라인정보 + 기존 라인정보 추가하고 신호보내기
                // db에 새로운 라인정보만 추가

                int index = received_json["data"].value("index", -1);
                int x1 = received_json["data"].value("x1", -1);
                int y1 = received_json["data"].value("y1", -1);
                int x2 = received_json["data"].value("x2", -1);
                int y2 = received_json["data"].value("y2", -1);
                string name = received_json["data"].value("name", "name1");
                string mode = received_json["data"].value("mode", "BothDirections");
                int leftMatrixNum = received_json["data"].value("leftMatrixNum", 1);
                int rightMatrixNum = received_json["data"].value("rightMatrixNum", 2);

                CrossLine newCrossLine = {index, x1*4, y1*4, x2*4, y2*4, name, mode, leftMatrixNum, rightMatrixNum};

                // 2. 결과를 저장할 벡터
                vector<CrossLine> crossLines;
                bool isDuplicated = false;
                // 3. JSON 문자열 파싱
                json j = json::parse(getLines());

                // 4. 데이터 추출 및 벡터에 추가
                // "lineCrossing" 배열의 첫 번째 요소 안에 있는 "line" 배열을 순회
                for (const auto& item : j["lineCrossing"][0]["line"]) {
                    CrossLine cl; // 임시 CrossLine 객체 생성

                    // 각 필드 값 추출
                    cl.index = item["index"];
                    if(index == cl.index) {
                        isDuplicated = true;
                    }
                    cl.name = item["name"];
                    cl.mode = item["mode"];

                    // lineCoordinates 배열에서 좌표 추출
                    cl.x1 = item["lineCoordinates"][0]["x"];
                    cl.y1 = item["lineCoordinates"][0]["y"];
                    cl.x2 = item["lineCoordinates"][1]["x"];
                    cl.y2 = item["lineCoordinates"][1]["y"];

                    // 완성된 객체를 벡터에 추가
                    crossLines.push_back(cl);
                }


                if(isDuplicated == false){
                    crossLines.push_back(newCrossLine);
                    putLines(crossLines);

                    bool mappingSuccess;
                    // --- DB 접근 시 Mutex로 보호 ---
                    {
                        std::lock_guard<std::mutex> lock(db_mutex);
                        cout << "[Thread " << std::this_thread::get_id() << "] DB 삽입 시작 (Lock 획득)" << endl;
                        mappingSuccess = insert_data_lines(db,index,x1,y1,x2,y2,name,mode,leftMatrixNum,rightMatrixNum);
                        cout << "[Thread " << std::this_thread::get_id() << "] DB 삽입 완료 (Lock 해제)" << endl;
                    }
                    // --- 보호 끝 ---

                    json root;
                    root["request_id"] = 11;
                    root["mapping_success"] = (mappingSuccess == true)?1:0;
                    json_string = root.dump();
                    
                    uint32_t res_len = json_string.length();
                    uint32_t net_res_len = htonl(res_len);
                    sendAll(ssl, reinterpret_cast<const char*>(&net_res_len), sizeof(net_res_len), 0);
                    sendAll(ssl, json_string.c_str(), res_len, 0);
                } 
   
                cout << "[Thread " << std::this_thread::get_id() << "] 응답 전송 완료." << endl;

            } 
            
            else if(received_json.value("request_id", -1) == 3){
                // 클라이언트의 감지선 좌표값 요청(select all) 신호 
                // getLine해서 기존 라인 정보 들고오기
                // db의 select all해서 라인 정보 들고오기

                // 불일치되는 부분 

                // db에는 index로 하나씩 delete 
                // 패킷은 제외하고 남는 것만 전송

                vector<CrossLine> httpLines;
                // 3. JSON 문자열 파싱
                json j = json::parse(getLines());

                // 4. 데이터 추출 및 벡터에 추가
                // "lineCrossing" 배열의 첫 번째 요소 안에 있는 "line" 배열을 순회
                for (const auto& item : j["lineCrossing"][0]["line"]) {
                    CrossLine cl; // 임시 CrossLine 객체 생성

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
                    cout << "[Thread " << std::this_thread::get_id() << "] DB 조회 시작 (Lock 획득)" << endl;
                    dbLines = select_all_data_lines(db);
                    cout << "[Thread " << std::this_thread::get_id() << "] DB 조회 완료 (Lock 해제)" << endl;
                }
                // --- 보호 끝 ---

                vector<CrossLine> realLines;
                for(auto httpLine:httpLines){
                    for(auto dbLine:dbLines){
                        if(httpLine.index == dbLine.index){
                            realLines.push_back(dbLine);
                        }
                    }
                }

                // lines 테이블 비우고 실제 CCTV에 있는 가상선으로만 DB 채우기
                {
                    std::lock_guard<std::mutex> lock(db_mutex);
                    cout << "[Thread " << std::this_thread::get_id() << "] DB 삭제 시작 (Lock 획득)" << endl;
                    delete_all_data_lines(db);
                    cout << "[Thread " << std::this_thread::get_id() << "] DB 삭제 완료 (Lock 해제)" << endl;
                }
                for(auto realLine:realLines){
                    {
                        std::lock_guard<std::mutex> lock(db_mutex);
                        cout << "[Thread " << std::this_thread::get_id() << "] DB 삽입 시작 (Lock 획득)" << endl;
                        insert_data_lines(db,realLine.index,realLine.x1,realLine.y1,realLine.x2,realLine.y2,realLine.name,realLine.mode,realLine.leftMatrixNum,realLine.rightMatrixNum);
                        cout << "[Thread " << std::this_thread::get_id() << "] DB 삽입 완료 (Lock 해제)" << endl;
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
                    d_obj["right_matrix_num"] = line.rightMatrixNum;
                    d_obj["left_matrix_num"] = line.leftMatrixNum;
                    data_array.push_back(d_obj);
                }
                root["data"] = data_array;
                json_string = root.dump();
                
                uint32_t res_len = json_string.length();
                uint32_t net_res_len = htonl(res_len);
                sendAll(ssl, reinterpret_cast<const char*>(&net_res_len), sizeof(net_res_len), 0);
                sendAll(ssl, json_string.c_str(), res_len, 0);
                cout << "[Thread " << std::this_thread::get_id() << "] 응답 전송 완료." << endl;
            } 
            
            else if(received_json.value("request_id", -1) == 4){
                // 클라이언트에게 보낼 라인 삭제 신호
                // db에서 선 삭제
                // deleteline 호출

                // int deleteIndex = received_json["data"].value("index", -1);

                deleteAllLines();

                bool deleteSuccess;
                // --- DB 접근 시 Mutex로 보호 ---
                {
                    std::lock_guard<std::mutex> lock(db_mutex);
                    cout << "[Thread " << std::this_thread::get_id() << "] DB 조회 시작 (Lock 획득)" << endl;
                    deleteSuccess = delete_all_data_lines(db);
                    cout << "[Thread " << std::this_thread::get_id() << "] DB 조회 완료 (Lock 해제)" << endl;
                }
                // --- 보호 끝 ---

                json root;
                root["request_id"] = 13;
                root["delete_success"] = (deleteSuccess==true)?1:0;
                json_string = root.dump();
                
                uint32_t res_len = json_string.length();
                uint32_t net_res_len = htonl(res_len);
                sendAll(ssl, reinterpret_cast<const char*>(&net_res_len), sizeof(net_res_len), 0);
                sendAll(ssl, json_string.c_str(), res_len, 0);
                cout << "[Thread " << std::this_thread::get_id() << "] 응답 전송 완료." << endl;
            }

            else if(received_json.value("request_id", -1) == 5){
                // 클라이언트의 도로기준선 좌표 insert 신호
                int matrixNum = received_json["data"].value("matrixNum", -1);
                int x = received_json["data"].value("x", -1);
                int y = received_json["data"].value("y", -1);

                BaseLineCoordinate baseLineCoordinate = {matrixNum,x,y};

                bool insertSuccess;
                // --- DB 접근 시 Mutex로 보호 ---
                {
                    std::lock_guard<std::mutex> lock(db_mutex);
                    cout << "[Thread " << std::this_thread::get_id() << "] DB 삽입 시작 (Lock 획득)" << endl;
                    insertSuccess = insert_data_baseLineCoordinates(db,matrixNum,x,y);
                    cout << "[Thread " << std::this_thread::get_id() << "] DB 삽입 완료 (Lock 해제)" << endl;
                }
                // --- 보호 끝 ---

                json root;
                root["request_id"] = 14;
                root["insert_success"] = (insertSuccess == true)?1:0;
                json_string = root.dump();
                
                uint32_t res_len = json_string.length();
                uint32_t net_res_len = htonl(res_len);
                sendAll(ssl, reinterpret_cast<const char*>(&net_res_len), sizeof(net_res_len), 0);
                sendAll(ssl, json_string.c_str(), res_len, 0);
                cout << "[Thread " << std::this_thread::get_id() << "] 응답 전송 완료." << endl;
            } 
            
            else if(received_json.value("request_id", -1) == 6){
                // 클라이언트 감지선의 수직선 방정식 insert 신호 
                int index = received_json["data"].value("index", -1);
                double a = received_json["data"].value("a", -1); // ax+b = 0
                double b = received_json["data"].value("b", -1);

                VerticalLineEquation verticalLineEquation = {index,a,b};

                bool insertSuccess;
                // --- DB 접근 시 Mutex로 보호 ---
                {
                    std::lock_guard<std::mutex> lock(db_mutex);
                    cout << "[Thread " << std::this_thread::get_id() << "] DB 삽입 시작 (Lock 획득)" << endl;
                    insertSuccess = insert_data_verticalLineEquation(db,index,a,b);
                    cout << "[Thread " << std::this_thread::get_id() << "] DB 삽입 완료 (Lock 해제)" << endl;
                }
                // --- 보호 끝 ---

                json root;
                root["request_id"] = 14;
                root["insert_success"] = (insertSuccess == true)?1:0;
                json_string = root.dump();
                
                uint32_t res_len = json_string.length();
                uint32_t net_res_len = htonl(res_len);
                sendAll(ssl, reinterpret_cast<const char*>(&net_res_len), sizeof(net_res_len), 0);
                sendAll(ssl, json_string.c_str(), res_len, 0);
                cout << "[Thread " << std::this_thread::get_id() << "] 응답 전송 완료." << endl;
            } 
            
            else if(received_json.value("request_id", -1) == 7){
                // 클라이언트 도로기준선 좌표 select all(동기화) 신호

                vector<BaseLineCoordinate> baseLineCoordinates;
                // --- DB 접근 시 Mutex로 보호 ---
                {
                    std::lock_guard<std::mutex> lock(db_mutex);
                    cout << "[Thread " << std::this_thread::get_id() << "] DB 조회 시작 (Lock 획득)" << endl;
                    baseLineCoordinates = select_all_data_baseLineCoordinates(db);
                    cout << "[Thread " << std::this_thread::get_id() << "] DB 조회 완료 (Lock 해제)" << endl;
                }
                // --- 보호 끝 ---

                json root;
                root["request_id"] = 15;
                json data_array = json::array();
                for (const auto& baseLineCoordinate : baseLineCoordinates) {
                    json d_obj;
                    d_obj["matrixNum"] = baseLineCoordinate.matrixNum;
                    d_obj["x"] = baseLineCoordinate.x;
                    d_obj["y"] = baseLineCoordinate.y;
                    data_array.push_back(d_obj);
                }
                root["data"] = data_array;
                json_string = root.dump();
                
                uint32_t res_len = json_string.length();
                uint32_t net_res_len = htonl(res_len);
                sendAll(ssl, reinterpret_cast<const char*>(&net_res_len), sizeof(net_res_len), 0);
                sendAll(ssl, json_string.c_str(), res_len, 0);
                cout << "[Thread " << std::this_thread::get_id() << "] 응답 전송 완료." << endl;
            }

            cout << "송신 성공 : (" << json_string.size() << " 바이트):\n" << json_string.substr(0,100) << " # 이후 데이터 출력 생략"<< endl;
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

    SQLite::Database db("server_log.db", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    cout << "데이터베이스 파일 'server_log.db'에 연결되었습니다.\n";

    // 1. libcurl 전역 초기화
    // 프로그램 시작 시 한 번만 호출하면 돼요.
    CURLcode res_global_init = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res_global_init != CURLE_OK) {
        std::cerr << "curl_global_init() 실패: " << curl_easy_strerror(res_global_init) << std::endl;
        return -1;
    }
    
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
    curl_global_cleanup();
    return 0;
}

// SSL 버전의 송수신 함수
bool recvAll(SSL* ssl, char* buffer, size_t len) {
    size_t total_received = 0;
    while (total_received < len) {
        int bytes_received = SSL_read(ssl, buffer + total_received, len - total_received);
        
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

// 일반 소켓 버전의 송수신 함수
// bool recvAll(int socket_fd, char* buffer, size_t len) {
//     size_t total_received = 0;
//     while (total_received < len) {
//         ssize_t bytes_received = recv(socket_fd, buffer + total_received, len - total_received, 0);
        
//         if (bytes_received == -1) {
//             if (errno == EINTR) continue;
//             cerr << "recv 에러: " << strerror(errno) << endl;
//             return false;
//         }
//         if (bytes_received == 0) {
//             cerr << "데이터 수신 중 클라이언트 연결 종료" << endl;
//             return false;
//         }
//         total_received += bytes_received;
//     }
//     return true;
// }

// ssize_t sendAll(int socket_fd, const char* buffer, size_t len, int flags) {
//     size_t total_sent = 0;
//     while (total_sent < len) {
//         ssize_t bytes_sent = send(socket_fd, buffer + total_sent, len - total_sent, flags);

//         if (bytes_sent == -1) {
//             if (errno == EINTR) {
//                 continue;
//             }
//             return -1;
//         }

//         if (bytes_sent == 0) {
//             return total_sent;
//         }
//         total_sent += bytes_sent;
//     }
//     return total_sent;
// }

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
