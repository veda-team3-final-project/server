<<<<<<< HEAD
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
=======
# Compiler
CXX = g++
CXXFLAGS = -std=c++17 -O2

# OpenCV pkg-config
PKG_CFLAGS = `pkg-config --cflags opencv4`
PKG_LIBS = `pkg-config --libs opencv4`

# FFmpeg flags
FFMPEG_FLAGS = -lavformat -lavcodec -lavutil

# Targets
TARGETS = metadata_test rtsp_overlay
SCRIPTS = run_metadata_test.sh run_rtsp_overlay.sh

all: $(TARGETS) $(SCRIPTS)

metadata_test: metadata_test.cpp
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) metadata_test.cpp -o metadata_test $(FFMPEG_FLAGS)

rtsp_overlay: rtsp_overlay.cpp
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) rtsp_overlay.cpp -o rtsp_overlay $(PKG_LIBS)

run_metadata_test.sh: metadata_test
	echo '#!/bin/bash' > $@
	echo 'set -a' >> $@
	echo 'source .env' >> $@
	echo 'set +a' >> $@
	echo './metadata_test' >> $@
	chmod +x $@

run_rtsp_overlay.sh: rtsp_overlay
	echo '#!/bin/bash' > $@
	echo 'set -a' >> $@
	echo 'source .env' >> $@
	echo 'set +a' >> $@
	echo './rtsp_overlay' >> $@
	chmod +x $@

clean:
	rm -f $(TARGETS) $(SCRIPTS)
>>>>>>> 294cd34 (테스트 코드 업로드)
