
// 서버 main
// 이 코드에 함수 세부 구현 작성 X, 파일 나눠서 작성


#include "rtsp_server.hpp"
#include "tcp_server.hpp"
#include "db_management.hpp"

#include <thread>

using namespace std;

int main(int argc, char *argv[]){
    SQLite::Database db("server_log.db",SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    cout << "데이터베이스 파일 'server_log.db'에 연결되었습니다.\n";

    thread rtsp_run_thread(rtsp_run,argc,argv);

    thread tcp_run_thread(tcp_run);

    rtsp_run_thread.join();
    tcp_run_thread.join();

    return 0;
}