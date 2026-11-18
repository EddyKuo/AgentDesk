#include "devkit.hpp"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <bcrypt.h>

#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::devkit {

namespace win = platform::win;
using namespace std::chrono_literals;

fs::path source_root() {
    return fs::path(win::to_wide(AGENTDESK_SOURCE_DIR)).make_preferred();
}

fs::path exe_dir() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return fs::path(buf).parent_path();
}

fs::path temp_dir() {
    auto p = fs::temp_directory_path().native();
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/'))
        p.pop_back();
    return p;
}

fs::path system_dir() {
    wchar_t buf[MAX_PATH]{};
    const UINT n = ::GetSystemDirectoryW(buf, MAX_PATH);
    return std::wstring(buf, n);
}

std::wstring pid_string() {
    return std::to_wstring(::GetCurrentProcessId());
}

namespace {

std::wstring normalize_name(std::wstring_view raw) {
    std::wstring out;
    for (const wchar_t c : raw) {
        if (c != L'-' && c != L'_')
            out.push_back(static_cast<wchar_t>(std::towlower(c)));
    }
    return out;
}

bool looks_like_option(const std::wstring& a) {
    // "-5" is a value, not an option.
    return a.size() >= 2 && a[0] == L'-' && !std::iswdigit(a[1]);
}

}  // namespace

Args::Args(int argc, wchar_t** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (!looks_like_option(a)) {
            positional_.push_back(a);
            continue;
        }
        const auto eq = a.find(L'=');
        if (eq != std::wstring::npos) {
            named_.emplace_back(normalize_name(a.substr(0, eq)), a.substr(eq + 1));
        } else if (i + 1 < argc) {
            named_.emplace_back(normalize_name(a), argv[++i]);
        } else {
            throw std::invalid_argument("missing value for " + win::to_utf8(a));
        }
    }
}

std::optional<std::wstring> Args::get(std::wstring_view name) const {
    const auto key = normalize_name(name);
    for (auto it = named_.rbegin(); it != named_.rend(); ++it) {
        if (it->first == key)
            return it->second;
    }
    return std::nullopt;
}

std::wstring Args::get_or(std::wstring_view name, std::wstring fallback) const {
    auto v = get(name);
    return v && !v->empty() ? *v : std::move(fallback);
}

fs::path Args::path_or(std::wstring_view name, const fs::path& fallback) const {
    return fs::absolute(get_or(name, fallback.native()));
}

int Args::get_int(std::wstring_view name, int fallback) const {
    const auto v = get(name);
    return v ? std::stoi(*v) : fallback;
}

RunResult run(const fs::path& exe, const std::vector<std::wstring>& args, const fs::path& cwd) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE read_raw = nullptr;
    HANDLE write_raw = nullptr;
    if (!::CreatePipe(&read_raw, &write_raw, &sa, 0))
        win::throw_last_error("CreatePipe");
    win::UniqueHandle read_end(read_raw);
    win::UniqueHandle write_end(write_raw);
    ::SetHandleInformation(read_end.get(), HANDLE_FLAG_INHERIT, 0);

    // Only the stdout pipe and a copy of our stderr are inherited, nothing else this tool holds.
    win::UniqueHandle err_dup;
    const HANDLE err = ::GetStdHandle(STD_ERROR_HANDLE);
    HANDLE dup = nullptr;
    if (err != nullptr && err != INVALID_HANDLE_VALUE &&
        ::DuplicateHandle(::GetCurrentProcess(), err, ::GetCurrentProcess(), &dup, 0, TRUE,
                          DUPLICATE_SAME_ACCESS)) {
        err_dup.reset(dup);
    }
    std::vector<HANDLE> inherit = {write_end.get()};
    if (err_dup)
        inherit.push_back(err_dup.get());
    win::AttributeList attrs(1);
    attrs.update(PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit.data(), inherit.size() * sizeof(HANDLE));
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nullptr;
    si.StartupInfo.hStdOutput = write_end.get();
    si.StartupInfo.hStdError = err_dup.get();
    si.lpAttributeList = attrs.get();
    std::wstring cmd = win::build_command_line(exe.native(), args);
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT,
                          nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi)) {
        throw std::runtime_error("cannot start " + u8(exe));
    }
    win::UniqueHandle process(pi.hProcess);
    ::CloseHandle(pi.hThread);
    write_end.reset();  // EOF arrives once the child (and anything it handed stdout to) exits

    RunResult r;
    char buf[4096];
    DWORD n = 0;
    while (::ReadFile(read_end.get(), buf, sizeof(buf), &n, nullptr) && n > 0)
        r.out.append(buf, n);
    ::WaitForSingleObject(process.get(), INFINITE);
    ::GetExitCodeProcess(process.get(), &r.exit_code);
    return r;
}

