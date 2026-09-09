#ifndef INC_LOCK_INSPECTOR_H
#define INC_LOCK_INSPECTOR_H
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace wperf
{
enum class DiscoverySource { RestartManager, NativeHandleScan, Both };
struct LockingProcess
{
    uint32_t pid = 0;
    std::wstring name; //!< Restart Manager display name or native executable name; may be empty.
    uint64_t startTime = 0; //!< Windows FILETIME ticks; distinguishes reused PIDs.
    std::vector<std::wstring> resources; //!< Native matching paths; RM does not invent resource detail.
    DiscoverySource source = DiscoverySource::RestartManager;
};

enum class LockInspectionStatus
{
    Success,
    InvalidPath,
    AccessDenied,
    DirectoryUnsupported,
    RestartManagerFailure,
    PartialSuccess,
    NativeScanFailure,
};

enum class LockInspectionStage
{
    None,
    ValidatePath,
    StartSession,
    RegisterResource,
    GetList,
    EndSession,
    NativeSnapshot,
    NativeResolveTarget,
    NativeScan,
};

struct NativeScanDiagnostics
{
    uint64_t systemHandles = 0;
    uint64_t candidateHandles = 0;
    uint64_t resolvedHandles = 0;
    uint32_t skippedProcesses = 0;
    uint32_t skippedHandles = 0;
    uint64_t elapsedMilliseconds = 0;
    bool limitReached = false;
    uint32_t ntStatus = 0;
};

struct LockInspectionResult
{
    LockInspectionStatus status = LockInspectionStatus::Success;
    LockInspectionStage stage = LockInspectionStage::None;
    uint32_t nativeError = 0;
    uint32_t cleanupError = 0; //!< RmEndSession error, also retained after earlier failures.
    std::vector<LockingProcess> processes;
    bool deepScan = false;
    NativeScanDiagnostics nativeScan;
    LockInspectionStatus restartManagerStatus = LockInspectionStatus::Success;
    uint32_t restartManagerError = 0;
    LockInspectionStage restartManagerStage = LockInspectionStage::None;
};

/**
 * Read-only, synchronous, on demand. Requires an absolute, existing Windows path.
 * No recursion or symlink/junction canonicalization. Directory discovery is limited
 * by Restart Manager. Success with an empty list is not proof a resource is unlocked.
 * Results are snapshots sorted/deduplicated by (PID, startTime); no processes are opened.
 */
[[nodiscard]] LockInspectionResult InspectLocks(const std::filesystem::path& path);
struct LockInspectionOptions { bool deep = false; };
// Explicit deep mode runs Restart Manager first, then one native snapshot/merge.
// PartialSuccess retains usable results and diagnostics; it is not complete coverage.
[[nodiscard]] LockInspectionResult InspectLocks(const std::filesystem::path& path,
                                               const LockInspectionOptions& options);
} // namespace wperf
#endif // INC_LOCK_INSPECTOR_H
