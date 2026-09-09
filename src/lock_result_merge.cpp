#include "native_handle_backend.h"
#include <algorithm>
#include <tuple>

namespace wperf::detail
{
namespace
{
bool Usable(LockInspectionStatus status)
{
    return status == LockInspectionStatus::Success || status == LockInspectionStatus::PartialSuccess;
}
}

LockInspectionResult MergeLockResults(LockInspectionResult primary, LockInspectionResult native)
{
    const bool nativeUsable = Usable(native.status);
    const bool primaryUsable = Usable(primary.status);
    native.deepScan = true;
    native.restartManagerStatus = primary.status;
    native.restartManagerError = primary.nativeError;
    native.restartManagerStage = primary.stage;
    native.cleanupError = primary.cleanupError;
    if(nativeUsable) {
        // DirectoryUnsupported is the expected reason to use the native backend.
        if(primary.status != LockInspectionStatus::Success
            && primary.status != LockInspectionStatus::DirectoryUnsupported)
            native.status = LockInspectionStatus::PartialSuccess;
    } else if(primaryUsable) {
        native.status = LockInspectionStatus::PartialSuccess;
    }
    if(!nativeUsable) native.processes.clear();
    if(primaryUsable) {
        for(auto& process : primary.processes) {
            process.source = DiscoverySource::RestartManager;
            native.processes.push_back(std::move(process));
        }
    }
    auto& processes = native.processes;
    std::sort(processes.begin(), processes.end(), [](const auto& a, const auto& b) {
        return std::tuple(a.pid, a.startTime, a.source, a.name) < std::tuple(b.pid, b.startTime, b.source, b.name);
    });
    std::vector<LockingProcess> merged;
    for(auto& process : processes) {
        if(!merged.empty() && merged.back().pid == process.pid
            && (merged.back().startTime == process.startTime || merged.back().startTime == 0 || process.startTime == 0)) {
            auto& previous = merged.back();
            previous.startTime = (std::max)(previous.startTime, process.startTime);
            if(previous.name.empty()) previous.name = process.name;
            if(previous.source != process.source) previous.source = DiscoverySource::Both;
            previous.resources.insert(previous.resources.end(), process.resources.begin(), process.resources.end());
        } else {
            // Do not conflate known different process lifetimes after PID reuse.
            if(!merged.empty() && merged.back().pid == process.pid) native.status = LockInspectionStatus::PartialSuccess;
            merged.push_back(std::move(process));
        }
    }
    for(auto& process : merged) {
        auto& paths = process.resources;
        for(auto& path : paths) path = NormalizeLockPath(path);
        std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
            const int comparison = CompareLockPaths(a, b);
            return comparison != 0 ? comparison < 0 : a < b;
        });
        paths.erase(std::unique(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
            return CompareLockPaths(a, b) == 0;
        }), paths.end());
    }
    native.processes = std::move(merged);
    return native;
}
}
