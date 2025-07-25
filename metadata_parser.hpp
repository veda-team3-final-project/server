#ifndef METADATA_PARSER_HPP
#define METADATA_PARSER_HPP

#include <vector>
#include <string>
#include <mutex>
#include <cstring>
#include <atomic>
#include "server_bbox.hpp"

extern std::vector<ServerBBox> latest_bboxes;
extern std::mutex bbox_mutex;

void parse_metadata();

#endif // METADATA_PARSER_HPP
