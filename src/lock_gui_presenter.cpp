#include "lock_gui_presenter.h"
#include <algorithm>

namespace wperf
{
namespace
{
    std::wstring ErrorStatus(const LockInspectionResult& result)
    {
        const wchar_t* message = L"Lock inspection failed.";
        switch(result.status) {
        case LockInspectionStatus::InvalidPath:
            message = L"The selected path is invalid.";
            break;
        case LockInspectionStatus::AccessDenied:
            message = L"Lock inspection failed: access denied.";
            break;
        case LockInspectionStatus::DirectoryUnsupported:
            message = L"The selected directory is not supported by the normal scan. Try Deep Scan.";
            break;
        case LockInspectionStatus::RestartManagerFailure:
            message = L"Lock inspection failed in Restart Manager.";
            break;
        case LockInspectionStatus::NativeScanFailure:
            message = L"The native handle scan failed.";
            break;
        case LockInspectionStatus::Success:
        case LockInspectionStatus::PartialSuccess:
            break;
        }
        std::wstring text(message);
        if(result.nativeError != 0)
            text += L" Windows error: " + std::to_wstring(result.nativeError) + L'.';
        return text;
    }
} // namespace

LockGuiPresentation PresentLockInspection(const LockInspectionResult& result)
{
    LockGuiPresentation view;
    view.partial = result.status == LockInspectionStatus::PartialSuccess;
    view.successful = result.status == LockInspectionStatus::Success || view.partial;
    if(!view.successful) {
        view.status = ErrorStatus(result);
        return view;
    }

    for(const auto& process: result.processes) {
        const std::wstring name = process.name.empty() ? L"(name unavailable)" : process.name;
        if(process.resources.empty()) {
            view.rows.push_back({name, process.pid, process.startTime, {}});
        } else {
            for(const auto& resource: process.resources)
                view.rows.push_back({name, process.pid, process.startTime, resource});
        }
    }
    std::sort(view.rows.begin(), view.rows.end(), [](const LockGuiRow& left, const LockGuiRow& right) {
        if(left.process != right.process)
            return left.process < right.process;
        if(left.pid != right.pid)
            return left.pid < right.pid;
        return left.resource < right.resource;
    });

    if(view.partial) {
        view.status = result.processes.empty()
                          ? L"No matching processes found in the inspected portion. Some processes could not be inspected."
                          : L"Partial result: some processes or handles could not be inspected due to permissions or scan limits.";
    } else if(result.processes.empty()) {
        view.status = L"No locking processes found.";
    } else {
        view.status = std::to_wstring(result.processes.size()) + (result.processes.size() == 1 ? L" locking process found." : L" locking processes found.");
    }
    return view;
}
} // namespace wperf
