// Child processes for the updater and telemetry, and small file and text helpers.
#include "system_process.hpp"

#include <cctype>
#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <thread>

namespace agentdesk::ui::detail {

ProcessResult run_process(const std::filesystem::path& exe, const std::vector<std::wstring>& args,
                          const std::string& input, const std::function<bool()>& keep_going) {
    ProcessResult r;
    platform::ProcessSpec spec;
    spec.executable = exe;
    for (const auto& a : args)
        spec.args.push_back(to_utf8(a));
    spec.pipe_stdin = true;
    spec.pipe_stdout = true;
    spec.pipe_stderr = true;
    prepare_tool(spec);
    std::unique_ptr<platform::Process> child;
    try {
        child = platform::Process::spawn(spec);
    } catch (const std::exception&) {
        return r;
    }
    r.started = true;
    std::thread writer([&] {
        if (!input.empty())
            child->write(input);
        child->close_stdin();
    });
    std::thread reader([&] {
        while (auto chunk = child->read_some(64 * 1024))
            r.out += *chunk;
    });
    std::thread err_reader([&] {
        while (auto chunk = child->read_some_stderr(64 * 1024))
            r.err += *chunk;
    });
    bool killed = false;
    std::optional<int> code;
    while (!(code = child->wait(std::chrono::milliseconds(50)))) {
        if (keep_going && !keep_going()) {
            killed = true;
            child->kill();
            code = child->wait(std::chrono::seconds(5));
            break;
        }
    }
    writer.join();
    reader.join();
    err_reader.join();
    r.exit_code = killed ? 1ul : static_cast<std::uint32_t>(code.value_or(1));
    return r;
}

// The variable goes into the child's environment only: changing ours would race with other
// threads reading it.
bool spawn_detached(const std::filesystem::path& exe, const std::vector<std::wstring>& args,
                    const std::wstring& env_name, const std::wstring& env_value) {
    std::map<std::string, std::string> env;
    if (!env_name.empty())
        env[to_utf8(env_name)] = to_utf8(env_value);
    std::vector<std::string> a;
    for (const auto& w : args)
        a.push_back(to_utf8(w));
    return start_in_background(exe, a, env);
}

std::string trim(std::string_view s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos)
        return {};
    return std::string(s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1));
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string read_file(const std::filesystem::path& p, bool* ok) {
    std::ifstream in(p, std::ios::binary);
    if (ok != nullptr)
        *ok = static_cast<bool>(in);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::uint64_t unix_now() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
}

}  // namespace agentdesk::ui::detail
