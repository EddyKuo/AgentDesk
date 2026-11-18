// make_fixtures: regenerates the byte-exact VT fixtures under tests/fixtures/bytes.
// Fixtures are committed; regenerate only when intentionally changing them and review the diff.
//   make_fixtures [--out <dir>]     (default: <source>\tests\fixtures\bytes)
#include <cstdio>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "devkit.hpp"

namespace dk = agentdesk::devkit;

namespace {

const std::string kEsc = "\x1b";
const std::string kSt = kEsc + "\\";
const std::string kBel = "\x07";

std::string mixed_sgr_utf8() {
    return kEsc + "]2;fixture title" + kBel +                                                           //
           kEsc + "[1;31mRED" + kEsc + "[0m plain " + kEsc + "[38;2;10;20;30mrgb" + kEsc + "[0m\r\n" +  //
           // "e" plus a combining acute accent (U+0301), not the precomposed U+00E9.
           "中文寬字 é emoji \U0001F600 end\r\n" +                        //
           kEsc + "[3;5Hpos" + kEsc + "[K\r\n" +                          //
           "line\r\nline2" + kEsc + "[2J" + kEsc + "[Hafter clear\r\n" +  //
           kEsc + "[?1049halt screen text" + kEsc + "[?1049l" +           //
           kEsc + "]7501;state=working:app=fixture:progress=40" + kSt +   //
           "tail 測試\r\n" +                                              //
           kEsc + "]7501;state=blocked:kind=permission:msg=bmVlZCBhcHByb3ZhbA==" + kBel;
}

std::string scrolling_output() {
    std::string out;
    char num[8];
    for (int i = 0; i < 60; ++i) {
        std::snprintf(num, sizeof(num), "%03d", i);
        out += "line " + std::string(num) + " 第" + std::to_string(i) + "行\r\n";
    }
    return out + kEsc + "[1;32mdone" + kEsc + "[0m";
}

std::string status_query_then_da() {
    return kEsc + "]7501;?" + kSt + kEsc + "[c";
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const dk::Args args(argc, argv);
        const dk::fs::path dir =
            args.path_or(L"out", (dk::source_root() / L"tests" / L"fixtures" / L"bytes").native());
        dk::fs::create_directories(dir);
        const std::vector<std::pair<const char*, std::string>> files = {
            {"mixed_sgr_utf8.bin", mixed_sgr_utf8()},
            {"scrolling_output.bin", scrolling_output()},
            {"status_query_then_da.bin", status_query_then_da()},
        };
        for (const auto& [name, data] : files) {
            dk::write_file(dir / name, data);
            std::printf("%s %zu\n", name, data.size());
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "make_fixtures: %s\n", e.what());
        return 1;
    }
}
