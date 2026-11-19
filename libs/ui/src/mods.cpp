// Loads mod DLLs and calls into them, every call guarded so a crashing mod is turned off instead
// of taking agent-ui.exe down; also parses the panels mods return.
#include "agentdesk/ui/mods.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>

#include "agentdesk/mod_api.h"
#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::ui {

namespace {

using Json = nlohmann::json;

Json session_json(const ModSession& s) {
    return {{"id", s.id},           {"title", s.title},       {"program", s.program},
            {"cwd", s.cwd},         {"state", s.agent_state}, {"process", s.process_state},
            {"machine", s.machine}, {"parent", s.parent},     {"created_at", s.created_at},
            {"labels", s.labels},   {"focused", s.focused}};
}

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string dump(const Json& j) {
    return j.dump(-1, ' ', false, Json::error_handler_t::replace);
}

// ---- calls into a mod, guarded against crashes (structured exceptions). These functions hold
// no C++ objects, so __try is allowed; a fault returns its exception code instead of ending
// agent-ui.exe.
using Code = unsigned long;

Code seh_init(agentdesk_mod_init_fn fn, const agentdesk_host* h, agentdesk_mod* m, int* rc) {
    __try {
        *rc = fn(h, m);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
Code seh_event(const agentdesk_mod* m, const char* e, const char* p) {
    __try {
        m->on_event(m->self, e, p);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
Code seh_panels(const agentdesk_mod* m, const char* where, const char** out) {
    __try {
        *out = m->panels(m->self, where);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
Code seh_click(const agentdesk_mod* m, const char* panel, const char* widget) {
    __try {
        m->click(m->self, panel, widget);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
Code seh_decorate(const agentdesk_mod* m, const char* kind, const char* subject, const char** out) {
    __try {
        *out = m->decorate(m->self, kind, subject);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
Code seh_command(const agentdesk_mod* m, const char* id) {
    __try {
        m->command(m->self, id);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
Code seh_destroy(const agentdesk_mod* m) {
    __try {
        m->destroy(m->self);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
// Copies a mod-owned string; a bad pointer counts as a crash.
Code seh_copy(const char* s, std::size_t max, char* buf, std::size_t* len) {
    __try {
        std::size_t n = 0;
        while (n < max && s[n] != '\0') {
            buf[n] = s[n];
            ++n;
        }
        *len = n;
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

std::string crash_text(Code code) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "crashed (exception 0x%08lX); turned off until reload", code);
    return buf;
}

ModWidget::Kind widget_kind(const std::string& k) {
    if (k == "button")
        return ModWidget::Kind::kButton;
    if (k == "separator")
        return ModWidget::Kind::kSeparator;
    if (k == "progress")
        return ModWidget::Kind::kProgress;
    if (k == "row")
        return ModWidget::Kind::kRow;
    if (k == "badge")
        return ModWidget::Kind::kBadge;
    return ModWidget::Kind::kText;
}

// Fields of mod-supplied JSON: a value of the wrong type counts as missing (json::value throws).
std::string str_field(const Json& j, const char* key, const char* fallback = "") {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string(fallback);
}
float num_field(const Json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<float>() : 0.0f;
}

// A JSON object file in a mod folder; nullopt when it is missing or not an object.
std::optional<Json> read_object(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec))
        return std::nullopt;
    auto j = Json::parse(read_file(file), nullptr, false);
    if (!j.is_object())
        return std::nullopt;
    return j;
}

}  // namespace

std::vector<ModPanel> parse_mod_panels(const std::string& mod, const std::string& where,
                                       const std::string& text) {
    std::vector<ModPanel> out;
    const auto list = Json::parse(text, nullptr, false);
    if (!list.is_array())
        return out;
    for (const auto& pj : list) {
        if (!pj.is_object())
            continue;
        ModPanel panel{mod, str_field(pj, "id"), str_field(pj, "title"), where, {}};
        if (const auto widgets = pj.find("widgets"); widgets != pj.end() && widgets->is_array()) {
            for (const auto& w : *widgets) {
                if (!w.is_object() || panel.widgets.size() >= 200)
                    continue;
                ModWidget mw;
                mw.kind = widget_kind(str_field(w, "kind", "text"));
                mw.text = str_field(w, "text");
                mw.text2 = str_field(w, "text2");
                mw.color = str_field(w, "color");
                const float v = num_field(w, "value");
                mw.value = std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
                mw.id = str_field(w, "id");
                panel.widgets.push_back(std::move(mw));
            }
        }
        out.push_back(std::move(panel));
    }
    return out;
}

// ------------------------------------------------------------------ one mod = one DLL
struct ModHost::Mod {
    ModHost* host = nullptr;
    ModInfo info;
    std::filesystem::path folder;
    HMODULE dll = nullptr;
    agentdesk_host api{};
    agentdesk_mod mod{};
    bool ready = false;    // init succeeded and not crashed since
    bool crashed = false;  // its code may still run (threads, callbacks): never unload it
    Json settings = Json::object();
    std::vector<ModCommand> commands;
    std::string scratch;  // sessions() / setting() result, valid during one call

    ~Mod() {
        if (dll == nullptr || crashed)
            return;  // a mod that crashed may have left state behind: keep its code mapped
        if (ready && mod.destroy != nullptr && seh_destroy(&mod) != 0)
            return;
        ::FreeLibrary(dll);
    }

    void fail(Code code) {
        crashed = true;
        ready = false;
        info.loaded = false;
        info.error = crash_text(code);
    }

    // Copies a string the mod returned (at most `max` bytes).
    bool take(const char* s, std::string& out, std::size_t max) {
        out.clear();
        if (s == nullptr)
            return true;
        std::string buf(max, '\0');
        std::size_t n = 0;
        if (const Code c = seh_copy(s, max, buf.data(), &n); c != 0) {
            fail(c);
            return false;
        }
        buf.resize(n);
        out = std::move(buf);
        return true;
    }

    static Mod& of(void* ctx) { return *static_cast<Mod*>(ctx); }
};

struct ModHostAccess {
    static ModOutput& output(ModHost& h) { return h.output_; }
    static const std::vector<ModSession>& sessions(const ModHost& h) { return h.sessions_; }
};

namespace {

void api_run(void* ctx, const char* command) {
    if (command != nullptr)
        ModHostAccess::output(*ModHost::Mod::of(ctx).host).commands.emplace_back(command);
}
void api_send(void* ctx, const char* session, const char* text, int enter) {
    if (session == nullptr || text == nullptr)
        return;
    ModHostAccess::output(*ModHost::Mod::of(ctx).host).sends.push_back({session, text, enter != 0});
}
void api_notify(void* ctx, const char* title, const char* body) {
    if (title != nullptr)
        ModHostAccess::output(*ModHost::Mod::of(ctx).host)
            .notes.push_back({title, body != nullptr ? body : ""});
}
void api_exec(void* ctx, const char* const* argv, int argc) {
    if (argv == nullptr || argc <= 0)
        return;
    std::vector<std::wstring> a;
    for (int i = 0; i < argc && i < 64; ++i)
        if (argv[i] != nullptr)
            a.push_back(platform::win::to_wide(argv[i]));
    if (!a.empty())
        ModHostAccess::output(*ModHost::Mod::of(ctx).host).execs.push_back(std::move(a));
}
void api_add_command(void* ctx, const char* id, const char* label) {
    auto& m = ModHost::Mod::of(ctx);
    if (id == nullptr || label == nullptr)
        return;
    std::erase_if(m.commands, [&](const ModCommand& c) { return c.id == id; });
    m.commands.push_back({m.info.name, id, label});
}
const char* api_sessions(void* ctx) {
    auto& m = ModHost::Mod::of(ctx);
    Json list = Json::array();
    for (const auto& s : ModHostAccess::sessions(*m.host))
        list.push_back(session_json(s));
    m.scratch = dump(list);
    return m.scratch.c_str();
}
const char* api_setting(void* ctx, const char* key) {
    auto& m = ModHost::Mod::of(ctx);
    if (key == nullptr || !m.settings.contains(key))
        return nullptr;
    m.scratch = dump(m.settings[key]);
    return m.scratch.c_str();
}

// Loads <folder>\mod.dll and runs its agentdesk_mod_init. Any failure is kept in m.info.error;
// success sets m.ready.
void start_mod(ModHost::Mod& m) {
    // The mod's own folder first for its dependencies, then System32; never the current folder
    // or PATH.
    m.dll = ::LoadLibraryExW((m.folder / L"mod.dll").c_str(), nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (m.dll == nullptr) {
        m.info.error = "cannot load mod.dll (error " + std::to_string(::GetLastError()) + ")";
        return;
    }
    const auto init = reinterpret_cast<agentdesk_mod_init_fn>(::GetProcAddress(m.dll, "agentdesk_mod_init"));
    if (init == nullptr) {
        m.info.error = "mod.dll does not export agentdesk_mod_init";
        return;
    }
    m.api = {AGENTDESK_MOD_API_VERSION, &m,           api_run,    api_send, api_notify, api_exec,
             api_add_command,           api_sessions, api_setting};
    int rc = -1;
    if (const Code c = seh_init(init, &m.api, &m.mod, &rc); c != 0) {
        m.crashed = true;
        m.info.error = crash_text(c);
    } else if (rc != 0) {
        m.info.error = "the mod refused to start (code " + std::to_string(rc) + ")";
    } else if (m.mod.api_version != AGENTDESK_MOD_API_VERSION) {
        m.info.error = "built for mod interface version " + std::to_string(m.mod.api_version) +
                       ", this AgentDesk has " + std::to_string(AGENTDESK_MOD_API_VERSION);
    } else {
        m.ready = true;
        m.info.loaded = true;
        // The DLL's own description fills in only when mod.json gave none.
        std::string d;
        if (m.take(m.mod.description, d, 200) && !d.empty() && m.info.description.empty())
            m.info.description = d;
    }
}

}  // namespace

ModHost::ModHost(std::filesystem::path dir) : dir_(std::move(dir)) {}
ModHost::~ModHost() = default;

void ModHost::load(const std::set<std::string>& enabled) {
    mods_.clear();
    std::error_code ec;
    if (!std::filesystem::is_directory(dir_, ec))
        return;
    std::vector<std::filesystem::path> folders;
    for (const auto& e : std::filesystem::directory_iterator(dir_, ec))
        if (e.is_directory())
            folders.push_back(e.path());
    std::sort(folders.begin(), folders.end());
    for (const auto& f : folders) {
        const bool has_dll = std::filesystem::is_regular_file(f / L"mod.dll", ec);
        const bool old_lua = std::filesystem::is_regular_file(f / L"main.lua", ec);
        if (!has_dll && !old_lua)
            continue;
        auto m = std::make_unique<Mod>();
        m->host = this;
        m->folder = f;
        const auto name8 = f.filename().u8string();
        m->info.name.assign(name8.begin(), name8.end());
        m->info.enabled = enabled.contains(m->info.name);
        if (const auto j = read_object(f / L"mod.json"))
            m->info.description = str_field(*j, "description");
        if (auto j = read_object(f / L"settings.json"))
            m->settings = std::move(*j);
        if (!has_dll)
            m->info.error = "Lua mods are no longer supported: build it as mod.dll (see the user guide)";
        else if (m->info.enabled)
            start_mod(*m);
        mods_.push_back(std::move(m));
    }
}

std::vector<ModInfo> ModHost::mods() const {
    std::vector<ModInfo> out;
    for (const auto& m : mods_)
        out.push_back(m->info);
    return out;
}

void ModHost::set_sessions(std::vector<ModSession> sessions) {
    sessions_ = std::move(sessions);
}

void ModHost::emit(const std::string& event, const Json& payload) {
    const std::string p = dump(payload);
    for (auto& m : mods_) {
        if (!m->ready || m->mod.on_event == nullptr)
            continue;
        if (const Code c = seh_event(&m->mod, event.c_str(), p.c_str()); c != 0)
            m->fail(c);
    }
}

std::vector<ModPanel> ModHost::panels(const std::string& where) {
    std::vector<ModPanel> out;
    for (auto& m : mods_) {
        if (!m->ready || m->mod.panels == nullptr)
            continue;
        const char* raw = nullptr;
        if (const Code c = seh_panels(&m->mod, where.c_str(), &raw); c != 0) {
            m->fail(c);
            continue;
        }
        std::string text;
        if (!m->take(raw, text, 1024 * 1024))
            continue;
        for (auto& panel : parse_mod_panels(m->info.name, where, text))
            out.push_back(std::move(panel));
    }
    return out;
}

void ModHost::click(const std::string& mod, const std::string& panel, const std::string& widget_id) {
    for (auto& m : mods_) {
        if (m->info.name != mod || !m->ready || m->mod.click == nullptr)
            continue;
        if (const Code c = seh_click(&m->mod, panel.c_str(), widget_id.c_str()); c != 0)
            m->fail(c);
    }
}

std::string ModHost::decorate(const std::string& kind, const Json& subject) {
    std::string out;
    const std::string s = dump(subject);
    for (auto& m : mods_) {
        if (!m->ready || m->mod.decorate == nullptr)
            continue;
        const char* raw = nullptr;
        if (const Code c = seh_decorate(&m->mod, kind.c_str(), s.c_str(), &raw); c != 0) {
            m->fail(c);
            continue;
        }
        std::string text;
        if (m->take(raw, text, 64) && !text.empty())
            out += (out.empty() ? "" : " ") + text;
    }
    return out;
}

std::vector<ModCommand> ModHost::commands() const {
    std::vector<ModCommand> out;
    for (const auto& m : mods_)
        if (m->ready)
            out.insert(out.end(), m->commands.begin(), m->commands.end());
    return out;
}

void ModHost::run_command(const std::string& mod, const std::string& id) {
    for (auto& m : mods_) {
        if (m->info.name != mod || !m->ready || m->mod.command == nullptr)
            continue;
        if (const Code c = seh_command(&m->mod, id.c_str()); c != 0)
            m->fail(c);
    }
}

ModOutput ModHost::take_output() {
    return std::exchange(output_, {});
}

}  // namespace agentdesk::ui
