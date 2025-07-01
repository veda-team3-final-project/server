CXX = g++
CXXFLAGS = -Wall -std=c++11 $(shell pkg-config --cflags gstreamer-rtsp-server-1.0 gstreamer-1.0 glib-2.0)
LDFLAGS = $(shell pkg-config --libs gstreamer-rtsp-server-1.0 gstreamer-1.0 glib-2.0)

all: server

clean:
	rm -f *.o server

server: server.o rtsp_server.o tcp_server.o
	$(CXX) server.o rtsp_server.o tcp_server.o -o server $(LDFLAGS) -pthread

server.o: server.cpp
	$(CXX) -c server.cpp $(CXXFLAGS)

rtsp_server.o: rtsp_server.cpp
	$(CXX) -c rtsp_server.cpp $(CXXFLAGS)

tcp_server.o: tcp_server.cpp
	$(CXX) -c tcp_server.cpp