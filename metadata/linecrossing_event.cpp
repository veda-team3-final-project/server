#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <regex>

void metadata_thread() {
    const std::string cmd =
        "ffmpeg -i rtsp://admin:admin123@192.168.0.46:554/0/onvif/profile2/media.smp "
        "-map 0:1 -f data - 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "[ERROR] Failed to open ffmpeg pipe" << std::endl;
        return;
    }

    constexpr int BUFFER_SIZE = 8192;
    char buffer[BUFFER_SIZE];
    std::string xml_data;

    // 일반 문자열 리터럴로 변경
    std::regex time_regex("UtcTime=\"([^\"]+)\"");
    std::regex id_regex("Name=\"ObjectId\"\\s+Value=\"([^\"]*)\"");
    std::regex action_regex("Name=\"Action\"\\s+Value=\"([^\"]*)\"");
    std::regex state_regex("Name=\"State\"\\s+Value=\"(true|false)\"");
    std::regex rule_regex("Name=\"RuleName\"\\s+Value=\"([^\"]+)\"");

    while (fgets(buffer, BUFFER_SIZE, pipe)) {
        xml_data += buffer;

        if (xml_data.find("</tt:Message>") != std::string::npos) {
            if (xml_data.find("LineCrossing") != std::string::npos) {
                std::smatch match_time, match_id, match_action, match_state, match_rule;
                std::string time_str = "N/A";
                std::string id_str = "N/A";
                std::string action_str = "N/A";
                std::string rule_str = "N/A";

                if (std::regex_search(xml_data, match_state, state_regex) &&
                    match_state[1] == "true") {

                    if (std::regex_search(xml_data, match_time, time_regex)) {
                        time_str = match_time[1];
                    }
                    if (std::regex_search(xml_data, match_id, id_regex)) {
                        id_str = match_id[1];
                    }
                    if (std::regex_search(xml_data, match_action, action_regex)) {
                        action_str = match_action[1];
                    }
                    if (std::regex_search(xml_data, match_rule, rule_regex)) {
                        rule_str = match_rule[1];
                    }

                    std::cout << "[" << time_str << "] "
                              << "[ID: " << id_str << "] "
                              << "[Action: " << action_str << "] "
                              << "[Line: " << rule_str << "]" << std::endl;
                }
            }
            xml_data.clear();
        }
    }

    pclose(pipe);
}

int main() {
    metadata_thread();
    return 0;
}
