#include "process_controller.h"
#include <windows.h>

namespace wperf
{
namespace
{
    constexpr DWORD GracefulRights = PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE;
    constexpr DWORD TerminateRights = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE;
    constexpr DWORD ProjectTerminationCode = 0x57465010; // "WFP" control action marker.

    uint64_t FileTimeValue(const FILETIME& value)
    {
        return (static_cast<uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    }

    ProcessControlResult OpenFailure(DWORD error)
    {
        if(error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_FOUND)
            return {ProcessControlStatus::AlreadyExited, error, 0};
        return {error == ERROR_ACCESS_DENIED ? ProcessControlStatus::AccessDenied : ProcessControlStatus::Failed,
                error, 0};
    }

    struct WindowCloseContext
    {
        DWORD pid = 0;
        uint32_t signaled = 0;
        ULONGLONG deadline = 0;
    };

    BOOL CALLBACK CloseWindow(HWND window, LPARAM parameter)
    {
        auto& context = *reinterpret_cast<WindowCloseContext*>(parameter);
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if(pid != context.pid || !IsWindow(window))
            return TRUE;
        const ULONGLONG now = GetTickCount64();
        if(now >= context.deadline)
            return FALSE;
        if(PostMessageW(window, WM_CLOSE, 0, 0) != FALSE)
            ++context.signaled;
        return TRUE;
    }

    ProcessControlResult ValidateAndOpen(const ProcessIdentity& identity, DWORD rights, HANDLE& process)
    {
        process = nullptr;
        if(!identity.valid || identity.pid == 0)
            return {ProcessControlStatus::InvalidIdentity, ERROR_INVALID_PARAMETER, 0};
        if(identity.pid == GetCurrentProcessId())
            return {ProcessControlStatus::SelfTarget, ERROR_ACCESS_DENIED, 0};
        process = OpenProcess(rights, FALSE, identity.pid);
        if(!process)
            return OpenFailure(GetLastError());
        const ProcessIdentity actual = CaptureProcessIdentity(identity.pid);
        if(!actual.valid) {
            const DWORD error = GetLastError();
            CloseHandle(process);
            return OpenFailure(error == ERROR_SUCCESS ? ERROR_ACCESS_DENIED : error);
        }
        if(!SameProcessIdentity(identity, actual)) {
            CloseHandle(process);
            return {ProcessControlStatus::IdentityMismatch, ERROR_INVALID_DATA, 0};
        }
        if(WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
            CloseHandle(process);
            return {ProcessControlStatus::AlreadyExited, ERROR_SUCCESS, 0};
        }
        return {ProcessControlStatus::CloseRequestSent, ERROR_SUCCESS, 0};
    }
} // namespace

ProcessIdentity CaptureProcessIdentity(uint32_t pid)
{
    ProcessIdentity identity{pid, 0, false};
    if(pid == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return identity;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if(!process)
        return identity;
    FILETIME created{}, exited{}, kernel{}, user{};
    if(GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        identity.creationTime = FileTimeValue(created);
        identity.valid = identity.creationTime != 0;
    } else {
        SetLastError(GetLastError());
    }
    CloseHandle(process);
    return identity;
}

bool SameProcessIdentity(const ProcessIdentity& expected, const ProcessIdentity& actual)
{
    return expected.valid && actual.valid && expected.pid == actual.pid
           && expected.creationTime == actual.creationTime;
}

ProcessControlResult RequestGracefulClose(const ProcessIdentity& identity, uint32_t waitMilliseconds)
{
    HANDLE process = nullptr;
    auto result = ValidateAndOpen(identity, GracefulRights, process);
    if(result.status != ProcessControlStatus::CloseRequestSent)
        return result;
    if(!process)
        return result;

    WindowCloseContext context{identity.pid, 0, GetTickCount64() + waitMilliseconds};
    EnumWindows(CloseWindow, reinterpret_cast<LPARAM>(&context));
    if(context.signaled == 0) {
        CloseHandle(process);
        return {ProcessControlStatus::NoClosableWindow, ERROR_SUCCESS, 0};
    }
    const DWORD wait = WaitForSingleObject(process, waitMilliseconds);
    CloseHandle(process);
    if(wait == WAIT_OBJECT_0)
        return {ProcessControlStatus::ProcessExited, ERROR_SUCCESS, context.signaled};
    if(wait == WAIT_TIMEOUT)
        return {ProcessControlStatus::StillRunning, ERROR_SUCCESS, context.signaled};
    return {ProcessControlStatus::Failed, GetLastError(), context.signaled};
}

ProcessControlResult ForceTerminate(const ProcessIdentity& identity, uint32_t waitMilliseconds)
{
    HANDLE process = nullptr;
    auto result = ValidateAndOpen(identity, TerminateRights, process);
    if(result.status != ProcessControlStatus::CloseRequestSent)
        return result;
    if(!process)
        return result;
    if(!TerminateProcess(process, ProjectTerminationCode)) {
        const DWORD error = GetLastError();
        CloseHandle(process);
        return {error == ERROR_ACCESS_DENIED ? ProcessControlStatus::AccessDenied : ProcessControlStatus::Failed,
                error, 0};
    }
    const DWORD wait = WaitForSingleObject(process, waitMilliseconds);
    CloseHandle(process);
    if(wait == WAIT_OBJECT_0)
        return {ProcessControlStatus::ProcessExited, ERROR_SUCCESS, 0};
    if(wait == WAIT_TIMEOUT)
        return {ProcessControlStatus::StillRunning, ERROR_SUCCESS, 0};
    return {ProcessControlStatus::Failed, GetLastError(), 0};
}
} // namespace wperf
