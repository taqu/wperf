#ifndef INC_LOCK_GUI_PRESENTER_H
#define INC_LOCK_GUI_PRESENTER_H

#include "lock_inspector.h"
#include <string>
#include <vector>

namespace wperf
{
struct LockGuiRow
{
    std::wstring process;
    uint32_t pid = 0;
    uint64_t startTime = 0;
    std::wstring resource;
};

struct LockGuiPresentation
{
    std::vector<LockGuiRow> rows;
    std::wstring status;
    bool successful = false;
    bool partial = false;
};

[[nodiscard]] LockGuiPresentation PresentLockInspection(const LockInspectionResult& result);
} // namespace wperf

#endif // INC_LOCK_GUI_PRESENTER_H
