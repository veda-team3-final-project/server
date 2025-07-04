#include <opencv2/opencv.hpp>
#include <iostream>

int main() {
    // Get OpenCV build information
    std::string buildInfo = cv::getBuildInformation();
    
    // Print to console
    std::cout << "===== OpenCV Build Information =====" << std::endl;
    std::cout << buildInfo << std::endl;
    std::cout << "====================================" << std::endl;
    
    // Check FFmpeg support
    if (buildInfo.find("FFMPEG: YES") != std::string::npos) {
        std::cout << "\nFFmpeg support: YES" << std::endl;
    } else if (buildInfo.find("FFMPEG: NO") != std::string::npos) {
        std::cout << "\nFFmpeg support: NO" << std::endl;
        std::cerr << "Warning: FFmpeg support is required!" << std::endl;
    } else {
        std::cout << "\nFFmpeg support information not found" << std::endl;
    }
    
    // Print important additional information
    if (buildInfo.find("Video I/O:") != std::string::npos) {
        size_t pos = buildInfo.find("Video I/O:");
        size_t end = buildInfo.find("\n", pos);
        std::cout << "\n[Video I/O section]\n" 
                  << buildInfo.substr(pos, end - pos) << std::endl;
    }
    
    return 0;
}