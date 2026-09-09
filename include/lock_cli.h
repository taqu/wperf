#ifndef INC_LOCK_CLI_H
#define INC_LOCK_CLI_H
#include "lock_inspector.h"
#include <span>
#include <string_view>

namespace wperf::cli
{
enum class Mode { Desktop, Help, Inspect, Invalid };
struct Options
{
    Mode mode = Mode::Desktop;
    std::wstring path;
    bool json = false;
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
Output Run(const Options& options, Inspector inspect = InspectLocks);
// Returns -1 for ordinary desktop startup; otherwise the command's exit code.
int DispatchCommandLine();
}
#endif
