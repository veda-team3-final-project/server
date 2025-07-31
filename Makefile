CXX = g++
CXXFLAGS = -Wall -std=c++17 $(shell pkg-config --cflags gstreamer-rtsp-server-1.0 gstreamer-1.0 glib-2.0 libcurl) -I/usr/include/openssl -I./src/otp -I./src/otp/cotp -I./src/otp/QR-Code-generator
LDFLAGS = $(shell pkg-config --libs gstreamer-rtsp-server-1.0 gstreamer-1.0 glib-2.0 libcurl) -pthread -lSQLiteCpp -lsqlite3 -lssl -lcrypto -lsodium

OTP_SRC = $(wildcard src/otp/*.cpp)
OTP_DEPS_SRC = src/otp/QR-Code-generator/qrcodegen.cpp
OTP_OBJ = $(OTP_SRC:.cpp=.o) $(OTP_DEPS_SRC:.cpp=.o)

all: server metadata/control

clean:
	rm -f *.o src/*.o server metadata/control $(OTP_OBJ)

# TCP, RTSP 서버

server: server.o rtsp_server.o tcp_server.o db_management.o metadata_parser.o hash.o config_manager.o $(OTP_OBJ)
	$(CXX) server.o src/rtsp_server.o src/tcp_server.o src/db_management.o src/metadata_parser.o src/hash.o src/config_manager.o $(OTP_OBJ) -o server $(LDFLAGS)


server.o: server.cpp src/metadata_parser.hpp
	$(CXX) -c server.cpp $(CXXFLAGS)

rtsp_server.o: src/rtsp_server.cpp
	$(CXX) -c src/rtsp_server.cpp -o src/rtsp_server.o $(CXXFLAGS)

tcp_server.o: src/tcp_server.cpp src/metadata_parser.hpp
	$(CXX) -c src/tcp_server.cpp -o src/tcp_server.o $(CXXFLAGS)

db_management.o : src/db_management.cpp
	$(CXX) -c src/db_management.cpp -o src/db_management.o -std=c++17

metadata_parser.o: src/metadata_parser.cpp src/metadata_parser.hpp
	$(CXX) -c $< -o src/metadata_parser.o -std=c++17

hash.o: src/hash.cpp src/hash.hpp
	$(CXX) -c src/hash.cpp -o src/hash.o $(CXXFLAGS)

config_manager.o: src/config_manager.cpp src/config_manager.hpp
	$(CXX) -c src/config_manager.cpp -o src/config_manager.o -std=c++17

# 메타데이터, 감지 처리 서버

metadata/control: src/metadata/main_control.cpp src/metadata/board_control.cpp src/config_manager.o
	$(CXX) src/metadata/main_control.cpp src/metadata/board_control.cpp src/config_manager.o -o control -lSQLiteCpp -lsqlite3 --std=c++17