#pragma once

// Client-side screen state rebuilt from host events (design doc 3.7, 3.7.1). Pure logic, no I/O.

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "agentdesk/terminal/vt_engine.hpp"

namespace agentdesk::ui {

enum class DeltaResult {
    kApplied,
    kIgnoredStale,  // older than what we have (late delta after a newer snapshot)
    kNeedSnapshot,  // generation changed or seq gap: drop deltas until a new snapshot arrives
};

class RemoteScreen {
public:
    // Returns true when this chunk completed a snapshot and it became current.
    bool apply_snapshot_chunk(const nlohmann::json& event);
    DeltaResult apply_delta(const nlohmann::json& event);

    [[nodiscard]] std::shared_ptr<const terminal::ScreenSnapshot> current() const { return current_; }
    [[nodiscard]] bool needs_snapshot() const noexcept { return needs_snapshot_; }
    // Also drops snapshot assemblies in flight (the next snapshot replaces them).
    void mark_needs_snapshot() noexcept;
    // Snapshots partly received (at most one: a session's snapshots arrive one after another).
    [[nodiscard]] std::size_t pending_count() const noexcept { return pending_.size(); }

private:
    struct Pending {
        int chunk_count = 0;
        int received = 0;
        std::shared_ptr<terminal::ScreenSnapshot> snap;
    };
    static void apply_meta(const nlohmann::json& meta, terminal::ScreenSnapshot& s);

    std::shared_ptr<const terminal::ScreenSnapshot> current_;
    std::map<std::string, Pending> pending_;  // by snapshot_id
    bool needs_snapshot_ = true;
};

}  // namespace agentdesk::ui
