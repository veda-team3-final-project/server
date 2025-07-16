
// 서버 main
// 이 코드에 함수 세부 구현 작성 X, 파일 나눠서 작성


#include "rtsp_server.hpp"
#include "tcp_server.hpp"
#include "metadata/handler.cpp"
#include "db_management.hpp"

#include <thread>
#include <fstream>

using namespace std;

int main(int argc, char *argv[]){

    /////////////////////

    // thread rtsp_run_thread(rtsp_run,argc,argv);

    thread tcp_run_thread(tcp_run);

    // 지연이 metadata 서버 때문은 아님. tcp 내부 처리 문제인듯
    // thread metadata_run_thread(metadata_thread);

    // rtsp_run_thread.join();
    tcp_run_thread.join();
    // metadata_run_thread.join();

    

    return 0;
}