#include <iostream>
#include <string>
#include <regex>
#include <chrono>
#include <thread>
#include <cstdlib>  // getenv

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

std::string get_rtsp_url() {
    const char* user = std::getenv("RTSP_USER");
    const char* pass = std::getenv("RTSP_PASS");
    const char* host = std::getenv("RTSP_HOST");
    const char* port = std::getenv("RTSP_PORT");
    const char* path = std::getenv("RTSP_PATH");

    if (!user || !pass || !host || !port || !path) {
        std::cerr << "[ERROR] Missing one or more RTSP environment variables." << std::endl;
        exit(EXIT_FAILURE);
    }

    return "rtsp://" + std::string(user) + ":" + std::string(pass) + "@" +
           std::string(host) + ":" + std::string(port) + std::string(path);
}

int main() {
    std::string rtsp_url = get_rtsp_url();

    avformat_network_init();

    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, rtsp_url.c_str(), nullptr, nullptr) != 0) {
        std::cerr << "Failed to open RTSP stream" << std::endl;
        return -1;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        std::cerr << "Failed to find stream info" << std::endl;
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    int metadata_stream_index = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i) {
        AVCodecParameters* codecpar = fmt_ctx->streams[i]->codecpar;
        if (codecpar->codec_type == AVMEDIA_TYPE_DATA) {
            metadata_stream_index = i;
            break;
        }
    }

    if (metadata_stream_index == -1) {
        std::cerr << "No metadata (AVMEDIA_TYPE_DATA) stream found" << std::endl;
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    AVPacket pkt;
    auto last_output_time = std::chrono::steady_clock::now();
    std::string buffer;

    while (av_read_frame(fmt_ctx, &pkt) >= 0) {
        if (pkt.stream_index == metadata_stream_index) {
            std::string xml(reinterpret_cast<char*>(pkt.data), pkt.size);
            buffer += xml;

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_output_time);
            if (elapsed.count() >= 500) {
                std::smatch match;
                std::regex object_pattern("<tt:Object ObjectId=\\\"(\\d+)\\\">[\\s\\S]*?<tt:BoundingBox left=\\\"(\\d+.?\\d*)\\\" top=\\\"(\\d+.?\\d*)\\\" right=\\\"(\\d+.?\\d*)\\\" bottom=\\\"(\\d+.?\\d*)\\\"[\\s\\S]*?<tt:Type(?: Likelihood=\\\"(\\d+.?\\d*)\\\")?>([^<]+)</tt:Type>", std::regex::icase);

                auto begin = buffer.cbegin();
                while (std::regex_search(begin, buffer.cend(), match, object_pattern)) {
                    std::string object_id = match[1];
                    std::string left = match[2];
                    std::string top = match[3];
                    std::string right = match[4];
                    std::string bottom = match[5];
                    std::string conf = match[6];
                    std::string type = match[7];

                    std::cout << "[Type=" << type << "], [ObjectId=" << object_id
                              << "], [BBox=(" << left << ", " << top << ", " << right << ", " << bottom
                              << ")], [conf=" << (conf.empty() ? "N/A" : conf) << "]" << std::endl;

                    begin = match.suffix().first;
                }

                std::regex event_pattern("<tt:SimpleItem Name=\\\"(\\w+)\\\" Value=\\\"(\\d+)\\\"[\\s\\S]*?</tt:Source>");
                auto event_begin = buffer.cbegin();
                while (std::regex_search(event_begin, buffer.cend(), match, event_pattern)) {
                    std::string event_name = match[1];
                    std::string event_id = match[2];

                    std::cout << "[EventName=Event], [Type=" << event_name << "], [ObjectId=" << event_id << "]" << std::endl;

                    event_begin = match.suffix().first;
                }

                buffer.clear();
                last_output_time = now;
            }
        }
        av_packet_unref(&pkt);
    }

    avformat_close_input(&fmt_ctx);
    avformat_network_deinit();
    return 0;
}
