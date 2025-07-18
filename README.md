Linux Raspberry Pi

필요 패키지 설치 명령어 :
sudo apt update
sudo apt install libgstreamer1.0-dev libgstrtspserver-1.0-dev
sudo apt install libcurl4-openssl-dev

(Optional) 부가 패키지 설치 명령어 :
sudo apt install sqlite3 // sqlite db 수동 조작용

rtsp 접속을 위한 환경변수(.env) 파일 내용 

```
RTSP_USER=[카메라 ID]
RTSP_PASS=[카메라 PW]
RTSP_HOST=[카메라 IP]
RTSP_PORT=[포트번호]
RTSP_PATH=/0/onvif/profile2/media.smp
```


빌드 및 실행
```
make
```
이후 생성된 .sh 스크립트 파일 실행하여 코드 동작
