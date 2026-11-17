#include "agentdesk/ui/mods.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

// Lua built as C++ (vcpkg lua[cpp]): errors are C++ exceptions, so RAII objects unwind. The
// headers are included directly, not through lua.hpp (which would force C linkage).
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::ui {

namespace {

using Json = nlohmann::json;
constexpr int kHookEvery = 100000;  // instructions between checks
constexpr int kMaxHookTicks = 50;   // 5 million instructions per call
constexpr int kMaxFailures = 5;     // then the mod is stopped until reloaded
const char* const kModKey = "agentdesk.mod";

void push_json(lua_State* L, const Json& j, int depth = 0) {
    if (depth > 32) {
        lua_pushnil(L);
        return;
    }
    switch (j.type()) {
        case Json::value_t::null:
        case Json::value_t::discarded:
            lua_pushnil(L);
            break;
        case Json::value_t::boolean:
            lua_pushboolean(L, j.get<bool>() ? 1 : 0);
            break;
        case Json::value_t::number_integer:
        case Json::value_t::number_unsigned:
            lua_pushinteger(L, j.get<lua_Integer>());
            break;
        case Json::value_t::number_float:
            lua_pushnumber(L, j.get<double>());
            break;
        case Json::value_t::string:
            lua_pushstring(L, j.get_ref<const std::string&>().c_str());
            break;
        case Json::value_t::array: {
            lua_createtable(L, static_cast<int>(j.size()), 0);
            lua_Integer i = 1;
            for (const auto& v : j) {
                push_json(L, v, depth + 1);
                lua_rawseti(L, -2, i++);
            }
            break;
        }
        case Json::value_t::object: {
            lua_createtable(L, 0, static_cast<int>(j.size()));
            for (const auto& [k, v] : j.items()) {
                push_json(L, v, depth + 1);
                lua_setfield(L, -2, k.c_str());
            }
            break;
        }
        case Json::value_t::binary:
            lua_pushnil(L);
            break;
    }
}

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

}  // namespace

// ------------------------------------------------------------------ one mod = one Lua state
struct ModHost::Mod {
    ModHost* host = nullptr;
    ModInfo info;
    std::filesystem::path folder;
    lua_State* L = nullptr;
    int hook_ticks = 0;
    int failures = 0;
    Json settings = Json::object();
    std::map<std::string, std::vector<int>> handlers;  // event -> function refs
    struct Panel {
        std::string id;
        std::string title;
        std::string where;
        int draw = LUA_NOREF;
    };
    std::vector<Panel> panels;
    std::map<std::string, std::vector<int>> decorators;  // kind -> function refs
    struct Command {
        std::string id;
        std::string label;
        int fn = LUA_NOREF;
    };
    std::vector<Command> commands;
    ModPanel* drawing = nullptr;  // panel being drawn

    ~Mod() {
        if (L != nullptr)
            lua_close(L);
    }

    static Mod* from(lua_State* L) {
        lua_getfield(L, LUA_REGISTRYINDEX, kModKey);
        auto* m = static_cast<Mod*>(lua_touserdata(L, -1));
        lua_pop(L, 1);
        return m;
    }

    // Calls the function on the stack top with `nargs` arguments below it already pushed (the
    // function first). Returns false and records the error on failure.
    bool call(int nargs, int nresults) {
        hook_ticks = 0;
        if (lua_pcall(L, nargs, nresults, 0) != LUA_OK) {
            const char* msg = lua_tostring(L, -1);
            info.error = msg != nullptr ? msg : "error";
            lua_pop(L, 1);
            if (++failures >= kMaxFailures) {
                info.loaded = false;
                info.error += " (stopped after repeated errors; reload to try again)";
            }
            return false;
        }
        return true;
    }
};

namespace {

ModHost::Mod* mod_of(lua_State* L) {
    return ModHost::Mod::from(L);
}

void hook(lua_State* L, lua_Debug*) {
    auto* m = mod_of(L);
    if (m != nullptr && ++m->hook_ticks > kMaxHookTicks)
        luaL_error(L, "mod took too long (more than %d million instructions)",
                   kMaxHookTicks * kHookEvery / 1000000);
}

// Skips the receiver when called with method syntax (ui:text("x")).
int first_arg(lua_State* L) {
    return lua_istable(L, 1) ? 2 : 1;
}

int l_on(lua_State* L) {
    auto* m = mod_of(L);
    const std::string ev = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    m->handlers[ev].push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    return 0;
}

}  // namespace

namespace {

ModOutput& output_of(lua_State* L);
const std::vector<ModSession>& sessions_of(lua_State* L);

int api_run(lua_State* L) {
    output_of(L).commands.emplace_back(luaL_checkstring(L, 1));
    return 0;
}

int api_send(lua_State* L) {
    ModOutput::Send s;
    s.session = luaL_checkstring(L, 1);
    s.text = luaL_checkstring(L, 2);
    s.enter = lua_toboolean(L, 3) != 0;
    output_of(L).sends.push_back(std::move(s));
    return 0;
}

int api_notify(lua_State* L) {
    output_of(L).notes.push_back({luaL_checkstring(L, 1), luaL_optstring(L, 2, "")});
    return 0;
}

int api_exec(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    std::vector<std::wstring> argv;
    const lua_Integer n = luaL_len(L, 1);
    for (lua_Integer i = 1; i <= n && i <= 64; ++i) {
        lua_rawgeti(L, 1, i);
        argv.push_back(platform::win::to_wide(luaL_checkstring(L, -1)));
        lua_pop(L, 1);
    }
    if (argv.empty())
        return luaL_error(L, "exec needs a program");
    output_of(L).execs.push_back(std::move(argv));
    return 0;
}

int api_sessions(lua_State* L) {
    Json list = Json::array();
    for (const auto& s : sessions_of(L))
        list.push_back(session_json(s));
    push_json(L, list);
    return 1;
}

int api_focused(lua_State* L) {
    for (const auto& s : sessions_of(L)) {
        if (s.focused) {
            lua_pushstring(L, s.id.c_str());
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

int api_setting(lua_State* L) {
    auto* m = mod_of(L);
    const std::string key = luaL_checkstring(L, 1);
    if (m->settings.contains(key))
        push_json(L, m->settings[key]);
    else
        lua_pushvalue(L, 2);  // default
    return 1;
}

int api_command(lua_State* L) {
    auto* m = mod_of(L);
    ModHost::Mod::Command c;
    c.id = luaL_checkstring(L, 1);
    c.label = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    lua_pushvalue(L, 3);
    c.fn = luaL_ref(L, LUA_REGISTRYINDEX);
    m->commands.push_back(std::move(c));
    return 0;
}

int api_panel(lua_State* L) {
    auto* m = mod_of(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    ModHost::Mod::Panel p;
    lua_getfield(L, 1, "id");
    p.id = luaL_optstring(L, -1, "panel");
    lua_getfield(L, 1, "title");
    p.title = luaL_optstring(L, -1, "");
    lua_getfield(L, 1, "where");
    p.where = luaL_optstring(L, -1, "sidebar");
    lua_pop(L, 3);
    lua_getfield(L, 1, "draw");
    luaL_checktype(L, -1, LUA_TFUNCTION);
    p.draw = luaL_ref(L, LUA_REGISTRYINDEX);
    m->panels.push_back(std::move(p));
    return 0;
}

int api_decorate(lua_State* L) {
    auto* m = mod_of(L);
    const std::string kind = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    m->decorators[kind].push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    return 0;
}

// ---- ui widgets (inside a panel's draw)
ModPanel& drawing(lua_State* L) {
    auto* m = mod_of(L);
    if (m->drawing == nullptr)
        luaL_error(L, "ui functions only work inside a panel's draw function");
    return *m->drawing;
}

int ui_text(lua_State* L) {
    const int a = first_arg(L);
    ModWidget w;
    w.kind = ModWidget::Kind::kText;
    w.text = luaL_checkstring(L, a);
    w.color = luaL_optstring(L, a + 1, "");
    drawing(L).widgets.push_back(std::move(w));
    return 0;
}

bool consume_click(lua_State* L, const std::string& id);

int ui_button(lua_State* L) {
    const int a = first_arg(L);
    ModWidget w;
    w.kind = ModWidget::Kind::kButton;
    w.text = luaL_checkstring(L, a);
    w.id = luaL_optstring(L, a + 1, w.text.c_str());
    const bool clicked = consume_click(L, w.id);
    drawing(L).widgets.push_back(std::move(w));
    lua_pushboolean(L, clicked ? 1 : 0);
    return 1;
}

int ui_separator(lua_State* L) {
    ModWidget w;
    w.kind = ModWidget::Kind::kSeparator;
    drawing(L).widgets.push_back(std::move(w));
    return 0;
}

int ui_progress(lua_State* L) {
    const int a = first_arg(L);
    ModWidget w;
    w.kind = ModWidget::Kind::kProgress;
    w.value = static_cast<float>(std::clamp(luaL_checknumber(L, a), 0.0, 1.0));
    w.text = luaL_optstring(L, a + 1, "");
    drawing(L).widgets.push_back(std::move(w));
    return 0;
}

int ui_row(lua_State* L) {
    const int a = first_arg(L);
    ModWidget w;
    w.kind = ModWidget::Kind::kRow;
    w.text = luaL_checkstring(L, a);
    w.text2 = luaL_optstring(L, a + 1, "");
    w.color = luaL_optstring(L, a + 2, "");
    drawing(L).widgets.push_back(std::move(w));
    return 0;
}

int ui_badge(lua_State* L) {
    const int a = first_arg(L);
    ModWidget w;
    w.kind = ModWidget::Kind::kBadge;
    w.text = luaL_checkstring(L, a);
    w.color = luaL_optstring(L, a + 1, "accent");
    drawing(L).widgets.push_back(std::move(w));
    return 0;
}

void open_sandbox(lua_State* L) {
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    luaL_requiref(L, LUA_OSLIBNAME, luaopen_os, 1);
    lua_pop(L, 6);
    // No files, processes or code loading (agentdesk.exec is the one way to start a program).
    for (const char* name : {"dofile", "loadfile", "load", "require"}) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_getglobal(L, "os");
    for (const char* name : {"execute", "exit", "remove", "rename", "tmpname", "getenv", "setlocale"}) {
        lua_pushnil(L);
        lua_setfield(L, -2, name);
    }
    lua_pop(L, 1);
}

void register_api(lua_State* L) {
    static const luaL_Reg api[] = {{"on", l_on},
                                   {"run", api_run},
                                   {"send", api_send},
                                   {"notify", api_notify},
                                   {"exec", api_exec},
                                   {"sessions", api_sessions},
                                   {"focused", api_focused},
                                   {"setting", api_setting},
                                   {"command", api_command},
                                   {"panel", api_panel},
                                   {"decorate", api_decorate},
                                   {nullptr, nullptr}};
    luaL_newlib(L, api);
    lua_setglobal(L, "agentdesk");
    static const luaL_Reg ui[] = {
        {"text", ui_text}, {"button", ui_button}, {"separator", ui_separator}, {"progress", ui_progress},
        {"row", ui_row},   {"badge", ui_badge},   {nullptr, nullptr}};
    luaL_newlib(L, ui);
    lua_setfield(L, LUA_REGISTRYINDEX, "agentdesk.ui");
}

}  // namespace

// ------------------------------------------------------------------ ModHost
// Access to ModHost private state from the C callbacks.
struct ModHostAccess {
    static ModOutput& output(ModHost& h) { return h.output_; }
    static const std::vector<ModSession>& sessions(ModHost& h) { return h.sessions_; }
    static std::set<std::string>& clicked(ModHost& h) { return h.clicked_; }
};

namespace {
ModOutput& output_of(lua_State* L) {
    return ModHostAccess::output(*mod_of(L)->host);
}
const std::vector<ModSession>& sessions_of(lua_State* L) {
    return ModHostAccess::sessions(*mod_of(L)->host);
}
bool consume_click(lua_State* L, const std::string& id) {
    auto* m = mod_of(L);
    auto& clicked = ModHostAccess::clicked(*m->host);
    const std::string key = m->info.name + "/" + m->drawing->id + "/" + id;
    return clicked.erase(key) > 0;
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
        if (e.is_directory() && std::filesystem::is_regular_file(e.path() / L"main.lua", ec))
            folders.push_back(e.path());
    std::sort(folders.begin(), folders.end());
    for (const auto& f : folders) {
        auto m = std::make_unique<Mod>();
        m->host = this;
        m->folder = f;
        const auto name8 = f.filename().u8string();
        m->info.name.assign(name8.begin(), name8.end());
        m->info.enabled = enabled.contains(m->info.name);
        const std::string src = read_file(f / L"main.lua");
        if (src.rfind("--", 0) == 0) {
            const auto end = src.find('\n');
            std::string d = src.substr(2, end == std::string::npos ? std::string::npos : end - 2);
            while (!d.empty() && (d.front() == ' ' || d.front() == '-'))
                d.erase(d.begin());
            while (!d.empty() && (d.back() == '\r' || d.back() == ' '))
                d.pop_back();
            m->info.description = d;
        }
        if (std::filesystem::is_regular_file(f / L"settings.json", ec)) {
            const auto j = Json::parse(read_file(f / L"settings.json"), nullptr, false);
            if (j.is_object())
                m->settings = j;
        }
        if (m->info.enabled) {
            m->L = luaL_newstate();
            lua_pushlightuserdata(m->L, m.get());
            lua_setfield(m->L, LUA_REGISTRYINDEX, kModKey);
            open_sandbox(m->L);
            register_api(m->L);
            lua_sethook(m->L, hook, LUA_MASKCOUNT, kHookEvery);
            const std::string chunk = "@" + m->info.name + "/main.lua";
            if (luaL_loadbufferx(m->L, src.data(), src.size(), chunk.c_str(), "t") != LUA_OK) {
                const char* msg = lua_tostring(m->L, -1);
                m->info.error = msg != nullptr ? msg : "cannot load";
                lua_pop(m->L, 1);
            } else if (m->call(0, 0)) {
                m->info.loaded = true;
                m->info.error.clear();
                m->failures = 0;
            }
        }
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
    for (auto& m : mods_) {
        if (!m->info.loaded)
            continue;
        const auto it = m->handlers.find(event);
        if (it == m->handlers.end())
            continue;
        for (const int ref : it->second) {
            if (!m->info.loaded)
                break;
            lua_rawgeti(m->L, LUA_REGISTRYINDEX, ref);
            push_json(m->L, payload);
            m->call(1, 0);
        }
    }
}

std::vector<ModPanel> ModHost::panels(const std::string& where) {
    std::vector<ModPanel> out;
    for (auto& m : mods_) {
        if (!m->info.loaded)
            continue;
        for (const auto& p : m->panels) {
            if (p.where != where)
                continue;
            ModPanel panel{m->info.name, p.id, p.title, p.where, {}};
            m->drawing = &panel;
            lua_rawgeti(m->L, LUA_REGISTRYINDEX, p.draw);
            lua_getfield(m->L, LUA_REGISTRYINDEX, "agentdesk.ui");
            const bool ok = m->call(1, 0);
            m->drawing = nullptr;
            if (!ok) {
                panel.widgets.clear();
                panel.widgets.push_back(
                    {ModWidget::Kind::kText, "error: " + m->info.error, "", "bad", 0, ""});
            }
            out.push_back(std::move(panel));
            if (!m->info.loaded)
                break;
        }
    }
    return out;
}

void ModHost::click(const std::string& mod, const std::string& panel, const std::string& widget_id) {
    clicked_.insert(mod + "/" + panel + "/" + widget_id);
}

std::string ModHost::decorate(const std::string& kind, const Json& subject) {
    std::string out;
    for (auto& m : mods_) {
        if (!m->info.loaded)
            continue;
        const auto it = m->decorators.find(kind);
        if (it == m->decorators.end())
            continue;
        for (const int ref : it->second) {
            lua_rawgeti(m->L, LUA_REGISTRYINDEX, ref);
            push_json(m->L, subject);
            if (m->call(1, 1)) {
                if (const char* s = lua_tostring(m->L, -1); s != nullptr && *s != '\0')
                    out += (out.empty() ? "" : " ") + std::string(s).substr(0, 64);
                lua_pop(m->L, 1);
            }
        }
    }
    return out;
}

std::vector<ModCommand> ModHost::commands() const {
    std::vector<ModCommand> out;
    for (const auto& m : mods_) {
        if (!m->info.loaded)
            continue;
        for (const auto& c : m->commands)
            out.push_back({m->info.name, c.id, c.label});
    }
    return out;
}

void ModHost::run_command(const std::string& mod, const std::string& id) {
    for (auto& m : mods_) {
        if (m->info.name != mod || !m->info.loaded)
            continue;
        for (const auto& c : m->commands) {
            if (c.id == id) {
                lua_rawgeti(m->L, LUA_REGISTRYINDEX, c.fn);
                m->call(0, 0);
                return;
            }
        }
    }
}

ModOutput ModHost::take_output() {
    return std::exchange(output_, {});
}

}  // namespace agentdesk::ui
