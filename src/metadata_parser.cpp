#include "metadata_parser.hpp"
#include <iostream>
#include <regex>
#include <cstdio>
#include "config_manager.hpp"

// tcp_server.hpp에서 선언된 update_bbox_buffer 함수 사용
void update_bbox_buffer(const std::vector<ServerBBox>& new_bboxes);

using namespace std;

// Use ServerBBox for latest_bboxes
vector<ServerBBox> latest_bboxes;
mutex bbox_mutex;
atomic<bool> parser_running(false);

void start_metadata_parser() {
    parser_running = true;
}

void stop_metadata_parser() {
    parser_running = false;
}

void parse_metadata() {
    if (!parser_running) {
        return;
    }

    // 설정 로드 (이미 로드되어 있으면 무시됨)
    if (!load_all_config()) {
        cerr << "[Metadata] 설정 로드 실패" << endl;
        return;
    }

    // 설정에서 RTSP URL 가져오기
    string rtsp_url = get_rtsp_url();
    string cmd = "ffmpeg -i " + rtsp_url + " -map 0:1 -f data -";
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        cerr << "[Metadata] ffmpeg 파이프 실행 실패" << endl;
        return;
    }

    constexpr int BUFFER_SIZE = 8192;
    char buffer[BUFFER_SIZE];
    string xml_buffer;

    // Updated regex for better group capturing
    std::regex object_regex("<tt:Object ObjectId=\"(\\d+)\">.*?<tt:BoundingBox left=\"(\\d+\\.?\\d*)\" top=\"(\\d+\\.?\\d*)\" right=\"(\\d+\\.?\\d*)\" bottom=\"(\\d+\\.?\\d*)\"/>(.*?)</tt:Object>");
    std::regex class_regex("<tt:ClassCandidate>\\s*<tt:Type>(\\w+)</tt:Type>\\s*<tt:Likelihood>([\\d\\.]+)</tt:Likelihood>");

    cout << "[MetadataParser] Started parsing metadata..." << endl;
    
    while (parser_running && !feof(pipe)) {
        size_t bytes = fread(buffer, 1, BUFFER_SIZE - 1, pipe);
        if (bytes <= 0) continue;
        buffer[bytes] = '\0';
        xml_buffer += buffer;
        
        cout << "[Debug] Read " << bytes << " bytes from ffmpeg pipe" << endl;

        size_t end_pos;
        while (parser_running && (end_pos = xml_buffer.find("</tt:MetadataStream>")) != string::npos) {
            string packet = xml_buffer.substr(0, end_pos);
            xml_buffer.erase(0, end_pos + strlen("</tt:MetadataStream>"));
            
            cout << "[Debug] Found MetadataStream packet, length: " << packet.length() << endl;
            cout << "[Debug] Packet content (first 200 chars): " << packet.substr(0, 200) << "..." << endl;

            vector<ServerBBox> parsed_boxes;
            sregex_iterator iter(packet.begin(), packet.end(), object_regex);
            sregex_iterator end;
            
            cout << "[Debug] Starting regex matching..." << endl;

            for (; iter != end; ++iter) {
                cout << "[Debug] Found Object match! ObjectId: " << (*iter)[1] << endl;
                ServerBBox box;
                // Corrected group indices based on the new regex
                box.object_id = stoi((*iter)[1]);
                
                // Extract coordinates and assign directly to ServerBBox members
                box.left = static_cast<int>(stof((*iter)[2]));
                box.top = static_cast<int>(stof((*iter)[3]));
                box.right = static_cast<int>(stof((*iter)[4]));
                box.bottom = static_cast<int>(stof((*iter)[5]));
                
                cout << "[Debug] BBox coords: left=" << box.left << ", top=" << box.top 
                     << ", right=" << box.right << ", bottom=" << box.bottom << endl;

                // Extract the inner content for class parsing
                string inner_content = (*iter)[6]; // Group 6 is the inner content

                smatch class_match;
                if (regex_search(inner_content, class_match, class_regex)) {
                    box.type = class_match[1]; // Use std::string directly
                    box.confidence = stof(class_match[2]);
                    cout << "[Debug] Found class info: type=" << box.type << ", confidence=" << box.confidence << endl;
                } else {
                    // Handle cases where class info might be missing or not matched
                    box.type = "Unknown";
                    box.confidence = 0.0f;
                    cout << "[Debug] No class info found, using defaults" << endl;
                }

                parsed_boxes.push_back(box);
                cout << "[Debug] Added box to parsed_boxes. Total count: " << parsed_boxes.size() << endl;
            }
            
            cout << "[Debug] Finished processing packet. Total boxes found: " << parsed_boxes.size() << endl;

            // 항상 버퍼에 데이터 추가 (빈 박스 포함)
            update_bbox_buffer(parsed_boxes);
            if (parsed_boxes.empty()) {
                cout << "[MetadataParser] Added empty box data to buffer (no objects detected)." << endl;
            } else {
                cout << "[MetadataParser] Added " << parsed_boxes.size() << " boxes to buffer." << endl;
            }
            
            // 기존 latest_bboxes도 호환성을 위해 유지
            lock_guard<mutex> lock(bbox_mutex);
            latest_bboxes = move(parsed_boxes);
        }
    }

    pclose(pipe);
}