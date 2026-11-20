#pragma once
// SVG files as ImGui textures, rasterised at the size they are drawn (lunasvg). The program icons
// are drawn this way. The textures go through ImGui's own texture list (ImTextureData), so the
// renderer creates them and recreates them after a lost device. UI thread only; the cache must
// outlive the ImGui context's last frame.
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

    // An SVG file drawn at w x h pixels. `mono`: its shape in white, to be tinted by the color it
    // is drawn with (GPUI's svg().text_color(): the SVG is a mask). Missing or unreadable: !ok().
    Image svg(const std::filesystem::path& file, int w, int h, bool mono = false);

private:
    struct Entry {
        std::unique_ptr<ImTextureData> data;
        Image image;
    };
    Image keep(const std::tuple<std::wstring, int, int, bool>& key, int w, int h,
               std::unique_ptr<unsigned char[]> rgba);
    std::map<std::tuple<std::wstring, int, int, bool>, Entry> entries_;
};

}  // namespace agentdesk::ui
