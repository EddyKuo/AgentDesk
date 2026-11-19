// Programs (programs\programs.json and icons) and small shared helpers: paths, elapsed time,
// clipped text, attention colours, fuzzy matching.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// ------------------------------------------------------------------ programs
// herdr src/programs.rs: one table (defaults/programs.json, shipped in programs\ next to the exe)
// with the person's programs.json in the data folder on top; read again when that file changes,
// problems go to programs.error (herdr window.rs 982-991, 909-912).
ui::Programs g_programs;

}  // namespace

std::filesystem::file_time_type g_programs_mtime{};

namespace {
bool g_programs_loaded = false;

std::filesystem::path programs_dir() {
    wchar_t exe[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return std::filesystem::path(exe).parent_path() / L"programs";
}

}  // namespace

// Returns the problem with the person's file, if any (empty: applied or no file).
std::string load_programs() {
    std::error_code ec;
    const auto mine = data_dir() / L"programs.json";
    g_programs_mtime = std::filesystem::last_write_time(mine, ec);
    std::string error;
    g_programs = ui::Programs::load(programs_dir() / L"programs.json", mine, &error);
    g_programs_loaded = true;
    const auto err_path = data_dir() / L"programs.error";
    if (error.empty())
        std::filesystem::remove(err_path, ec);
    else
        std::ofstream(err_path, std::ios::binary) << "programs.json was not applied: " << error << "\n";
    return error;
}

namespace {
const ui::Programs& programs() {
    if (!g_programs_loaded)
        load_programs();
    return g_programs;
}

}  // namespace

// What runs in a terminal (herdr programs::of): the agent's 7501 `app` first, else the
// foreground program's argv (its name alone from an older host).
ui::Program program_of(const ui::SessionView& v) {
    std::vector<std::string> argv = v.program_argv;
    if (argv.empty() && !v.program.empty())
        argv.push_back(v.program);
    return programs().of(v.agent_app, argv);
}

// The program a profile starts.
ui::Program program_of_profile(const std::string& executable, const std::string& id) {
    return programs().of({}, {executable.empty() ? id : executable});
}

// A person's icon (in the data folder) before the shipped one (herdr: both live in its home).
std::filesystem::path program_icon_file(const ui::Program& p) {
    if (p.icon.empty())
        return {};
    const auto rel = win::to_wide(p.icon);
    std::error_code ec;
    if (const auto mine = data_dir() / rel; std::filesystem::is_regular_file(mine, ec))
        return mine;
    return programs_dir() / rel;
}

ImU32 hex_color(const std::string& hex, ImU32 fallback) {
    if (hex.size() != 7 || hex[0] != '#')
        return fallback;
    const unsigned long v = std::strtoul(hex.c_str() + 1, nullptr, 16);
    return IM_COL32((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff, 255);
}

ui::ImageCache* g_program_images = nullptr;  // App::State's (set while the App lives)

namespace {
ui::ImageCache& program_images() {
    static ui::ImageCache none;  // never used while an App lives
    return g_program_images != nullptr ? *g_program_images : none;
}

}  // namespace

// herdr's icon(p, size, color) (pane/main.js 27-30, agents/main.js 78, tabs/main.js 120): a
// tinted entry is a one-color SVG in `color`, else the picture itself. `min` is its top left.
void program_icon(ImDrawList* dl, const ui::Program& p, ImVec2 min, float size, ImU32 color) {
    const auto file = program_icon_file(p);
    if (file.empty())
        return;
    const int side = std::max(1, static_cast<int>(size + 0.5f));
    const auto img = program_images().svg(file, side, side, p.tint);
    if (!img.ok())
        return;
    dl->AddImage(img.tex, min, ImVec2(min.x + size, min.y + size), ImVec2(0, 0), ImVec2(1, 1),
                 p.tint ? color : IM_COL32_WHITE);
}

bool is_shell(const std::string& program) {
    return program == "cmd" || program == "pwsh" || program == "powershell" || program == "bash" ||
           program == "wsl" || program == "nu";
}

// Profile ids that open a shell rather than an agent (WSL profiles are "wsl-<distribution>").
bool is_shell_profile(const std::string& id) {
    return is_shell(id) || id.starts_with("wsl-");
}

// ------------------------------------------------------------------ small helpers
std::string folder_name(const std::string& cwd) {
    std::string s = cwd;
    while (!s.empty() && (s.back() == '\\' || s.back() == '/'))
        s.pop_back();
    const auto p = s.find_last_of("\\/");
    return p == std::string::npos ? s : s.substr(p + 1);
}

// `p` is `dir` or a folder in it (case-insensitive, either slash): "C:\src\feat-2" is not in
// "C:\src\feat".
bool path_within(const std::string& p, const std::string& dir) {
    return !dir.empty() && p.size() >= dir.size() && _strnicmp(p.c_str(), dir.c_str(), dir.size()) == 0 &&
           (p.size() == dir.size() || p[dir.size()] == '\\' || p[dir.size()] == '/' || dir.back() == '\\' ||
            dir.back() == '/');
}

std::string display_path(const std::string& cwd) {
    static const std::string home = [] {
        wchar_t buf[MAX_PATH]{};
        const DWORD n = ::GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
        return n > 0 ? win::to_utf8(std::wstring(buf, n)) : std::string();
    }();
    if (path_within(cwd, home))
        return "~" + cwd.substr(home.size());
    return cwd;
}

std::string elapsed_since(const std::string& iso) {
    std::tm tm{};
    std::istringstream in(iso);
    in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (in.fail())
        return {};
    const std::time_t t = _mkgmtime(&tm);
    const long long s = std::max<long long>(0, static_cast<long long>(std::time(nullptr) - t));
    if (s < 60)
        return std::to_string(s) + "s";
    if (s < 3600)
        return std::to_string(s / 60) + "m";
    if (s < 86400)
        return std::to_string(s / 3600) + "h";
    return std::to_string(s / 86400) + "d";
}

float text_clipped(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const std::string& text,
                   float max_w) {
    if (max_w <= 0 || text.empty())
        return 0;
    const ImVec2 full = font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str());
    if (full.x <= max_w) {
        dl->AddText(font, size, pos, col, text.c_str());
        return full.x;
    }
    const char* ell = "\xE2\x80\xA6";
    const float ell_w = font->CalcTextSizeA(size, FLT_MAX, 0, ell).x;
    std::size_t n = text.size();
    while (n > 0) {
        --n;
        while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
            --n;
        if (font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str(), text.c_str() + n).x + ell_w <= max_w)
            break;
    }
    const std::string cut = text.substr(0, n) + ell;
    dl->AddText(font, size, pos, col, cut.c_str());
    return font->CalcTextSizeA(size, FLT_MAX, 0, cut.c_str()).x;
}

