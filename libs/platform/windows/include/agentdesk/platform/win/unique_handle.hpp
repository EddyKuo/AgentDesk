#pragma once

// RAII owners for Win32 handles and process/thread attribute lists.

#include <windows.h>

#include <memory>

namespace agentdesk::platform::win {

// Owns a kernel HANDLE. Accepts both failure sentinels (nullptr and INVALID_HANDLE_VALUE)
// so callers can wrap any Create* result; never wrap GetCurrentProcess() pseudo handles.
struct HandleCloser {
    void operator()(HANDLE h) const noexcept {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) {
            ::CloseHandle(h);
        }
    }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

inline bool is_valid(const UniqueHandle& h) noexcept {
    return h && h.get() != INVALID_HANDLE_VALUE;
}

// PROC_THREAD_ATTRIBUTE_LIST owner. Values passed to update() must outlive this object.
class AttributeList {
public:
    explicit AttributeList(DWORD count);
    ~AttributeList();
    AttributeList(const AttributeList&) = delete;
    AttributeList& operator=(const AttributeList&) = delete;
    AttributeList(AttributeList&&) = delete;
    AttributeList& operator=(AttributeList&&) = delete;

    void update(DWORD_PTR attribute, void* value, SIZE_T size);
    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept { return list_; }

private:
    std::unique_ptr<std::byte[]> buffer_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_{};
};

}  // namespace agentdesk::platform::win
