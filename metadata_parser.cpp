#include "metadata_parser.hpp"
#include <iostream>
#include <regex>
#include <cstdio>
#include "server_bbox.hpp" // Include the new server-side BBox definition

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

    const string cmd =
        "ffmpeg -i rtsp://admin:admin123@@192.168.0.137:554/0/onvif/profile2/media.smp "
        "-map 0:1 -f data -";
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

    while (parser_running && !feof(pipe)) {
        size_t bytes = fread(buffer, 1, BUFFER_SIZE - 1, pipe);
        if (bytes <= 0) continue;
        buffer[bytes] = '\0';
        xml_buffer += buffer;

        size_t end_pos;
        while (parser_running && (end_pos = xml_buffer.find("</tt:MetadataStream>")) != string::npos) {
            string packet = xml_buffer.substr(0, end_pos);
            xml_buffer.erase(0, end_pos + strlen("</tt:MetadataStream>"));

            vector<ServerBBox> parsed_boxes;
            sregex_iterator iter(packet.begin(), packet.end(), object_regex);
            sregex_iterator end;

            for (; iter != end; ++iter) {
                ServerBBox box;
                // Corrected group indices based on the new regex
                box.object_id = stoi((*iter)[1]);
                
                // Extract coordinates and assign directly to ServerBBox members
                box.left = static_cast<int>(stof((*iter)[2]));
                box.top = static_cast<int>(stof((*iter)[3]));
                box.right = static_cast<int>(stof((*iter)[4]));
                box.bottom = static_cast<int>(stof((*iter)[5]));

                // Extract the inner content for class parsing
                string inner_content = (*iter)[6]; // Group 6 is the inner content

                smatch class_match;
                if (regex_search(inner_content, class_match, class_regex)) {
                    box.type = class_match[1]; // Use std::string directly
                    box.confidence = stof(class_match[2]);
                } else {
                    // Handle cases where class info might be missing or not matched
                    box.type = "Unknown";
                    box.confidence = 0.0f;
                }

                parsed_boxes.push_back(box);
            }

            if (!parsed_boxes.empty()) {
                lock_guard<mutex> lock(bbox_mutex);
                latest_bboxes = move(parsed_boxes);
                // Use std::cout for server-side debugging
                cout << "[MetadataParser] Populated latest_bboxes with " << latest_bboxes.size() << " boxes." << endl;
            }
        }
    }

    pclose(pipe);
}