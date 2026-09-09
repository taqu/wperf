#ifndef INC_LOCK_INSPECTOR_H
#define INC_LOCK_INSPECTOR_H
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace wperf
{
struct LockingProcess
{
    uint32_t pid = 0;
    std::wstring name; //!< Restart Manager's display name; may be empty.
    uint64_t startTime = 0; //!< Windows FILETIME ticks; distinguishes reused PIDs.
};

enum class LockInspectionStatus
{
    Success,
    InvalidPath,
    AccessDenied,
    DirectoryUnsupported,
    RestartManagerFailure,
};

enum class LockInspectionStage
{
    None,
    ValidatePath,
    StartSession,
    RegisterResource,
    GetList,
    EndSession,
};

struct LockInspectionResult
{
    LockInspectionStatus status = LockInspectionStatus::Success;
    LockInspectionStage stage = LockInspectionStage::None;
    uint32_t nativeError = 0;
    uint32_t cleanupError = 0; //!< RmEndSession error, also retained after earlier failures.
    std::vector<LockingProcess> processes;
};

/**
 * Read-only, synchronous, on demand. Requires an absolute, existing Windows path.
 * No recursion or symlink/junction canonicalization. Directory discovery is limited
 * by Restart Manager. Success with an empty list is not proof a resource is unlocked.
 * Results are snapshots sorted/deduplicated by (PID, startTime); no processes are opened.
 */
[[nodiscard]] LockInspectionResult InspectLocks(const std::filesystem::path& path);
} // namespace wperf
#endif // INC_LOCK_INSPECTOR_H
