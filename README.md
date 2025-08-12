# Server

## 요구사항

- Hanwha Vision PNO-A9081R 
- Raspberry Pi 4B 8GB(ARM64) / Debian Bookworm OS

에서 정상적으로 컴파일 및 실행됩니다. 

## 설치 가이드(수동)

코드를 직접 컴파일하여 사용할 수 있습니다.

#### 필요 패키지 설치 :
```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    pkg-config \
    libgstreamer1.0-dev \
    libgstrtspserver-1.0-dev \
    libsqlitecpp-dev \
    libsodium-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    sqlite3
```


컴파일 시에 SQLite 관련 에러가 발생할 수 있습니다. 그 경우에는 직접 SQLite를 빌드하여 사용하는 것을 권장합니다.

```bash
git clone https://github.com/SRombauts/SQLiteCpp
cd SQLiteCpp
mkdir build
cd build
cmake ..
```
```
make
make install
```

모든 패키지가 설치된 환경에서, 레포지토리를 Clone 합니다.
```
git clone https://github.com/veda-team3-final-project/server
cd server
```
이후 `make`를 이용하여 컴파일 할 수 있습니다.

```
# server/control 빌드
make 
# server만 빌드
make server
# control만 빌드
make metadata/control
```

## 설치 가이드(Docker)

Docker가 설치된 라즈베리 파이에서, 다음의 명령어로 이미지를 간단하게 설치할 수 있습니다.

```
docker pull ghcr.io/veda-team3-final-project/server:latest
```

이 프로젝트의 서버 실행은 8080 포트를 사용합니다. 그리고 아래의 단계에서 필요한 파일들을 손쉽게 생성하고 적용할 수 있도록 디렉토리를 연결하여 컨테이너를 실행하는 것이 좋습니다. 아래의 명령어로 컨테이너를 실행시킬 수 있습니다.

```
docker run -d -p 8080:8080 -v $(pwd):/app --name my-server-container server:latest
```

컨테이너를 실행시킨 후에는 다음의 명령어로 미리 컴파일된 server, control을 실행시킬 수 있습니다.

```
docker exec -it my-server-container bash
./server
./control
```

반드시 아래의 가이드에 따라 인증서 및 `.env` 파일을 생성하고 `/app`에 위치시킨 후 실행해야 합니다.

## 인증서 생성

### OpenSSL 인증서 생성 가이드

이 프로젝트는 보안을 위해 서버/클라이언트 간의 TCP 통신에 SSL을 적용하였습니다. 아래의 가이드를 따라 인증서를 생성합니다.

1. 키 발급 및 비밀번호 설정
```
openssl genpkey -algorithm RSA -out ca.key -aes256
```

2. 자체 서명 인증서 생성
```
openssl req -x509 -new -nodes -key ca.key -sha256 -days 3650 -out ca.crt
```

3. 개인 키 생성
```
openssl genpkey -algorithm RSA -out server.key
```

4. 설정 파일 생성

아래의 빈 필드를 채우고 openssl.cnf 파일을 생성합니다.
```
[req]
distinguished_name = req_distinguished_name
req_extensions = v3_req
prompt = no

[req_distinguished_name]
C = KR
ST = Seoul
L = Seoul
O = 
OU = 
CN = # 서버의 IP 주소 또는 도메인 이름

[v3_req]
keyUsage = keyEncipherment, dataEncipherment
extendedKeyUsage = serverAuth
subjectAltName = @alt_names

[alt_names]
IP.1 = # 서버의 IP 주소 또는 도메인 이름
# DNS.1 = my-server.local # 필요하다면 도메인 이름도 추가할 수 있습니다.
```

5. CSR 생성
```
openssl req -new -key server.key -out server.csr -config openssl.cnf
```

6. CSR 서명

1번의 과정에서 설정한 비밀번호를 입력하여 서명합니다.

```
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
-out server.crt -days 365 -sha256 -extfile openssl.cnf -extensions v3_req
```

7. 최종 풀체인 인증서 생성
```
cat server.crt ca.crt > fullchain.crt
```

### 실행 방법 :

위 과정을 모두 마친 후, `.env` 파일을 생성하여 다음의 내용을 기입합니다.

```
# 공통 인증 정보
USERNAME= # 한화비전 CCTV WEB UI에서 설정한 ID
PASSWORD= # 한화비전 CCTV WEB UI에서 설정한 PW
HOST= # 한화비전 CCTV에 할당된 IP

# API 쿠키 설정
TRACKID=0842ca6f0d90294ea7de995c40a4aac6

# RTSP 설정
RTSP_PORT=554
RTSP_PATH=/0/onvif/profile2/media.smp

# DB 경로
DB_FILE= # SQlite DB 파일 경로(경로에 존재하지 않을 경우에는 생성됨)

```

저장한 후,

```
./server
./control
```

을 통해 실행할 수 있습니다.

## 문서

이 레포지토리의 코드를 Doxygen을 통해 문서화 하였습니다.

아래의 링크를 통해 클래스, 함수의 구조 및 include 의존 그래프 등을 확인할 수 있습니다.

https://veda-team3-final-project.github.io/server/