Started start(const fs::path& exe, const std::vector<std::wstring>& args, const fs::path& cwd) {
    std::wstring cmd = win::build_command_line(exe.native(), args);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                          cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
        throw std::runtime_error("cannot start " + u8(exe));
    }
    ::CloseHandle(pi.hThread);
    return {win::UniqueHandle(pi.hProcess), pi.dwProcessId};
}

bool wait_exit(const Started& p, std::chrono::milliseconds timeout) {
    return ::WaitForSingleObject(p.process.get(), static_cast<DWORD>(timeout.count())) == WAIT_OBJECT_0;
}

DWORD exit_code(const Started& p) {
    DWORD code = 0;
    ::GetExitCodeProcess(p.process.get(), &code);
    return code;
}

void kill(const Started& p) {
    if (p.process && ::WaitForSingleObject(p.process.get(), 0) == WAIT_TIMEOUT) {
        ::TerminateProcess(p.process.get(), 1);
        ::WaitForSingleObject(p.process.get(), 5000);
    }
}

bool process_alive(DWORD pid) {
    win::UniqueHandle h(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
    if (!win::is_valid(h))
        return false;
    return ::WaitForSingleObject(h.get(), 0) == WAIT_TIMEOUT;
}

std::wstring process_image_path(DWORD pid) {
    win::UniqueHandle h(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!win::is_valid(h))
        return {};
    std::wstring buf(32768, L'\0');
    DWORD n = static_cast<DWORD>(buf.size());
    if (!::QueryFullProcessImageNameW(h.get(), 0, buf.data(), &n))
        return {};
    buf.resize(n);
    return buf;
}

DWORD host_pid(const std::wstring& pipe) {
    auto client = ipc::PipeClient::connect(pipe, 2s);
    if (!client)
        return 0;
    const auto hello = client->call("hello", {{"version", 1}, {"client", "agentdesk-devtools"}});
    client->close();
    if (!hello || !hello->result)
        return 0;
    return hello->result->value("host_pid", DWORD{0});
}

void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + u8(path));
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_file(const fs::path& path, std::string_view bytes, bool bom) {
    if (path.has_parent_path())
        fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot write " + u8(path));
    if (bom)
        out << "\xEF\xBB\xBF";
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out)
        throw std::runtime_error("cannot write " + u8(path));
}

Json read_json(const fs::path& path) {
    auto text = read_file(path);
    if (text.starts_with("\xEF\xBB\xBF"))
        text.erase(0, 3);
    return Json::parse(text);
}

void copy_tree(const fs::path& from, const fs::path& to) {
    fs::create_directories(to);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
}

void remove_tree(const fs::path& path) noexcept {
    std::error_code ec;
    fs::remove_all(path, ec);
}

std::string sha256_hex(const fs::path& path) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
        throw std::runtime_error("BCryptOpenAlgorithmProvider");
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (!BCRYPT_SUCCESS(::BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
        ::BCryptCloseAlgorithmProvider(alg, 0);
        throw std::runtime_error("BCryptCreateHash");
    }
    std::ifstream in(path, std::ios::binary);
    bool ok = static_cast<bool>(in);
    std::vector<char> buf(1 << 20);
    while (ok && in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const auto n = static_cast<ULONG>(in.gcount());
        if (n > 0)
            ok = BCRYPT_SUCCESS(::BCryptHashData(hash, reinterpret_cast<PUCHAR>(buf.data()), n, 0));
    }
    unsigned char digest[32]{};
    ok = ok && BCRYPT_SUCCESS(::BCryptFinishHash(hash, digest, sizeof(digest), 0));
    ::BCryptDestroyHash(hash);
    ::BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok)
        throw std::runtime_error("cannot hash " + u8(path));
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    for (const unsigned char b : digest) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 15]);
    }
    return out;
}

std::string utc_now_iso() {
    FILETIME ft{};
    ::GetSystemTimePreciseAsFileTime(&ft);
    SYSTEMTIME st{};
    ::FileTimeToSystemTime(&ft, &st);
    const auto ticks = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u.%07lluZ", st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond, static_cast<unsigned long long>(ticks % 10000000ULL));
    return buf;
}

std::string ps_json(const Json& object) {
    std::string out = "{\n";
    std::size_t i = 0;
    for (const auto& [key, value] : object.items()) {
        out += "    " + Json(key).dump() + ":  " + value.dump(-1, ' ', false, Json::error_handler_t::replace);
        out += ++i < object.size() ? ",\n" : "\n";
    }
    out += "}";
    return out;
}

std::string profiles_json(const Json& profiles) {
    return Json{{"schema_version", 1}, {"profiles", profiles}}.dump(2);
}

std::string u8(const fs::path& path) {
    return win::to_utf8(path.native());
}

std::wstring wide(std::string_view text) {
    return win::to_wide(text);
}

}  // namespace agentdesk::devkit
