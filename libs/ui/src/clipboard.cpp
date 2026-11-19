// Reads the Windows clipboard for a terminal paste: dropped-file lists, Unicode text, images.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "agentdesk/ui/clipboard.hpp"

#include <windows.h>

#include <shellapi.h>

#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::ui {

ClipboardContent read_clipboard() {
    ClipboardContent out;
    // Another program may hold the clipboard for a moment.
    bool open = false;
    for (int attempt = 0; attempt < 5 && !open; ++attempt) {
        open = OpenClipboard(nullptr) != FALSE;
        if (!open)
            Sleep(10);
    }
    if (!open)
        return out;
    try {
        if (IsClipboardFormatAvailable(CF_HDROP)) {
            if (HANDLE h = GetClipboardData(CF_HDROP); h != nullptr) {
                if (auto* drop = static_cast<HDROP>(GlobalLock(h)); drop != nullptr) {
                    const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                    for (UINT i = 0; i < n; ++i) {
                        std::wstring path(DragQueryFileW(drop, i, nullptr, 0), L'\0');
                        DragQueryFileW(drop, i, path.data(), static_cast<UINT>(path.size() + 1));
                        if (!path.empty())
                            out.files.push_back(platform::win::to_utf8(path));
                    }
                    GlobalUnlock(h);
                }
            }
        }
        if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT); h != nullptr) {
                if (const auto* w = static_cast<const wchar_t*>(GlobalLock(h)); w != nullptr) {
                    const std::size_t cap = GlobalSize(h) / sizeof(wchar_t);
                    std::size_t len = 0;
                    while (len < cap && w[len] != L'\0')
                        ++len;
                    out.text = platform::win::to_utf8(std::wstring_view(w, len));
                    GlobalUnlock(h);
                }
            }
        }
        static const UINT kPng = RegisterClipboardFormatW(L"PNG");
        out.image = IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_DIBV5) ||
                    IsClipboardFormatAvailable(CF_BITMAP) || (kPng != 0 && IsClipboardFormatAvailable(kPng));
    } catch (const std::exception&) {
        // Invalid UTF-16 on the clipboard: treat that part as absent.
    }
    CloseClipboard();
    return out;
}

}  // namespace agentdesk::ui
