#include "agentdesk/ui/remote_screen.hpp"

#include "agentdesk/protocol/messages.hpp"

namespace agentdesk::ui {

void RemoteScreen::apply_meta(const nlohmann::json& meta, terminal::ScreenSnapshot& s) {
    s.generation = meta.value("generation", static_cast<std::uint64_t>(0));
    s.seq = meta.value("seq", static_cast<std::uint64_t>(0));
    s.alt_screen = meta.value("alt_screen", false);
    s.title = meta.value("title", "");
    if (const auto it = meta.find("cursor"); it != meta.end()) {
        s.cursor.row = it->value("row", 0);
        s.cursor.col = it->value("col", 0);
        s.cursor.visible = it->value("visible", true);
    }
}

bool RemoteScreen::apply_snapshot_chunk(const nlohmann::json& event) {
    const std::string id = event.value("snapshot_id", "");
    const int count = event.value("chunk_count", 1);
    if (id.empty() || count < 1)
        return false;
    auto& p = pending_[id];
    if (!p.snap) {
        const auto& meta = event.at("meta");
        p.chunk_count = count;
        p.snap = std::make_shared<terminal::ScreenSnapshot>();
        p.snap->rows = meta.value("rows", 0);
        p.snap->cols = meta.value("cols", 0);
        p.snap->cells.resize(static_cast<std::size_t>(p.snap->rows) * static_cast<std::size_t>(p.snap->cols));
        apply_meta(meta, *p.snap);
    }
    protocol::decode_rows_into(event.at("rows"), *p.snap);
    if (++p.received < p.chunk_count)
        return false;
    // Complete: switch atomically (design doc 3.7: partial snapshots are never shown).
    const bool newer =
        !current_ || p.snap->generation != current_->generation || p.snap->seq >= current_->seq;
    if (newer) {
        current_ = std::move(p.snap);
        needs_snapshot_ = false;
    }
    pending_.erase(id);
    return newer;
}

DeltaResult RemoteScreen::apply_delta(const nlohmann::json& event) {
    if (!current_ || needs_snapshot_)
        return DeltaResult::kNeedSnapshot;
    const auto generation = event.value("generation", static_cast<std::uint64_t>(0));
    const auto base = event.value("base_seq", static_cast<std::uint64_t>(0));
    const auto seq = event.value("seq", static_cast<std::uint64_t>(0));
    if (generation != current_->generation) {
        needs_snapshot_ = true;
        return DeltaResult::kNeedSnapshot;
    }
    if (seq <= current_->seq)
        return DeltaResult::kIgnoredStale;
    if (base != current_->seq) {
        needs_snapshot_ = true;  // gap: deltas were dropped (client queue over budget)
        return DeltaResult::kNeedSnapshot;
    }
    auto next = std::make_shared<terminal::ScreenSnapshot>(*current_);
    if (const auto meta = event.find("meta"); meta != event.end()) {
        if (meta->value("rows", next->rows) != next->rows || meta->value("cols", next->cols) != next->cols) {
            needs_snapshot_ = true;
            return DeltaResult::kNeedSnapshot;
        }
        apply_meta(*meta, *next);
    }
    protocol::decode_rows_into(event.at("rows"), *next);
    next->seq = seq;
    current_ = std::move(next);
    return DeltaResult::kApplied;
}

}  // namespace agentdesk::ui
