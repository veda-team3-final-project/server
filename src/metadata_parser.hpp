#ifndef METADATA_PARSER_HPP
#define METADATA_PARSER_HPP

#include <vector>
#include <string>
#include <mutex>
#include <cstring>
#include <atomic>
#include "json.hpp"

// Server-side BBox structure
struct ServerBBox {
    int object_id;
    std::string type;
    float confidence;
    int left;
    int top;
    int right;
    int bottom;
};

// Server-side parseBBoxes function
inline std::vector<ServerBBox> parseServerBBoxes(const nlohmann::json& bboxArray) {
    std::vector<ServerBBox> boxes;
    for (const auto& value : bboxArray) {
        ServerBBox bbox;
        bbox.object_id = value.at("object_id").get<int>();
        bbox.type = value.at("type").get<std::string>();
        bbox.confidence = value.at("confidence").get<float>();
        bbox.left = value.at("left").get<int>();
        bbox.top = value.at("top").get<int>();
        bbox.right = value.at("right").get<int>();
        bbox.bottom = value.at("bottom").get<int>();
        boxes.push_back(bbox);
    }
    return boxes;
}

extern std::vector<ServerBBox> latest_bboxes;
extern std::mutex bbox_mutex;

void start_metadata_parser();

void stop_metadata_parser();

void parse_metadata();

#endif // METADATA_PARSER_HPP
