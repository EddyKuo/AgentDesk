#pragma once
// C++ helper for writing an AgentDesk mod (header only). Derive from agentdesk::mod::Mod,
// override what you need, and export it:
//
//   #include <agentdesk/mod_sdk.hpp>
//   class Hello : public agentdesk::mod::Mod {
//   public:
//       Hello() { host().add_command("hi", "Say hi"); }
//       void command(const std::string& id) override { host().notify("hi"); }
//   };
//   AGENTDESK_MOD(Hello, "Says hello")
//
// Build it as a DLL named mod.dll and put it in <data>\mods\<name>\. Exceptions never cross
// into AgentDesk: one escaping a handler is caught here and reported as a notification.
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentdesk/mod_api.h"

namespace agentdesk::mod {

using Json = nlohmann::json;

class Host {
public:
    explicit Host(const agentdesk_host* h = nullptr) : h_(h) {}
    void run(const std::string& command) const { h_->run(h_->ctx, command.c_str()); }
    void send(const std::string& session, const std::string& text, bool enter = false) const {
        h_->send(h_->ctx, session.c_str(), text.c_str(), enter ? 1 : 0);
    }
    void notify(const std::string& title, const std::string& body = {}) const {
        h_->notify(h_->ctx, title.c_str(), body.c_str());
    }
    void exec(const std::vector<std::string>& argv) const {
        std::vector<const char*> p;
        for (const auto& a : argv)
            p.push_back(a.c_str());
        h_->exec(h_->ctx, p.data(), static_cast<int>(p.size()));
    }
    void add_command(const std::string& id, const std::string& label) const {
        h_->add_command(h_->ctx, id.c_str(), label.c_str());
    }
    [[nodiscard]] Json sessions() const {
        const char* s = h_->sessions(h_->ctx);
        auto j = Json::parse(s != nullptr ? s : "[]", nullptr, false);
        return j.is_array() ? j : Json::array();
    }
    // The value of `key` in the mod's settings.json, or `fallback`.
    [[nodiscard]] Json setting(const std::string& key, Json fallback = nullptr) const {
        const char* s = h_->setting(h_->ctx, key.c_str());
        if (s == nullptr)
            return fallback;
        auto j = Json::parse(s, nullptr, false);
        return j.is_discarded() ? fallback : j;
    }

private:
    const agentdesk_host* h_;
};

// Panel builder: widgets in order (the same widgets the window draws for every mod).
class Panel {
public:
    Panel(std::string id, std::string title) : j_{{"id", std::move(id)}, {"title", std::move(title)}, {"widgets", Json::array()}} {}
    Panel& text(const std::string& t, const std::string& color = {}) { return add({{"kind", "text"}, {"text", t}, {"color", color}}); }
    Panel& row(const std::string& left, const std::string& right, const std::string& color = {}) {
        return add({{"kind", "row"}, {"text", left}, {"text2", right}, {"color", color}});
    }
    Panel& badge(const std::string& t, const std::string& color = {}) { return add({{"kind", "badge"}, {"text", t}, {"color", color}}); }
    Panel& progress(float value, const std::string& label = {}) { return add({{"kind", "progress"}, {"value", value}, {"text", label}}); }
    Panel& button(const std::string& id, const std::string& label) { return add({{"kind", "button"}, {"id", id}, {"text", label}}); }
    Panel& separator() { return add({{"kind", "separator"}}); }
    [[nodiscard]] const Json& json() const { return j_; }

private:
    Panel& add(Json w) {
        j_["widgets"].push_back(std::move(w));
        return *this;
    }
    Json j_;
};

class Mod {
public:
    virtual ~Mod() = default;
    virtual void on_event(const std::string& /*event*/, const Json& /*payload*/) {}
    virtual std::vector<Panel> panels(const std::string& /*where*/) { return {}; }
    virtual void click(const std::string& /*panel*/, const std::string& /*widget*/) {}
    virtual std::string decorate(const std::string& /*kind*/, const Json& /*subject*/) { return {}; }
    virtual void command(const std::string& /*id*/) {}

    [[nodiscard]] const Host& host() const { return host_; }

    // ---- glue (used by AGENTDESK_MOD)
    static Host& current_host() {
        static Host h;
        return h;
    }
    Host host_ = current_host();
    std::string out_;  // last string handed to AgentDesk
};

namespace detail {

template <typename F>
void guarded(Mod* m, F&& f) {
    try {
        f();
    } catch (const std::exception& e) {
        m->host().notify("Mod error", e.what());
    } catch (...) {
        m->host().notify("Mod error", "unknown exception");
    }
}

template <typename T>
int init(const agentdesk_host* host, agentdesk_mod* out, const char* description) {
    if (host == nullptr || host->api_version != AGENTDESK_MOD_API_VERSION)
        return 1;
    Mod::current_host() = Host(host);
    Mod* m = nullptr;
    try {
        m = new T();
    } catch (...) {
        return 2;
    }
    out->api_version = AGENTDESK_MOD_API_VERSION;
    out->self = m;
    out->description = description;
    out->on_event = [](void* self, const char* event, const char* payload) {
        auto* mod = static_cast<Mod*>(self);
        guarded(mod, [&] {
            auto j = Json::parse(payload != nullptr ? payload : "{}", nullptr, false);
            mod->on_event(event != nullptr ? event : "", j.is_discarded() ? Json::object() : j);
        });
    };
    out->panels = [](void* self, const char* where) -> const char* {
        auto* mod = static_cast<Mod*>(self);
        mod->out_ = "[]";
        guarded(mod, [&] {
            Json list = Json::array();
            for (const auto& p : mod->panels(where != nullptr ? where : ""))
                list.push_back(p.json());
            mod->out_ = list.dump(-1, ' ', false, Json::error_handler_t::replace);
        });
        return mod->out_.c_str();
    };
    out->click = [](void* self, const char* panel, const char* widget) {
        auto* mod = static_cast<Mod*>(self);
        guarded(mod, [&] { mod->click(panel != nullptr ? panel : "", widget != nullptr ? widget : ""); });
    };
    out->decorate = [](void* self, const char* kind, const char* subject) -> const char* {
        auto* mod = static_cast<Mod*>(self);
        mod->out_.clear();
        guarded(mod, [&] {
            auto j = Json::parse(subject != nullptr ? subject : "{}", nullptr, false);
            mod->out_ = mod->decorate(kind != nullptr ? kind : "", j.is_discarded() ? Json::object() : j);
        });
        return mod->out_.c_str();
    };
    out->command = [](void* self, const char* id) {
        auto* mod = static_cast<Mod*>(self);
        guarded(mod, [&] { mod->command(id != nullptr ? id : ""); });
    };
    out->destroy = [](void* self) { delete static_cast<Mod*>(self); };
    return 0;
}

}  // namespace detail
}  // namespace agentdesk::mod

#define AGENTDESK_MOD(Class, Description)                                                              \
    extern "C" __declspec(dllexport) int agentdesk_mod_init(const agentdesk_host* host, agentdesk_mod* mod) { \
        return ::agentdesk::mod::detail::init<Class>(host, mod, Description);                          \
    }
