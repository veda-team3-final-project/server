
// 서버 main
// 이 코드에 함수 세부 구현 작성 X, 파일 나눠서 작성


#include "rtsp_server.hpp"
#include "tcp_server.hpp"
#include "metadata/handler.cpp"
#include "db_management.hpp"

#include <thread>
#include <fstream>
#include <opencv2/opencv.hpp>

using namespace std;

int main(int argc, char *argv[]){
    ////// DB Test
    // SQLite::Database db("server_log.db",SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    // cout << "데이터베이스 파일 'server_log.db'에 연결되었습니다.\n";

    // std::string image_path = "../test.jpg"; // 읽을 이미지 파일 경로

    // // 1. 파일 스트림 열기 (바이너리 모드)
    // std::ifstream file(image_path, std::ios::binary | std::ios::ate); // ate: 파일 끝에서 시작

    // if (!file.is_open()) {
    //     std::cerr << "Error: Could not open or find the image file: " << image_path << std::endl;
    //     return -1;
    // }

    // // 2. 파일 크기 얻기
    // // std::ios::ate 플래그 덕분에 파일 포인터가 이미 파일 끝에 있습니다.
    // std::streamsize file_size = file.tellg();
    // file.seekg(0, std::ios::beg); // 파일 포인터를 다시 파일 시작으로 이동

    // // 3. 파일 내용을 vector<unsigned char>로 읽기
    // std::vector<unsigned char> file_data(file_size);
    // if (file.read(reinterpret_cast<char*>(file_data.data()), file_size)) {
    //     std::cout << "Image file read successfully into vector<unsigned char>. Size: " << file_data.size() << " bytes." << std::endl;

    //     // 이제 file_data 벡터를 사용하여 원본 파일 바이트를 처리하거나 전송할 수 있습니다.
    //     // (예: TCP 소켓으로 전송)

    //     // 벡터의 일부 데이터를 출력하여 확인 (선택 사항)
    //     std::cout << "First 20 bytes of file_data: ";
    //     for (size_t i = 0; i < std::min((size_t)20, file_data.size()); ++i) {
    //         std::cout << std::hex << (int)file_data[i] << " ";
    //     }
    //     std::cout << std::dec << std::endl;

    // } else {
    //     std::cerr << "Error: Failed to read image file into vector." << std::endl;
    //     return -1;
    // }

    // file.close();

    // insert_data(db,file_data,"2025-07-08T01:10:10.037Z");

    // vector<LogData> vl = select_data_for_timestamp_range(db,"2025-07-08T00","2025-07-08T12");
    // for(auto ld : vl){
    //     cv::Mat decoded_image = cv::imdecode(ld.imageBlob, cv::IMREAD_COLOR);

    //     if (decoded_image.empty()) {
    //         std::cerr << "Error: Could not decode image from binary data." << std::endl;
    //         return -1;
    //     }
    
    //     // --- 3. 디코딩된 이미지를 화면에 표시 ---
    //     cv::imshow("Decoded Image", decoded_image);

    //     cv::waitKey(0);
    // }

    // delete_all_data(db);
    


    /////////////////////

    thread rtsp_run_thread(rtsp_run,argc,argv);

    thread tcp_run_thread(tcp_run);

    // 지연이 metadata 서버 때문은 아님. tcp 내부 처리 문제인듯
    thread metadata_run_thread(metadata_thread);

    rtsp_run_thread.join();
    tcp_run_thread.join();
    metadata_run_thread.join();

    

    return 0;
}