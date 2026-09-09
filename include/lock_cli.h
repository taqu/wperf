#ifndef INC_LOCK_CLI_H
#define INC_LOCK_CLI_H
#include "lock_inspector.h"
#include <span>
#include <string_view>

namespace wperf::cli
{
enum class Mode { Desktop, Help, Version, Inspect, LockUi, Invalid };
struct Options
{
    Mode mode = Mode::Desktop;
    std::wstring path;
    bool json = false;
    bool deep = false;
    std::wstring error;
};
struct Output
{
    int exitCode = 0;
    std::wstring out;
    std::wstring err;
};
// Arguments exclude the executable name. Discovery and path validation stay in the core.
Options Parse(std::span<const std::wstring_view> arguments);
using Inspector = LockInspectionResult (*)(const std::filesystem::path&);
LockInspectionResult InspectDeep(const std::filesystem::path& path);
Output Run(const Options& options, Inspector inspect = InspectLocks, Inspector deepInspect = InspectDeep);
// Returns -1 for a GUI startup mode; otherwise the command's exit code. When
// supplied, guiOptions receives Desktop or LockUi startup details.
int DispatchCommandLine(Options* guiOptions = nullptr);
}
#endif
