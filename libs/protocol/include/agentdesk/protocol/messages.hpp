#pragma once

// IPC protocol v1 messages (design doc 3.7). JSON envelopes:
//   request  {"v":1,"id":"r-1","method":"session.create","params":{...}}
//   response {"v":1,"id":"r-1","result":{...}}  or  {"v":1,"id":"r-1","error":{code,message,retryable}}
//   event    {"v":1,"event":"screen.delta","session_id":"...", ...}

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <nlohmann/json.hpp>

#include "agentdesk/terminal/vt_engine.hpp"

namespace agentdesk::protocol {

using Json = nlohmann::json;

enum class ErrorCode {
    kBadRequest,
    kNotFound,
    kProfileInvalid,
    kSpawnFailed,
    kNotController,
    kVersionMismatch,
    kOverloaded,
    kActiveSessions,
    kAmbiguous,  // name lookup matched several sessions (CLI exit code 4)
    kInternal,
};

std::string_view to_string(ErrorCode c);
std::optional<ErrorCode> parse_error_code(std::string_view s);

struct Request {
    std::string id;
    std::string method;
    Json params = Json::object();
};

struct Response {
    std::string id;
    std::optional<Json> result;
    std::optional<ErrorCode> error;
    std::string error_message;
    bool retryable = false;
};

struct Event {
    std::string event;
    Json body = Json::object();  // all fields except v/event
};

using Message = std::variant<Request, Response, Event>;

// Parses one frame payload. Unknown versions and malformed envelopes return an error string.
struct ParseResult {
    std::optional<Message> message;
    std::string error;  // set when message is empty
    bool version_mismatch = false;
};
ParseResult parse_message(std::string_view payload);

std::string serialize(const Request& r);
std::string serialize(const Response& r);
std::string serialize(const Event& e);

Response ok(std::string id, Json result);
Response fail(std::string id, ErrorCode code, std::string message, bool retryable = false);

// Screen encoding. Colours are 0xRRGGBB integers or -1 for the theme default.
Json encode_cell(const terminal::Cell& c);
terminal::Cell decode_cell(const Json& j);
Json encode_rows(const terminal::ScreenSnapshot& s, int first_row, int last_row);  // [first,last)
// Full snapshot (rows split into chunks of at most `rows_per_chunk` by the caller if needed).
Json encode_snapshot_meta(const terminal::ScreenSnapshot& s);
void decode_rows_into(const Json& rows, terminal::ScreenSnapshot& s);

}  // namespace agentdesk::protocol
