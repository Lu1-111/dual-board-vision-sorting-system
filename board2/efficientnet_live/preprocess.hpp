#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace efficientnet {
// Decoded full-frame RGB888; caller must apply EXIF orientation before this call.
// No ROI cropping. Width/height are the full source image dimensions.
std::vector<uint8_t> letterboxRgb(const uint8_t* rgb,int width,int height,std::size_t stride);
std::vector<float> normalizeRgb(const std::vector<uint8_t>& rgb224);
std::vector<float> prepareRgb(const uint8_t* rgb,int width,int height,std::size_t stride);
#ifdef EFFICIENTNET_WITH_OPENCV
// Decodes JPEG/PNG using OpenCV, including its standard EXIF orientation handling.
std::vector<uint8_t> letterboxImageOpenCv(const std::string& path);
std::vector<float> prepareImageOpenCv(const std::string& path);
#endif
#ifdef EFFICIENTNET_WITH_QT
std::vector<float> prepareImageQt(const std::string& path);
#endif
}
