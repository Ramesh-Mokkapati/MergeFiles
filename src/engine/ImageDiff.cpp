// ImageDiff.cpp - WIC decoding/encoding and the pixel comparison.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincodec.h>

#include "ImageDiff.h"

#include <algorithm>
#include <cstdlib>
#include <cwctype>

#include "Encoding.h"

namespace mf
{
    namespace
    {
        // COM for the calling thread, released when the scope ends (if this call initialized it).
        class ComScope
        {
        public:
            ComScope()
            {
                const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                m_release = SUCCEEDED(hr);
            }
            ~ComScope()
            {
                if (m_release)
                    CoUninitialize();
            }
            ComScope(const ComScope&) = delete;
            ComScope& operator=(const ComScope&) = delete;

        private:
            bool m_release = false;
        };

        template <typename T>
        class ComPtr
        {
        public:
            ComPtr() = default;
            ~ComPtr()
            {
                if (m_ptr)
                    m_ptr->Release();
            }
            ComPtr(const ComPtr&) = delete;
            ComPtr& operator=(const ComPtr&) = delete;
            T** operator&()
            {
                return &m_ptr;
            }
            T* operator->() const
            {
                return m_ptr;
            }
            T* Get() const
            {
                return m_ptr;
            }

        private:
            T* m_ptr = nullptr;
        };

        std::wstring HrText(HRESULT hr)
        {
            return LastErrorText(static_cast<unsigned long>(hr));
        }

        bool CreateFactory(ComPtr<IWICImagingFactory>& factory, std::wstring& error)
        {
            const HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
            if (FAILED(hr))
                error = L"Windows Imaging Component is not available: " + HrText(hr);
            return SUCCEEDED(hr);
        }
    }

    bool IsImageFileName(const std::wstring& path)
    {
        const size_t dot = path.find_last_of(L'.');
        if (dot == std::wstring::npos)
            return false;
        std::wstring ext = path.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        static const wchar_t* extensions[] = { L"png", L"jpg", L"jpeg", L"jpe", L"bmp", L"dib", L"gif", L"tif", L"tiff", L"ico", L"cur", L"jxr", L"wdp", L"webp", L"heic", L"dds" };
        for (const wchar_t* candidate : extensions)
        {
            if (ext == candidate)
                return true;
        }
        return false;
    }

