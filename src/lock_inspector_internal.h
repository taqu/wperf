#ifndef INC_LOCK_INSPECTOR_INTERNAL_H
#define INC_LOCK_INSPECTOR_INTERNAL_H
#include "lock_inspector.h"
#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>
#include <restartmanager.h>

namespace wperf::detail
{
/**
 * Narrow per-call seam for deterministic backend tests. Never globally replaced.
 * Attribute query returns a Win32 error code and writes attributes on success.
 */
struct LockInspectorApi
{
    DWORD (*queryAttributes)(LPCWSTR, DWORD*);
    decltype(&RmStartSession) startSession;
    decltype(&RmRegisterResources) registerResources;
    decltype(&RmGetList) getList;
    decltype(&RmEndSession) endSession;
};

LockInspectionResult InspectLocksWithApi(const std::filesystem::path& path,
                                         const LockInspectorApi& api);
} // namespace wperf::detail
#endif // INC_LOCK_INSPECTOR_INTERNAL_H
