#pragma once
// Pictures as ImGui textures: SVG files rasterised at the size they are drawn (lunasvg), PNG files
// decoded once (Windows Imaging Component). Herdr 98 draws its icons, cards and pictures this way
// (herdr renders the same files with its own SVG and image support). The textures go through
// ImGui's own texture list (ImTextureData), so the renderer creates them and recreates them after
// a lost device. UI thread only; the cache must outlive the ImGui context's last frame.
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <tuple>

#include <imgui.h>

namespace agentdesk::ui {

class ImageCache {
public:
    struct Image {
        ImTextureRef tex;
        int w = 0;
        int h = 0;
        [[nodiscard]] bool ok() const { return w > 0 && h > 0; }
    };

    ImageCache() = default;
    ~ImageCache();
    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;

    // An SVG file drawn at w x h pixels. `mono`: its currentColor is white, to be tinted by the
    // color it is drawn with (herdr's svg().text_color()). Missing or unreadable: !ok().
    Image svg(const std::filesystem::path& file, int w, int h, bool mono = false);
    // A PNG (or any WIC format) at its own size.
    Image picture(const std::filesystem::path& file);

    // Fonts an SVG's <text> may name (lunasvg looks fonts up by family): registered once.
    static void add_svg_font(const char* family, bool bold, const std::filesystem::path& file);

private:
    struct Entry {
        std::unique_ptr<ImTextureData> data;
        Image image;
    };
    Image keep(const std::tuple<std::wstring, int, int, bool>& key, int w, int h, std::unique_ptr<unsigned char[]> rgba);
    std::map<std::tuple<std::wstring, int, int, bool>, Entry> entries_;
};

}  // namespace agentdesk::ui
