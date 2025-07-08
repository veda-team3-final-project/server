// RTSP 서버 구동 모듈

#pragma once

#include <iostream>
#include <string>

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

using namespace std;

void rtsp_run(int argc, char *argv[]);