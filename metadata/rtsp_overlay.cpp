#include <opencv2/opencv.hpp>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <regex>
#include <iostream>

struct BBox {
    int object_id;
    float left;
    float top;
    float right;
    float bottom;
    float confidence;
    std::string type;
};

std::mutex bbox_mutex;
std::vector<BBox> bbox_list;

void metadata_thread() {
    const std::string cmd = \
        "ffmpeg -i rtsp://admin:admin123@192.168.0.46:554/0/onvif/profile2/media.smp -map 0:1 -f data -";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "Failed to open ffmpeg pipe" << std::endl;
        return;
    }

    constexpr int BUFFER_SIZE = 8192;
    char buffer[BUFFER_SIZE];
    std::string xml_data;

    std::regex object_regex(
        R"(<tt:Object ObjectId=\"(\d+)\">[\s\S]*?<tt:BoundingBox left=\"(\d+\.?\d*)\" top=\"(\d+\.?\d*)\" right=\"(\d+\.?\d*)\" bottom=\"(\d+\.?\d*)\"/>([\s\S]*?)</tt:Object>)"
    );
    std::regex class_regex(
        R"(<tt:ClassCandidate>\s*<tt:Type>(\w+)</tt:Type>\s*<tt:Likelihood>([\d\.]+)</tt:Likelihood>)"
    );

    while (!feof(pipe)) {
        size_t bytes_read = fread(buffer, 1, BUFFER_SIZE - 1, pipe);
        if (bytes_read <= 0) continue;
        buffer[bytes_read] = '\0';
        xml_data += buffer;

        size_t pos;
        while ((pos = xml_data.find("</tt:MetadataStream>")) != std::string::npos) {
            std::string one_packet = xml_data.substr(0, pos);
            xml_data.erase(0, pos + std::string("</tt:MetadataStream>").size());

            std::sregex_iterator iter(one_packet.begin(), one_packet.end(), object_regex);
            std::sregex_iterator end;

            std::lock_guard<std::mutex> lock(bbox_mutex);
            bbox_list.clear();

            for (; iter != end; ++iter) {
                BBox box;
                box.object_id = std::stoi((*iter)[1].str());
                box.left = std::stof((*iter)[2].str());
                box.top = std::stof((*iter)[3].str());
                box.right = std::stof((*iter)[4].str());
                box.bottom = std::stof((*iter)[5].str());

                std::string object_block = (*iter)[6].str();
                std::smatch class_match;
                if (std::regex_search(object_block, class_match, class_regex)) {
                    box.type = class_match[1];
                    box.confidence = std::stof(class_match[2]);
                } else {
                    box.type = "Unknown";
                    box.confidence = -1.0f;
                }

                bbox_list.push_back(box);
            }
        }
    }

    pclose(pipe);
}

void video_thread() {
    cv::VideoCapture cap("rtsp://admin:admin123@192.168.0.46:554/0/onvif/profile2/media.smp");
    if (!cap.isOpened()) {
        std::cerr << "Failed to open RTSP stream" << std::endl;
        return;
    }

    cv::Mat frame;
    while (true) {
        cap >> frame;
        if (frame.empty()) continue;

        std::vector<BBox> current_boxes;
        {
            std::lock_guard<std::mutex> lock(bbox_mutex);
            current_boxes = bbox_list;
        }

        for (const auto& box : current_boxes) {
            int scaled_left = static_cast<int>(box.left * frame.cols / 3840.0);
            int scaled_top = static_cast<int>(box.top * frame.rows / 2160.0);
            int scaled_right = static_cast<int>(box.right * frame.cols / 3840.0);
            int scaled_bottom = static_cast<int>(box.bottom * frame.rows / 2160.0);

            cv::rectangle(
                frame,
                cv::Point(scaled_left, scaled_top),
                cv::Point(scaled_right, scaled_bottom),
                cv::Scalar(0, 255, 0),
                2
            );

            char label[128];
            if (box.confidence >= 0)
                snprintf(label, sizeof(label), "[%d] %s (%.2f)", box.object_id, box.type.c_str(), box.confidence);
            else
                snprintf(label, sizeof(label), "[%d] %s (N/A)", box.object_id, box.type.c_str());

            cv::putText(frame, label, cv::Point(scaled_left, std::max(scaled_top - 5, 10)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
        }

        cv::imshow("RTSP + Metadata", frame);
        if (cv::waitKey(1) == 27) break;
    }
}

int main() {
    std::thread t1(metadata_thread);
    std::thread t2(video_thread);
    t1.join();
    t2.join();
    return 0;
}
