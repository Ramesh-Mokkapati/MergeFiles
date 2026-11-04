// ImageDiff.h - loading images (PNG, JPEG, BMP, GIF, TIFF, ICO, ... through Windows Imaging
// Component) and comparing them pixel by pixel.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mf
{
    struct Image
    {
        int width = 0;
        int height = 0;
        std::vector<uint32_t> pixels;  // 0xAARRGGBB (BGRA in memory), top-down rows

        bool Empty() const
        {
            return width <= 0 || height <= 0;
        }
        uint32_t At(int x, int y) const
        {
            return pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
        }
    };

    // True for file names whose extension is an image format WIC usually decodes.
    bool IsImageFileName(const std::wstring& path);
    bool LoadImageFile(const std::wstring& path, Image& image, std::wstring& error);
    bool SaveImagePng(const std::wstring& path, const Image& image, std::wstring& error);

    struct ImageDiff
    {
        int width = 0;   // the larger of the two widths
        int height = 0;  // the larger of the two heights
        std::vector<uint8_t> mask;  // width*height: 1 where the pixels differ (or exist in one image only)
        uint64_t differing = 0;
        bool sameSize = false;
        int maxDelta = 0;  // largest channel difference found
        // Bounding boxes of the differing areas (in rows of 32x32 tiles, merged), for navigation.
        struct Region
        {
            int left, top, right, bottom;
        };
        std::vector<Region> regions;
    };

    // Pixels whose channels (including alpha) all differ by at most `tolerance` count as equal.
    ImageDiff DiffImages(const Image& a, const Image& b, int tolerance);

    // An image that shows the differences: `base` faded to grey, differing pixels in `highlight`.
    Image MakeDifferenceImage(const Image& base, const ImageDiff& diff, uint32_t highlight = 0xFFE0303C);
}
