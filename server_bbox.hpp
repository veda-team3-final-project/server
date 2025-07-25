#ifndef SERVER_BBOX_HPP
#define SERVER_BBOX_HPP

#include <string>
#include <vector>
#include "json.hpp" // Assuming nlohmann/json is used for JSON parsing

// Server-side BBox structure (Qt-independent)
struct ServerBBox {
    int object_id;
    std::string type; // Use std::string instead of QString
    float confidence;
    // Use individual coordinates instead of QRect
    int left;
    int top;
    int right;
    int bottom;
};

// Server-side parseBBoxes function (Qt-independent)
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

#endif // SERVER_BBOX_HPP
