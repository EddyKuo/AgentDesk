// fetch_conpty: downloads Microsoft.Windows.Console.ConPTY (MIT, microsoft/terminal) and places
//   <dest>\conpty\conpty.dll and <dest>\conpty\x64\OpenConsole.exe
// Version and SHA-256 values are pinned; nothing "latest" is ever fetched.
//   fetch_conpty [--dest <dir>]     (default: <source>\third_party\conpty\bin)
#include <windows.h>
// winhttp.h needs windows.h first.
#include <cstdio>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <string>

#include <winhttp.h>

#include "devkit.hpp"

namespace dk = agentdesk::devkit;

namespace {

constexpr wchar_t kVersion[] = L"1.24.260710001";
constexpr wchar_t kHost[] = L"github.com";
constexpr wchar_t kPath[] =
    L"/microsoft/terminal/releases/download/v1.24.11911.0/"
    L"Microsoft.Windows.Console.ConPTY.1.24.260710001.nupkg";
constexpr char kNupkgSha[] = "9382ad7becb7e4d84e300578d8e4f4df28f43d979d9055d978c42913c47e0e9d";

struct Payload {
    const wchar_t* in_package;
    const wchar_t* in_dest;
    const char* sha;
};
constexpr Payload kFiles[] = {
    {L"runtimes\\win-x64\\native\\conpty.dll", L"conpty\\conpty.dll",
     "39fba2713e2495117b1591ae8c32a3b904bea7aa66069cf7815e2844c76d75d8"},
    {L"build\\native\\runtimes\\x64\\OpenConsole.exe", L"conpty\\x64\\OpenConsole.exe",
     "b7fd936c2668b87b9ecf7b3366dc6568afc1c6f981874cba3e955a1c35cf8160"},
};

struct InternetCloser {
    void operator()(HINTERNET h) const noexcept { ::WinHttpCloseHandle(h); }
};
using UniqueInternet = std::unique_ptr<void, InternetCloser>;

[[noreturn]] void fail_winhttp(const char* what) {
    throw std::runtime_error(std::string(what) + " failed, error " + std::to_string(::GetLastError()));
}

// HTTPS GET; WinHTTP follows GitHub's redirect to its download host by itself.
void download(const dk::fs::path& to) {
    UniqueInternet session(::WinHttpOpen(L"agentdesk-fetch-conpty/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session)
        fail_winhttp("WinHttpOpen");
    UniqueInternet connection(::WinHttpConnect(session.get(), kHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection)
        fail_winhttp("WinHttpConnect");
    UniqueInternet request(::WinHttpOpenRequest(connection.get(), L"GET", kPath, nullptr, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request)
        fail_winhttp("WinHttpOpenRequest");
    if (!::WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                              0))
        fail_winhttp("WinHttpSendRequest");
    if (!::WinHttpReceiveResponse(request.get(), nullptr))
        fail_winhttp("WinHttpReceiveResponse");
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!::WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
        fail_winhttp("WinHttpQueryHeaders");
    if (status != 200)
        throw std::runtime_error("download failed: HTTP " + std::to_string(status));
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot write " + dk::u8(to));
    std::string buf(64 * 1024, '\0');
    for (;;) {
        DWORD n = 0;
        if (!::WinHttpReadData(request.get(), buf.data(), static_cast<DWORD>(buf.size()), &n))
            fail_winhttp("WinHttpReadData");
        if (n == 0)
            break;
        out.write(buf.data(), n);
    }
    if (!out.flush())
        throw std::runtime_error("cannot write " + dk::u8(to));
}

void assert_sha256(const dk::fs::path& path, const char* expected) {
    const auto actual = dk::sha256_hex(path);
    if (actual != expected)
        throw std::runtime_error(dk::u8(path) + " sha256 " + actual + ", expected " + expected);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const dk::Args args(argc, argv);
        const dk::fs::path dest =
            args.path_or(L"dest", (dk::source_root() / L"third_party" / L"conpty" / L"bin").native());

        const auto work = dk::temp_dir() / (std::wstring(L"agentdesk-conpty-") + kVersion);
        const dk::fs::path zip = work.native() + L".zip";
        if (!dk::fs::exists(zip)) {
            const dk::fs::path part = zip.native() + L".part";
            download(part);
            dk::fs::rename(part, zip);
        }
        assert_sha256(zip, kNupkgSha);
        dk::remove_tree(work);
        dk::fs::create_directories(work);
        const auto tar =
            dk::run(dk::system_dir() / L"tar.exe", {L"-x", L"-f", zip.native(), L"-C", work.native()});
        if (tar.exit_code != 0)
            throw std::runtime_error("tar could not extract " + dk::u8(zip));

        dk::fs::create_directories(dest / L"conpty" / L"x64");
        for (const auto& f : kFiles) {
            const auto src = work / f.in_package;
            assert_sha256(src, f.sha);
            dk::fs::copy_file(src, dest / f.in_dest, dk::fs::copy_options::overwrite_existing);
        }
        for (const wchar_t* legal : {L"LICENSE", L"LICENSE.txt", L"NOTICE.md", L"NOTICE.txt"}) {
            if (dk::fs::exists(work / legal))
                dk::fs::copy_file(work / legal, dest / L"conpty" / legal,
                                  dk::fs::copy_options::overwrite_existing);
        }
        dk::remove_tree(work);
        std::printf("ConPTY %s -> %s\n", dk::u8(kVersion).c_str(),
                    dk::u8(dk::fs::canonical(dest / L"conpty")).c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fetch_conpty: %s\n", e.what());
        return 1;
    }
}
