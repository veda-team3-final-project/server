cmake_minimum_required(VERSION 3.16)
project(rtsp_metadata_server)

# C++ 표준 설정
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 컴파일러 플래그 설정
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

# pkg-config 사용을 위한 모듈 로드
find_package(PkgConfig REQUIRED)

# 필요한 패키지들 찾기
pkg_check_modules(GSTREAMER REQUIRED gstreamer-rtsp-server-1.0 gstreamer-1.0 glib-2.0)
pkg_check_modules(CURL REQUIRED libcurl)

# OpenSSL 찾기
find_package(OpenSSL REQUIRED)

# SQLite 찾기
find_package(SQLite3 REQUIRED)

# 스레딩 지원
find_package(Threads REQUIRED)

# 인클루드 디렉토리 설정
include_directories(
    ${GSTREAMER_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ./otp/src
    ./otp/deps/QR-Code-generator
)

# 링크 디렉토리 설정
link_directories(
    ${GSTREAMER_LIBRARY_DIRS}
    ${CURL_LIBRARY_DIRS}
)

# 컴파일러 플래그 추가
add_definitions(${GSTREAMER_CFLAGS_OTHER})
add_definitions(${CURL_CFLAGS_OTHER})

# OTP 관련 소스 파일들 수집
file(GLOB OTP_SOURCES 
    "otp/src/*.cpp"
    "otp/src/cotp/*.cpp"
    "otp/deps/QR-Code-generator/qrcodegen.cpp"
)

# 메인 서버 실행파일
add_executable(server
    server.cpp
    rtsp_server.cpp
    tcp_server.cpp
    db_management.cpp
    metadata_parser.cpp
    src/hash.cpp
    ${OTP_SOURCES}
)

# 메인 서버 링크 라이브러리
target_link_libraries(server
    ${GSTREAMER_LIBRARIES}
    ${CURL_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    Threads::Threads
    SQLiteCpp
    sqlite3
    sodium
)

# 메타데이터 컨트롤 실행파일
add_executable(metadata_control
    metadata/main_control.cpp
    metadata/board_control.cpp
)

# 메타데이터 컨트롤 링크 라이브러리
target_link_libraries(metadata_control
    SQLiteCpp
    sqlite3
)

# 헤더 파일 의존성 설정
set_target_properties(server PROPERTIES
    DEPENDS "server_bbox.hpp"
)

# 빌드 타입별 설정
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(server PRIVATE DEBUG)
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
endif()

# 설치 설정 (선택사항)
install(TARGETS server metadata_control
    RUNTIME DESTINATION bin
)

# Clean 타겟 (make clean과 유사)
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Cleaning all build files"
)
