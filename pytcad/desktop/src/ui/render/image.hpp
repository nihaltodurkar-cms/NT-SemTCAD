// CPU images for screenshots and render goldens (N2a): tightly packed 8-bit BGRA, read back from a window's back
// buffer (WindowSurface::endFrame with a capture) and written/read as PNG through WIC.
#pragma once

#include <wincodec.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace tcad::ui {

struct Image {
    int width = 0, height = 0;
    std::vector<std::uint8_t> bgra;  // width * height * 4 bytes, row-major, no padding

    bool empty() const { return width <= 0 || height <= 0; }
    // 0xAARRGGBB of pixel (x, y)
    std::uint32_t pixel(int x, int y) const;
    bool operator==(const Image&) const = default;
};

struct ImageDiff {
    bool same_size = false;
    std::size_t differing_pixels = 0;
    int max_channel_delta = 0;
};
ImageDiff compareImages(const Image& a, const Image& b);

// Pixels that are not the colour of pixel (0,0): a "not blank" measure for smoke checks.
std::size_t nonBackgroundPixels(const Image& img);

std::expected<void, std::string> savePng(IWICImagingFactory* wic, const Image& img, const std::filesystem::path& path);
std::expected<Image, std::string> loadPng(IWICImagingFactory* wic, const std::filesystem::path& path);

}  // namespace tcad::ui
