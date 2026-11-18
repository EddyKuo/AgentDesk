#pragma once

// OSC 7501 Program Status Protocol, Revision 0.4 (2026-10-10).
// https://www.superlogical.com/rex/docs/build/program-status

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace agentdesk::terminal {

enum class StatusState { kIdle, kWorking, kDone, kBlocked, kError, kClear };
enum class StatusKind { kNone, kPermission, kQuestion, kAuth };

struct StatusReport {
    StatusState state = StatusState::kIdle;
    std::string id;                        // empty = root record
    StatusKind kind = StatusKind::kNone;   // only meaningful for kBlocked
    std::optional<std::uint8_t> progress;  // only for kWorking / kBlocked
    std::string app;
    std::string title;  // decoded UTF-8, control characters rejected
    std::string msg;    // decoded UTF-8, control characters rejected
};

struct StatusQuery {};

using Osc7501Message = std::variant<StatusQuery, StatusReport>;

// Spec limits (bytes).
inline constexpr std::size_t kMaxSequenceBytes = 4096;
// Body = everything after "7501;" (ESC ] 7 5 0 1 ; = 7 bytes, ST up to 2 bytes).
inline constexpr std::size_t kMaxBodyBytes = kMaxSequenceBytes - 7 - 2;
inline constexpr std::size_t kMaxRecords = 256;

// Parses the OSC body (without the "7501;" prefix). Returns nullopt when the whole report
// must be discarded (unknown state, malformed id, limits, bad base64, control characters).
std::optional<Osc7501Message> parse_osc7501(std::string_view body);

// Reply for a supporting terminal; omitted lists mean the default state/kind sets.
std::string osc7501_query_reply();

std::string_view to_string(StatusState state);
std::string_view to_string(StatusKind kind);

struct StatusRecord {
    StatusReport report;
    std::uint64_t updated_order = 0;  // monotonic, for least-recently-updated eviction
};

// Records per terminal. Each report replaces its record entirely; `clear` removes a record
// and its descendants (no id: everything).
class ProgramStatusStore {
public:
    void apply(const StatusReport& report);

    // Lifetime rules: on process exit or a new shell prompt (OSC 133 A) working and blocked
    // records are dropped; done and error survive. Idle records are kept.
    void on_process_exit();
    void on_shell_prompt();
    void reset();  // RIS

    [[nodiscard]] const StatusRecord* root() const;
    [[nodiscard]] const std::map<std::string, StatusRecord>& records() const noexcept { return records_; }
    // `app` of a record, inherited from the nearest ancestor when absent.
    [[nodiscard]] std::string effective_app(const std::string& id) const;

    // agent-host's handover (design doc 7.4 OQ-09): the records and their order, as they are.
    [[nodiscard]] std::uint64_t counter() const noexcept { return counter_; }
    void restore(std::map<std::string, StatusRecord> records, std::uint64_t counter) {
        records_ = std::move(records);
        counter_ = counter;
    }

private:
    void drop_transient();

    std::map<std::string, StatusRecord> records_;  // key "" = root
    std::uint64_t counter_ = 0;
};

}  // namespace agentdesk::terminal
