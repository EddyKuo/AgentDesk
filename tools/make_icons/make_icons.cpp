// make_icons [<assets/icon folder>]: renders the icon's SVG files into agentdesk.ico (Windows),
// agentdesk.icns (macOS) and agentdesk.png, plus agentctl.ico and agent-host.ico: the same meerkat
// on another colour, so the three programs look related but can be told apart. The results are in
// the repository; run it after changing an SVG.
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <lunasvg.h>

namespace {

namespace fs = std::filesystem;

// Up to this many pixels the simplified drawing is the readable one.
constexpr int kSmallUpTo = 48;
// Below this its pupils would only blur the eyes.
constexpr int kPupilsFrom = 32;
// Both drawings' viewBox is this wide and high.
constexpr float kUnits = 1024.0f;
// Apple's icon grid: the tile takes 824 of 1024 pixels, the rest is room for the shadow.
constexpr float kMacTile = 824.0f / 1024.0f;

// The tile's colours in the SVG files: the full drawing's gradient top and bottom and its rock, and
// the small drawing's flat tile. Each appears once there, so a variant swaps them by text.
struct Tile {
    std::string_view top, bottom, rock, flat;
};
constexpr Tile kUiTile{"#2f5d73", "#17303f", "#0f2230", "#1e4357"};

struct Variant {
    const char* file;
    Tile tile;
};
// agentctl in plum, agent-host in green; agent-ui keeps the blue of the SVG files.
constexpr std::array kVariants = {
    Variant{"agentctl.ico", {"#6a4a86", "#2c1d3f", "#1b1128", "#45305e"}},
    Variant{"agent-host.ico", {"#3f7552", "#183324", "#0d2016", "#29513a"}},
};

struct Drawings {
    std::unique_ptr<lunasvg::Document> full;
    std::unique_ptr<lunasvg::Document> simple;
};

std::string read_text(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in && !in.eof())
        throw std::runtime_error("cannot read " + file.string());
    return text;
}

void swap_colour(std::string& svg, std::string_view from, std::string_view to) {
    const auto at = svg.find(from);
    if (at == std::string::npos)
        throw std::runtime_error("colour " + std::string(from) + " is not in the SVG");
    svg.replace(at, from.size(), to);
}

std::unique_ptr<lunasvg::Document> parse(const std::string& svg) {
    auto doc = lunasvg::Document::loadFromData(svg);
    if (!doc)
        throw std::runtime_error("cannot parse an SVG");
    return doc;
}

struct Sources {
    std::string full;
    std::string simple;
};

// Both drawings with the tile in the given colours.
Drawings drawings(const Sources& src, const Tile& tile) {
    std::string full = src.full;
    swap_colour(full, kUiTile.top, tile.top);
    swap_colour(full, kUiTile.bottom, tile.bottom);
    swap_colour(full, kUiTile.rock, tile.rock);
    std::string simple = src.simple;
    swap_colour(simple, kUiTile.flat, tile.flat);
    return {parse(full), parse(simple)};
}

// One size as PNG; `tile` is the part of the width the drawing takes, centred.
std::string render(const Drawings& d, int size, float tile) {
    lunasvg::Document& doc = size <= kSmallUpTo ? *d.simple : *d.full;
    if (auto pupils = d.simple->getElementById("pupils"))
        pupils.setAttribute("display", size < kPupilsFrom ? "none" : "inline");
    d.simple->updateLayout();
    lunasvg::Bitmap bmp(size, size);
    bmp.clear(0x00000000);
    const float side = static_cast<float>(size) * tile;
    const float margin = (static_cast<float>(size) - side) / 2.0f;
    const float scale = side / kUnits;
    doc.render(bmp, lunasvg::Matrix(scale, 0, 0, scale, margin, margin));
    std::string png;
    const auto append = [](void* to, void* data, int n) {
        static_cast<std::string*>(to)->append(static_cast<const char*>(data), static_cast<std::size_t>(n));
    };
    if (!bmp.writeToPng(append, &png))
        throw std::runtime_error("cannot encode a PNG");
    return png;
}

void put_le(std::string& s, std::size_t v, int bytes) {
    for (int i = 0; i < bytes; ++i)
        s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}

void put_be32(std::string& s, std::size_t v) {
    for (int shift = 24; shift >= 0; shift -= 8)
        s.push_back(static_cast<char>((v >> shift) & 0xff));
}

std::string ico(const Drawings& d) {
    constexpr std::array kSizes = {16, 20, 24, 32, 40, 48, 64, 128, 256};
    std::vector<std::string> images;
    for (const int size : kSizes)
        images.push_back(render(d, size, 1.0f));
    std::string out;
    put_le(out, 0, 2);
    put_le(out, 1, 2);  // an icon, not a cursor
    put_le(out, kSizes.size(), 2);
    std::size_t offset = 6 + 16 * kSizes.size();
    for (std::size_t i = 0; i < kSizes.size(); ++i) {
        const auto side = static_cast<std::size_t>(kSizes[i] % 256);  // 0 stands for 256
        put_le(out, side, 1);
        put_le(out, side, 1);
        put_le(out, 0, 2);   // palette size, reserved
        put_le(out, 1, 2);   // planes
        put_le(out, 32, 2);  // bits per pixel
        put_le(out, images[i].size(), 4);
        put_le(out, offset, 4);
        offset += images[i].size();
    }
    for (const auto& png : images)
        out += png;
    return out;
}

std::string icns(const Drawings& d) {
    struct Entry {
        const char* type;
        int size;
    };
    // 16, 32, 128, 256 and 512 points, at 1x and 2x. The 1x forms of 16 and 32 are left out: not every
    // macOS reads them as PNG, and it scales the 2x ones.
    constexpr std::array kEntries = {Entry{"ic11", 32},  Entry{"ic12", 64},  Entry{"ic07", 128},
                                     Entry{"ic13", 256}, Entry{"ic08", 256}, Entry{"ic14", 512},
                                     Entry{"ic09", 512}, Entry{"ic10", 1024}};
    std::string body;
    for (const auto& e : kEntries) {
        const std::string png = render(d, e.size, kMacTile);
        body += e.type;
        put_be32(body, png.size() + 8);
        body += png;
    }
    std::string out = "icns";
    put_be32(out, body.size() + 8);
    return out + body;
}

void write(const fs::path& file, const std::string& bytes) {
    std::ofstream out(file, std::ios::binary);
    out << bytes;
    out.close();
    if (!out)
        throw std::runtime_error("cannot write " + file.string());
    std::cout << file.filename().string() << "  " << bytes.size() << " bytes\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const fs::path dir =
            argc > 1 ? fs::path(argv[1]) : fs::path(AGENTDESK_SOURCE_DIR) / "assets" / "icon";
        const Sources src{read_text(dir / "agentdesk.svg"), read_text(dir / "agentdesk-small.svg")};
        const Drawings d = drawings(src, kUiTile);
        write(dir / "agentdesk.ico", ico(d));
        write(dir / "agentdesk.icns", icns(d));
        write(dir / "agentdesk.png", render(d, 512, 1.0f));
        for (const auto& v : kVariants)
            write(dir / v.file, ico(drawings(src, v.tile)));
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "make_icons: " << e.what() << '\n';
        return 1;
    }
}
