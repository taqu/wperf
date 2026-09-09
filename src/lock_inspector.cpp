#include "lock_inspector_internal.h"
#include <algorithm>
#include <iterator>
#include <new>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace wperf
{
namespace
{
    LockInspectionResult Failure(DWORD error, LockInspectionStage stage, bool directory = false)
    {
        LockInspectionResult result;
        result.status = LockInspectionStatus::RestartManagerFailure;
        result.stage = stage;
        result.nativeError = error;
        if(error == ERROR_ACCESS_DENIED && directory && stage == LockInspectionStage::GetList)
            result.status = LockInspectionStatus::DirectoryUnsupported;
        else if(error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD)
            result.status = LockInspectionStatus::AccessDenied;
        else if(stage == LockInspectionStage::ValidatePath || stage == LockInspectionStage::RegisterResource) {
            switch(error) {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
            case ERROR_INVALID_NAME:
            case ERROR_BAD_PATHNAME:
            case ERROR_DIRECTORY:
            case ERROR_FILENAME_EXCED_RANGE:
                result.status = LockInspectionStatus::InvalidPath;
                break;
            }
        }
        return result;
    }

    class RestartManagerSession
    {
    public:
        RestartManagerSession(const detail::LockInspectorApi& api, DWORD& cleanupError)
            : api_(api), cleanupError_(cleanupError)
        {
        }
        ~RestartManagerSession()
        {
            if(active_)
                cleanupError_ = api_.endSession(handle_);
        }
        RestartManagerSession(const RestartManagerSession&) = delete;
        RestartManagerSession& operator=(const RestartManagerSession&) = delete;

        DWORD Start()
        {
            WCHAR key[CCH_RM_SESSION_KEY + 1]{};
            DWORD error = api_.startSession(&handle_, 0, key);
            active_ = error == ERROR_SUCCESS; // Zero is not an invalid session sentinel.
            return error;
        }
        DWORD Handle() const { return handle_; }

    private:
        const detail::LockInspectorApi& api_;
        DWORD& cleanupError_;
        DWORD handle_ = 0;
        bool active_ = false;
    };

    LockInspectionResult ReadProcesses(DWORD session, bool directory, const detail::LockInspectorApi& api)
    {
        std::vector<RM_PROCESS_INFO> entries;
        // Initial size query plus at most three buffer-fill attempts. No sleeps/polling.
        for(unsigned attempt = 0; attempt < 4; ++attempt) {
            UINT needed = 0;
            UINT count = static_cast<UINT>(entries.size());
            DWORD rebootReasons = 0;
            DWORD error = api.getList(session, &needed, &count,
                                      entries.empty() ? nullptr : entries.data(), &rebootReasons);
            if(error == ERROR_SUCCESS) {
                if(count > entries.size())
                    return Failure(ERROR_INVALID_DATA, LockInspectionStage::GetList);
                LockInspectionResult result;
                result.processes.reserve(count);
                for(UINT i = 0; i < count; ++i) {
                    const RM_PROCESS_INFO& entry = entries[i];
                    const FILETIME& time = entry.Process.ProcessStartTime;
                    const WCHAR* nameEnd = std::find(std::begin(entry.strAppName), std::end(entry.strAppName), L'\0');
                    result.processes.push_back({entry.Process.dwProcessId,
                                                std::wstring(std::begin(entry.strAppName), nameEnd),
                                                (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime});
                }
                // Prefer a populated display name for duplicate identities; lexical tie-break
                // makes the selected metadata independent of Restart Manager's ordering.
                std::sort(result.processes.begin(), result.processes.end(), [](const auto& a, const auto& b) {
                    return std::tuple(a.pid, a.startTime, a.name.empty(), std::wstring_view(a.name))
                           < std::tuple(b.pid, b.startTime, b.name.empty(), std::wstring_view(b.name));
                });
                const auto end = std::unique(result.processes.begin(), result.processes.end(), [](const LockingProcess& a, const LockingProcess& b) {
                    return a.pid == b.pid && a.startTime == b.startTime;
                });
                result.processes.erase(end, result.processes.end());
                return result;
            }
            if(error != ERROR_MORE_DATA || attempt == 3)
                return Failure(error, LockInspectionStage::GetList, directory);
            // Ignore partial records on ERROR_MORE_DATA, even if processes vanished.
            entries.resize(needed);
        }
        return Failure(ERROR_MORE_DATA, LockInspectionStage::GetList);
    }

    DWORD QueryAttributes(LPCWSTR path, DWORD* attributes)
    {
        *attributes = GetFileAttributesW(path);
        return *attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
    }
} // namespace

LockInspectionResult detail::InspectLocksWithApi(const std::filesystem::path& path, const LockInspectorApi& api)
{
    const std::wstring& native = path.native();
    if(native.empty() || native.find(L'\0') != std::wstring::npos || !path.is_absolute())
        return Failure(ERROR_INVALID_NAME, LockInspectionStage::ValidatePath);

    DWORD attributes = 0;
    DWORD error = api.queryAttributes(native.c_str(), &attributes);
    if(error != ERROR_SUCCESS)
        return Failure(error, LockInspectionStage::ValidatePath);

    LockInspectionResult result;
    DWORD cleanupError = 0;
    {
        RestartManagerSession session(api, cleanupError);
        error = session.Start();
        if(error != ERROR_SUCCESS)
            return Failure(error, LockInspectionStage::StartSession);
        LPCWSTR resource = native.c_str();
        error = api.registerResources(session.Handle(), 1, &resource, 0, nullptr, 0, nullptr);
        if(error != ERROR_SUCCESS)
            result = Failure(error, LockInspectionStage::RegisterResource);
        else {
            try {
                result = ReadProcesses(session.Handle(), (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0, api);
            } catch(const std::bad_alloc&) {
                result = Failure(ERROR_OUTOFMEMORY, LockInspectionStage::GetList);
            } catch(const std::length_error&) {
                result = Failure(ERROR_OUTOFMEMORY, LockInspectionStage::GetList);
            }
        }
    } // End the session before returning, also on exceptions.
    if(cleanupError != ERROR_SUCCESS && result.status == LockInspectionStatus::Success)
        result = Failure(cleanupError, LockInspectionStage::EndSession);
    result.cleanupError = cleanupError;
    return result;
}

LockInspectionResult InspectLocks(const std::filesystem::path& path)
{
    const detail::LockInspectorApi api{QueryAttributes, RmStartSession, RmRegisterResources, RmGetList, RmEndSession};
    return detail::InspectLocksWithApi(path, api);
}
} // namespace wperf
