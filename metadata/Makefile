# Compiler
CXX = g++
CXXFLAGS = -std=c++17 -O2

# OpenCV pkg-config
PKG_CFLAGS = `pkg-config --cflags opencv4`
PKG_LIBS = `pkg-config --libs opencv4`

# FFmpeg flags
FFMPEG_FLAGS = -lavformat -lavcodec -lavutil

# Targets
TARGETS = metadata_test rtsp_overlay detect_vehical_data
SCRIPTS = run_metadata_test.sh run_rtsp_overlay.sh run_detect_vehical_data.sh

all: $(TARGETS) $(SCRIPTS)

metadata_test: metadata_test.cpp
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) metadata_test.cpp -o metadata_test $(FFMPEG_FLAGS)

rtsp_overlay: rtsp_overlay.cpp
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) rtsp_overlay.cpp -o rtsp_overlay $(PKG_LIBS)

detect_vehical_data: detect_vehical_data.cpp
	$(CXX) $(CXXFLAGS) detect_vehical_data.cpp -o detect_vehical_data $(FFMPEG_FLAGS)

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

run_detect_vehical_data.sh: detect_vehical_data
	echo '#!/bin/bash' > $@
	echo 'set -a' >> $@
	echo 'source .env' >> $@
	echo 'set +a' >> $@
	echo './detect_vehical_data' >> $@
	chmod +x $@

clean:
	rm -f $(TARGETS) $(SCRIPTS)