ImU32 attention_color(Attention a) {
    switch (a) {
        case Attention::kBlocked:
            return g_pal->needs_you;
        case Attention::kDone:
            return g_pal->done;
        case Attention::kWorking:
            return g_pal->working;
        case Attention::kIdle:
            return g_pal->idle;
        case Attention::kNone:
            return g_pal->faint;
    }
    return g_pal->faint;
}

namespace {
// Shape + color mark; never color alone (design doc 4.1). Working pulses gently.
void attention_mark(ImDrawList* dl, ImVec2 c, float r, Attention a) {
    const ImU32 col = attention_color(a);
    switch (a) {
        case Attention::kBlocked: {
            dl->AddCircleFilled(c, r * 1.15f, col);
            dl->AddLine(ImVec2(c.x, c.y - r * 0.55f), ImVec2(c.x, c.y + r * 0.15f), g_pal->card, px(1.6f));
            dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.55f), px(1.0f), g_pal->card);
            break;
        }
        case Attention::kDone:
            dl->AddCircleFilled(c, r * 1.1f, col);
            dl->AddLine(ImVec2(c.x - r * 0.5f, c.y), ImVec2(c.x - r * 0.1f, c.y + r * 0.45f), g_pal->card,
                        px(1.6f));
            dl->AddLine(ImVec2(c.x - r * 0.1f, c.y + r * 0.45f), ImVec2(c.x + r * 0.55f, c.y - r * 0.4f),
                        g_pal->card, px(1.6f));
            break;
        case Attention::kWorking: {
            const float t = static_cast<float>(ImGui::GetTime());
            const float a0 = t * 4.0f;
            dl->PathArcTo(c, r, a0, a0 + 4.2f, 16);
            dl->PathStroke(col, 0, px(1.8f));
            break;
        }
        case Attention::kIdle:
            dl->AddCircle(c, r * 0.8f, col, 0, px(1.4f));
            break;
        case Attention::kNone:
            (void)col;
            break;
    }
}

bool fuzzy_match(const std::string& text, const std::string& query) {
    std::size_t j = 0;
    for (std::size_t i = 0; i < text.size() && j < query.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) ==
            std::tolower(static_cast<unsigned char>(query[j])))
            ++j;
    }
    return j == query.size();
}

}  // namespace

Dir parse_dir(const std::string& s) {
    if (s == "left")
        return Dir::kLeft;
    if (s == "up")
        return Dir::kUp;
    if (s == "down")
        return Dir::kDown;
    return Dir::kRight;
}
}  // namespace detail

}  // namespace agentdesk::desktop
