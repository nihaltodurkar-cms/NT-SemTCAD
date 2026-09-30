#include "ui/render/image.hpp"

#include "ui/render/render_device.hpp"

#include <algorithm>
#include <cstdlib>

namespace tcad::ui {

std::uint32_t Image::pixel(int x, int y) const {
    const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
    return (std::uint32_t{bgra[i + 3]} << 24) | (std::uint32_t{bgra[i + 2]} << 16) | (std::uint32_t{bgra[i + 1]} << 8) |
           std::uint32_t{bgra[i]};
}

ImageDiff compareImages(const Image& a, const Image& b) {
    ImageDiff d;
    d.same_size = a.width == b.width && a.height == b.height && a.bgra.size() == b.bgra.size();
    if (!d.same_size) return d;
    for (std::size_t i = 0; i < a.bgra.size(); i += 4) {
        int m = 0;
        for (int c = 0; c < 4; ++c) m = std::max(m, std::abs(int{a.bgra[i + c]} - int{b.bgra[i + c]}));
        if (m) {
            ++d.differing_pixels;
            d.max_channel_delta = std::max(d.max_channel_delta, m);
        }
    }
    return d;
}

std::size_t nonBackgroundPixels(const Image& img) {
    if (img.empty()) return 0;
    const std::uint32_t bg = img.pixel(0, 0);
    std::size_t n = 0;
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) n += img.pixel(x, y) != bg;
    return n;
}

std::expected<void, std::string> savePng(IWICImagingFactory* wic, const Image& img, const std::filesystem::path& path) {
    if (img.empty()) return std::unexpected(std::string("savePng: empty image"));
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    ComPtr<IWICStream> stream;
    HRESULT hr = wic->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) return std::unexpected(hrError("open for writing", hr) + ": " + path.string());
    ComPtr<IWICBitmapEncoder> enc;
    hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    if (SUCCEEDED(hr)) hr = enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;
    if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(img.width, img.height);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
    if (SUCCEEDED(hr) && fmt != GUID_WICPixelFormat32bppBGRA) hr = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    if (SUCCEEDED(hr))
        hr = frame->WritePixels(img.height, img.width * 4, static_cast<UINT>(img.bgra.size()),
                                const_cast<BYTE*>(img.bgra.data()));
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = enc->Commit();
    if (FAILED(hr)) return std::unexpected(hrError("PNG encode", hr) + ": " + path.string());
    return {};
}

std::expected<Image, std::string> loadPng(IWICImagingFactory* wic, const std::filesystem::path& path) {
    ComPtr<IWICBitmapDecoder> dec;
    HRESULT hr = wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
    if (FAILED(hr)) return std::unexpected(hrError("open", hr) + ": " + path.string());
    ComPtr<IWICBitmapFrameDecode> frame;
    hr = dec->GetFrame(0, &frame);
    ComPtr<IWICFormatConverter> conv;
    if (SUCCEEDED(hr)) hr = wic->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr))
        hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                              WICBitmapPaletteTypeCustom);
    UINT w = 0, h = 0;
    if (SUCCEEDED(hr)) hr = conv->GetSize(&w, &h);
    Image img;
    if (SUCCEEDED(hr)) {
        img.width = static_cast<int>(w);
        img.height = static_cast<int>(h);
        img.bgra.resize(static_cast<std::size_t>(w) * h * 4);
        hr = conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(img.bgra.size()), img.bgra.data());
    }
    if (FAILED(hr)) return std::unexpected(hrError("PNG decode", hr) + ": " + path.string());
    return img;
}

}  // namespace tcad::ui
