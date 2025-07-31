// RTSP 송수신 모듈 of 서버 Pi
// 엣지 디바이스 Pi로부터 영상 수신, 클라이언트 Windows로 영상 송신
// 이 코드의 RTSP 서버 주소 : rtsp://<ip주소>:8554/retransmit

#include "rtsp_server.hpp"
#include "config_manager.hpp"

#define CCTV_RTSP_PORT "8554"
#define CCTV_MOUNT_POINT "/original"
#define CCTV_MOUNT_POINT_ALT "/retransmit"  // 기존 클라이언트 호환성을 위한 대체 경로
#define NIGHT_CCTV_MOUNT_POINT "/night"

void rtsp_run(int argc, char *argv[]) {
  // 설정 로드
  if (!load_all_config()) {
    cerr << "[ERROR] 설정 로드 실패" << endl;
    return;
  }
  
  gchar *port = (gchar *)CCTV_RTSP_PORT;
  // GStreamer 초기화 (초기화 실패시 에러 발생시킴)
  GError *error = NULL;
  gboolean initialized = gst_init_check(&argc, &argv, &error);
  if (!initialized) {
    g_printerr("GStreamer 초기화 실패: %s\n", error->message);
    g_error_free(error);
    return;
  }

  // RTSP 서버 생성
  GstRTSPServer *server = gst_rtsp_server_new();
  g_object_set(server, "service", port, NULL);

  // RTSP 마운트 포인트 획득
  // 마운트 포인트 : rtsp://<ip주소>:<포트번호>/<이 부분>r
  GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server);

  // RTSP 미디어 팩토리 생성
  // 미디어 팩토리 : 클라이언트의 요청을 받으면 실제 미디어 스트림을 만들어주는
  // 요소
  GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
  // RTSP 수신 & 송신 파이프라인 생성
  string rtsp_url = get_rtsp_url();
  cout << "[INFO] Edge device RTSP URL: " << rtsp_url << endl;
  
  string pipeline_description = 
      "rtspsrc location=" + rtsp_url + " latency=0 ! "  // 엣지 디바이스 Pi 주소 (.env에서 로드)
      "rtph264depay ! "              // RTP 패킷 -> H.264 비디오 데이터
                                     // 추출(depacketize)
      "queue ! "                     // 버퍼링을 위한 큐 추가
      "h264parse ! "                 // H.264 비디오 데이터 파싱
      "rtph264pay name=pay0 pt=96 config-interval=1";  // 비디오 데이터 -> RTP 패킷화
  
  cout << "[INFO] Pipeline description: " << pipeline_description << endl;
  gst_rtsp_media_factory_set_launch(factory, pipeline_description.c_str());

  // 야간 영상 전용 파이프라인
  GstRTSPMediaFactory *factory2 = gst_rtsp_media_factory_new();
  string pipeline_description2 =
  "rtspsrc location=" + rtsp_url + " latency=0 ! "
  "rtph264depay ! "
  "queue ! "
  "h264parse ! "
  "avdec_h264 ! "
  "queue ! "
  "videobalance brightness=0.3 contrast=1.5 ! "
  // "videogamma gamma=0.7 ! " // 현재 버전에서 지원안함
  // "unsharp ! " // 현재 버전에서 지원안함
  "x264enc tune=zerolatency ! "
  "h264parse ! "
  "rtph264pay name=pay0 pt=96";
  gst_rtsp_media_factory_set_launch(factory2, pipeline_description2.c_str());

  // RTSP 마운트 포인트 재설정 및 팩토리 추가
  gst_rtsp_mount_points_add_factory(mounts, CCTV_MOUNT_POINT, factory);
  gst_rtsp_mount_points_add_factory(mounts, CCTV_MOUNT_POINT_ALT, factory);  // 대체 경로 추가
  gst_rtsp_mount_points_add_factory(mounts, NIGHT_CCTV_MOUNT_POINT, factory2);
  g_object_unref(mounts);

  // RTSP 서버 시작
  if (gst_rtsp_server_attach(server, NULL) == 0) {
    cerr << "RTSP 서버 Attach 실패\n";
    return;
  }

  cout << "\nCCTV RTSP middle server is running on " << port << CCTV_MOUNT_POINT << " AND " << NIGHT_CCTV_MOUNT_POINT
       << "\n";

  // 메인 루프 실행
  GMainLoop *loop = g_main_loop_new(NULL, FALSE);
  g_main_loop_run(loop);

  g_main_loop_unref(loop);
  return;
}