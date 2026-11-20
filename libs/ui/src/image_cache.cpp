// SVG files (lunasvg) decoded into ImGui textures, cached by file and size.
#include "agentdesk/ui/image_cache.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include <lunasvg/lunasvg.h>

#include "agentdesk/ui/imgui_adapter.hpp"

namespace agentdesk::ui {

namespace {

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return {};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Premultiplied ARGB (lunasvg) to straight RGBA (ImGui).
std::unique_ptr<unsigned char[]> to_rgba(const lunasvg::Bitmap& b) {
    const int w = b.width(), h = b.height(), stride = b.stride();
    auto out =
        std::make_unique<unsigned char[]>(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    const std::uint8_t* src = b.data();
    for (int y = 0; y < h; ++y) {
        const auto* row =
            reinterpret_cast<const std::uint32_t*>(src + static_cast<std::ptrdiff_t>(y) * stride);
        for (int x = 0; x < w; ++x) {
            const std::uint32_t p = row[x];
            const std::uint32_t a = p >> 24;
            std::uint32_t r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, bl = p & 0xff;
            if (a != 0 && a != 255) {
                r = r * 255 / a;
                g = g * 255 / a;
                bl = bl * 255 / a;
            }
            unsigned char* d =
                out.get() +
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4;
            d[0] = static_cast<unsigned char>(r);
            d[1] = static_cast<unsigned char>(g);
            d[2] = static_cast<unsigned char>(bl);
            d[3] = static_cast<unsigned char>(a);
        }
    }
    return out;
}

}  // namespace

ImageCache::~ImageCache() {
    for (auto& [k, e] : entries_)
        if (e.data)
            adapter::unregister_texture(e.data.get());
}

ImageCache::Image ImageCache::keep(const std::tuple<std::wstring, int, int, bool>& key, int w, int h,
                                   std::unique_ptr<unsigned char[]> rgba) {
    Entry e;
    e.data = std::make_unique<ImTextureData>();
    e.data->Create(ImTextureFormat_RGBA32, w, h);
    std::memcpy(e.data->GetPixels(), rgba.get(),
                static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    e.data->UseColors = true;
    e.data->UsedRect = {0, 0, static_cast<unsigned short>(w), static_cast<unsigned short>(h)};
    adapter::register_texture(e.data.get());
    e.image.tex = e.data->GetTexRef();
    e.image.w = w;
    e.image.h = h;
    const Image out = e.image;
    entries_[key] = std::move(e);
    return out;
}

ImageCache::Image ImageCache::svg(const std::filesystem::path& file, int w, int h, bool mono) {
    if (w <= 0 || h <= 0)
        return {};
    const auto key = std::make_tuple(file.native(), w, h, mono);
    if (const auto it = entries_.find(key); it != entries_.end())
        return it->second.image;
    std::string text = read_file(file);
    if (text.empty()) {
        entries_[key] = Entry{};
        return {};
    }
    if (mono) {
        for (std::size_t at = text.find("currentColor"); at != std::string::npos;
             at = text.find("currentColor", at))
            text.replace(at, 12, "#ffffff");
    }
    const auto doc = lunasvg::Document::loadFromData(text);
    if (!doc) {
        entries_[key] = Entry{};
        return {};
    }
    const lunasvg::Bitmap bmp = doc->renderToBitmap(w, h);
    if (bmp.isNull()) {
        entries_[key] = Entry{};
        return {};
    }
    auto rgba = to_rgba(bmp);
    if (mono) {  // GPUI svg(): the shape is a mask filled with the text color
        const std::size_t n = static_cast<std::size_t>(bmp.width()) * static_cast<std::size_t>(bmp.height());
        for (std::size_t i = 0; i < n; ++i)
            rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
    }
    return keep(key, bmp.width(), bmp.height(), std::move(rgba));
}

}  // namespace agentdesk::ui
