## Linux Raspberry Pi

### 필요 패키지 설치 명령어 :
```
sudo apt update
sudo apt install libgstreamer1.0-dev libgstrtspserver-1.0-dev
sudo apt install libcurl4-openssl-dev
sudo apt install libsodium-dev
```

### (Optional) 부가 패키지 설치 명령어 :
```
sudo apt install sqlite3 // sqlite db 수동 조작용
sudo apt install clang-format // 코드 스타일 적용 자동화
```

### OpenSSL용 서버 키 생성 가이드 :
https://wldh0026.atlassian.net/wiki/x/A4DL


### 빌드 및 실행 방법 :
1. 필요 패키지 설치
2. 서버 키 생성 (`fullchain.crt`, `server.key`)
3. `fullchain.crt` 인증서를 클라이언트에게 전달
4. 클라이언트 키 생성, Qt 리소스에 추가
5. `make`
6. `./server` (TCP, RTSP 서버)를 먼저 실행
7. `./metadata/control` (메타데이터, 감지 처리 서버)를 새로운 터미널 열어서 실행

### .env 내용
```
# 공통 인증 정보
USERNAME=
PASSWORD=
HOST=<IP address>

# API 쿠키 설정
TRACKID=0842ca6f0d90294ea7de995c40a4aac6

# RTSP 설정
RTSP_PORT=554
RTSP_PATH=/0/onvif/profile2/media.smp

# DB 경로
DB_FILE=

```