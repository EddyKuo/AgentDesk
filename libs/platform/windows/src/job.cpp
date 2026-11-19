// KillOnCloseJob: creating, checking (after a handover) and using the session's job.
#include "agentdesk/platform/win/job.hpp"

#include <stdexcept>

#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::platform::win {

KillOnCloseJob::KillOnCloseJob() : job_(::CreateJobObjectW(nullptr, nullptr)) {
    if (!job_) {
        throw_last_error("CreateJobObjectW");
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &info, sizeof(info))) {
        throw_last_error("SetInformationJobObject");
    }
}

KillOnCloseJob::KillOnCloseJob(UniqueHandle existing) : job_(std::move(existing)) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    if (!is_valid(job_) || !::QueryInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &info,
                                                        sizeof(info), nullptr)) {
        throw_last_error("QueryInformationJobObject(handed over job)");
    }
    if ((info.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) == 0 ||
        (info.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_BREAKAWAY_OK) != 0) {
        throw std::runtime_error("handed over job does not kill its processes on close");
    }
}

void KillOnCloseJob::assign(HANDLE process) const {
    if (!::AssignProcessToJobObject(job_.get(), process)) {
        throw_last_error("AssignProcessToJobObject");
    }
}

void KillOnCloseJob::terminate(UINT exit_code) const {
    if (!::TerminateJobObject(job_.get(), exit_code)) {
        throw_last_error("TerminateJobObject");
    }
}

DWORD KillOnCloseJob::active_processes() const {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
    if (!::QueryInformationJobObject(job_.get(), JobObjectBasicAccountingInformation, &info, sizeof(info),
                                     nullptr)) {
        throw_last_error("QueryInformationJobObject");
    }
    return info.ActiveProcesses;
}

}  // namespace agentdesk::platform::win
