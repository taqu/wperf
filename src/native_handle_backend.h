#ifndef INC_NATIVE_HANDLE_BACKEND_H
#define INC_NATIVE_HANDLE_BACKEND_H
#include "lock_inspector.h"
#include <span>
#include <string_view>

namespace wperf::detail
{
struct DeviceMapping { std::wstring device; std::wstring drive; };
std::wstring NormalizeLockPath(std::wstring_view path, std::span<const DeviceMapping> mappings = {});
int CompareLockPaths(std::wstring_view left, std::wstring_view right);
bool MatchesLockPath(std::wstring_view target, std::wstring_view resource, bool directory);
LockInspectionResult MergeLockResults(LockInspectionResult primary, LockInspectionResult native);
LockInspectionResult InspectNativeHandles(const std::filesystem::path& path);

// Project-owned snapshot records and per-call test seam. NT layouts stay in the .cpp.
struct NativeHandleEntry { uint32_t pid; uintptr_t handle; uint16_t type; };
class NativeScanOperations
{
public:
    virtual ~NativeScanOperations() = default;
    virtual bool Open(uint32_t pid) = 0;
    virtual void CloseProcess() = 0;
    // Resolves one duplicate and closes it before returning. Empty means skipped.
    virtual std::wstring Resolve(uintptr_t handle) = 0;
    virtual void Metadata(LockingProcess& process) = 0;
    virtual uint64_t Milliseconds() = 0;
};
LockInspectionResult ScanNativeEntries(std::vector<NativeHandleEntry> entries, uint16_t fileType,
    uint32_t ownPid, uintptr_t anchor, std::wstring_view target, bool directory,
    NativeScanOperations& operations);
// Checked snapshot-growth helper shared by the native query loop and tests.
size_t NextSnapshotSize(size_t current, size_t requested);
}
#endif
