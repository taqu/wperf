#ifndef INC_PROCESS_CONTROLLER_H
#define INC_PROCESS_CONTROLLER_H

#include <cstdint>

namespace wperf
{
struct ProcessIdentity
{
    uint32_t pid = 0;
    uint64_t creationTime = 0;
    bool valid = false;
};

enum class ProcessControlStatus
{
    ProcessExited,
    CloseRequestSent,
    NoClosableWindow,
    StillRunning,
    AlreadyExited,
    IdentityMismatch,
    AccessDenied,
    SelfTarget,
    InvalidIdentity,
    Failed,
};

struct ProcessControlResult
{
    ProcessControlStatus status = ProcessControlStatus::Failed;
    uint32_t nativeError = 0;
    uint32_t windowsSignaled = 0;
};

[[nodiscard]] ProcessIdentity CaptureProcessIdentity(uint32_t pid);
[[nodiscard]] bool SameProcessIdentity(const ProcessIdentity& expected, const ProcessIdentity& actual);
[[nodiscard]] ProcessControlResult RequestGracefulClose(const ProcessIdentity& identity,
                                                        uint32_t waitMilliseconds = 1500);
[[nodiscard]] ProcessControlResult ForceTerminate(const ProcessIdentity& identity,
                                                  uint32_t waitMilliseconds = 2000);
} // namespace wperf

#endif // INC_PROCESS_CONTROLLER_H