    bool LoadImageFile(const std::wstring& path, Image& image, std::wstring& error)
    {
        ComScope com;
        ComPtr<IWICImagingFactory> factory;
        if (!CreateFactory(factory, error))
            return false;
        ComPtr<IWICBitmapDecoder> decoder;
        HRESULT hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(hr))
        {
            error = L"Cannot open the image: " + HrText(hr);
            return false;
        }
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        UINT width = 0, height = 0;
        hr = decoder->GetFrame(0, &frame);
        if (SUCCEEDED(hr))
            hr = factory->CreateFormatConverter(&converter);
        if (SUCCEEDED(hr))
            hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (SUCCEEDED(hr))
            hr = converter->GetSize(&width, &height);
        if (SUCCEEDED(hr) && (width == 0 || height == 0 || static_cast<unsigned long long>(width) * height > 400000000ull))
        {
            error = L"The image is empty or too large.";
            return false;
        }
        if (SUCCEEDED(hr))
        {
            image.width = static_cast<int>(width);
            image.height = static_cast<int>(height);
            image.pixels.assign(static_cast<size_t>(width) * height, 0);
            hr = converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(image.pixels.size() * 4), reinterpret_cast<BYTE*>(image.pixels.data()));
        }
        if (FAILED(hr))
        {
            error = L"Cannot decode the image: " + HrText(hr);
            image = Image();
            return false;
        }
        return true;
    }

    bool SaveImagePng(const std::wstring& path, const Image& image, std::wstring& error)
    {
        ComScope com;
        ComPtr<IWICImagingFactory> factory;
        if (!CreateFactory(factory, error))
            return false;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        HRESULT hr = factory->CreateStream(&stream);
        if (SUCCEEDED(hr))
            hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
        if (SUCCEEDED(hr))
            hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
        if (SUCCEEDED(hr))
            hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (SUCCEEDED(hr))
            hr = encoder->CreateNewFrame(&frame, nullptr);
        if (SUCCEEDED(hr))
            hr = frame->Initialize(nullptr);
        if (SUCCEEDED(hr))
            hr = frame->SetSize(static_cast<UINT>(image.width), static_cast<UINT>(image.height));
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(hr))
            hr = frame->SetPixelFormat(&format);
        if (SUCCEEDED(hr))
            hr = frame->WritePixels(static_cast<UINT>(image.height), static_cast<UINT>(image.width) * 4, static_cast<UINT>(image.pixels.size() * 4),
                reinterpret_cast<BYTE*>(const_cast<uint32_t*>(image.pixels.data())));
        if (SUCCEEDED(hr))
            hr = frame->Commit();
        if (SUCCEEDED(hr))
            hr = encoder->Commit();
        if (FAILED(hr))
            error = L"Cannot save the image: " + HrText(hr);
        return SUCCEEDED(hr);
    }

    ImageDiff DiffImages(const Image& a, const Image& b, int tolerance)
    {
        ImageDiff diff;
        diff.width = (std::max)(a.width, b.width);
        diff.height = (std::max)(a.height, b.height);
        diff.sameSize = a.width == b.width && a.height == b.height;
        diff.mask.assign(static_cast<size_t>(diff.width) * static_cast<size_t>(diff.height), 0);
        for (int y = 0; y < diff.height; ++y)
        {
            for (int x = 0; x < diff.width; ++x)
            {
                const bool inA = x < a.width && y < a.height;
                const bool inB = x < b.width && y < b.height;
                bool different = inA != inB;
                if (inA && inB)
                {
                    const uint32_t pa = a.At(x, y);
                    const uint32_t pb = b.At(x, y);
                    if (pa != pb)
                    {
                        int delta = 0;
                        for (int shift = 0; shift < 32; shift += 8)
                            delta = (std::max)(delta, std::abs(static_cast<int>((pa >> shift) & 0xFF) - static_cast<int>((pb >> shift) & 0xFF)));
                        diff.maxDelta = (std::max)(diff.maxDelta, delta);
                        different = delta > tolerance;
                    }
                }
                if (different)
                {
                    diff.mask[static_cast<size_t>(y) * static_cast<size_t>(diff.width) + static_cast<size_t>(x)] = 1;
                    ++diff.differing;
                }
            }
        }

        // Regions: 32x32 tiles that contain differences, merged with touching tiles (flood fill).
        const int tile = 32;
        const int tilesX = (diff.width + tile - 1) / tile;
        const int tilesY = (diff.height + tile - 1) / tile;
        std::vector<char> marked(static_cast<size_t>(tilesX) * static_cast<size_t>(tilesY), 0);
        for (int y = 0; y < diff.height; ++y)
        {
            for (int x = 0; x < diff.width; ++x)
            {
                if (diff.mask[static_cast<size_t>(y) * static_cast<size_t>(diff.width) + static_cast<size_t>(x)])
                    marked[static_cast<size_t>(y / tile) * static_cast<size_t>(tilesX) + static_cast<size_t>(x / tile)] = 1;
            }
        }
        std::vector<char> seen(marked.size(), 0);
        for (int ty = 0; ty < tilesY; ++ty)
        {
            for (int tx = 0; tx < tilesX; ++tx)
            {
                const size_t index = static_cast<size_t>(ty) * static_cast<size_t>(tilesX) + static_cast<size_t>(tx);
                if (!marked[index] || seen[index])
                    continue;
                ImageDiff::Region region = { tx, ty, tx, ty };
                std::vector<std::pair<int, int>> stack = { { tx, ty } };
                seen[index] = 1;
                while (!stack.empty())
                {
                    const auto [cx, cy] = stack.back();
                    stack.pop_back();
                    region.left = (std::min)(region.left, cx);
                    region.right = (std::max)(region.right, cx);
                    region.top = (std::min)(region.top, cy);
                    region.bottom = (std::max)(region.bottom, cy);
                    for (int dy = -1; dy <= 1; ++dy)
                    {
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            const int nx = cx + dx, ny = cy + dy;
                            if (nx < 0 || ny < 0 || nx >= tilesX || ny >= tilesY)
                                continue;
                            const size_t next = static_cast<size_t>(ny) * static_cast<size_t>(tilesX) + static_cast<size_t>(nx);
                            if (marked[next] && !seen[next])
                            {
                                seen[next] = 1;
                                stack.emplace_back(nx, ny);
                            }
                        }
                    }
                }
                diff.regions.push_back({ region.left * tile, region.top * tile, (std::min)(diff.width, (region.right + 1) * tile),
                    (std::min)(diff.height, (region.bottom + 1) * tile) });
            }
        }
        // Top to bottom, then left to right.
        std::sort(diff.regions.begin(), diff.regions.end(), [](const ImageDiff::Region& p, const ImageDiff::Region& q) {
            return p.top != q.top ? p.top < q.top : p.left < q.left;
        });
        return diff;
    }

    Image MakeDifferenceImage(const Image& base, const ImageDiff& diff, uint32_t highlight)
    {
        Image result;
        result.width = diff.width;
        result.height = diff.height;
        result.pixels.assign(static_cast<size_t>(diff.width) * static_cast<size_t>(diff.height), 0xFFFFFFFF);
        for (int y = 0; y < diff.height; ++y)
        {
            for (int x = 0; x < diff.width; ++x)
            {
                const size_t index = static_cast<size_t>(y) * static_cast<size_t>(diff.width) + static_cast<size_t>(x);
                if (diff.mask[index])
                {
                    result.pixels[index] = highlight;
                    continue;
                }
                if (x >= base.width || y >= base.height)
                    continue;
                // Grey, faded towards white so the highlights stand out.
                const uint32_t p = base.At(x, y);
                const int alpha = static_cast<int>(p >> 24);
                const int grey = (static_cast<int>((p >> 16) & 0xFF) * 30 + static_cast<int>((p >> 8) & 0xFF) * 59 + static_cast<int>(p & 0xFF) * 11) / 100;
                const int over = 255 - (255 - grey) * alpha / 255;  // composited on white
                const int faded = 255 - (255 - over) * 35 / 100;
                result.pixels[index] = 0xFF000000u | (static_cast<uint32_t>(faded) << 16) | (static_cast<uint32_t>(faded) << 8) | static_cast<uint32_t>(faded);
            }
        }
        return result;
    }
}
