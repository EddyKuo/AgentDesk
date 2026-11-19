// AttributeList: the buffer behind STARTUPINFOEX attributes (pseudoconsole, handle list).
#include "agentdesk/platform/win/unique_handle.hpp"

#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::platform::win {

AttributeList::AttributeList(DWORD count) {
    SIZE_T size = 0;
    // First call only reports the required size and is expected to fail.
    ::InitializeProcThreadAttributeList(nullptr, count, 0, &size);
    buffer_ = std::make_unique<std::byte[]>(size);
    // Win32 boundary: the attribute list is an opaque buffer we allocate ourselves.
    list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer_.get());
    if (!::InitializeProcThreadAttributeList(list_, count, 0, &size)) {
        throw_last_error("InitializeProcThreadAttributeList");
    }
}

AttributeList::~AttributeList() {
    ::DeleteProcThreadAttributeList(list_);
}

void AttributeList::update(DWORD_PTR attribute, void* value, SIZE_T size) {
    if (!::UpdateProcThreadAttribute(list_, 0, attribute, value, size, nullptr, nullptr)) {
        throw_last_error("UpdateProcThreadAttribute");
    }
}

}  // namespace agentdesk::platform::win
