#include "lock_cli.h"
#include "version_config.h"

#include <windows.h>

#include <shellapi.h>

namespace wperf::cli
{
namespace
{
    constexpr std::wstring_view Help =
        L"Usage:\n  wperf.exe\n  wperf.exe --lock <absolute-path> [--deep] [--json]\n\n"
        L"Options:\n  --lock <path>  Inspect a file or directory using Restart Manager\n"
        L"  --json         Output the inspection result as JSON\n"
        L"  --deep         Add an on-demand native handle scan (may be partial)\n"
        L"  --help         Show this help\n"
        L"  --version      Show the wperf version and exit.\n";

    std::wstring Quote(std::wstring_view value)
    {
        constexpr wchar_t hex[] = L"0123456789abcdef";
        std::wstring result = L"\"";
        for(wchar_t c: value) {
            if(c == L'"' || c == L'\\') {
                result += L'\\';
                result += c;
            } else if(c < 0x20 || (c >= 0xd800 && c <= 0xdfff)) {
                // Escape UTF-16 code units, including surrogate pairs, without lossy conversion.
                result += L"\\u";
                for(int shift = 12; shift >= 0; shift -= 4)
                    result += hex[(static_cast<unsigned int>(c) >> shift) & 15];
            } else {
                result += c;
            }
        }
        return result + L'"';
    }

    const wchar_t* Category(LockInspectionStatus status)
    {
        switch(status) {
        case LockInspectionStatus::Success:
            return L"success";
        case LockInspectionStatus::InvalidPath:
            return L"invalid_path";
        case LockInspectionStatus::AccessDenied:
            return L"access_denied";
        case LockInspectionStatus::DirectoryUnsupported:
            return L"directory_unsupported";
        case LockInspectionStatus::RestartManagerFailure:
            return L"restart_manager_failure";
        case LockInspectionStatus::PartialSuccess:
            return L"partial_success";
        case LockInspectionStatus::NativeScanFailure:
            return L"native_scan_failure";
        }
        return L"restart_manager_failure";
    }

    const wchar_t* Stage(LockInspectionStage stage)
    {
        switch(stage) {
        case LockInspectionStage::None:
            return L"none";
        case LockInspectionStage::ValidatePath:
            return L"validate_path";
        case LockInspectionStage::StartSession:
            return L"start_session";
        case LockInspectionStage::RegisterResource:
            return L"register_resource";
        case LockInspectionStage::GetList:
            return L"get_list";
        case LockInspectionStage::EndSession:
            return L"end_session";
        case LockInspectionStage::NativeSnapshot:
            return L"native_snapshot";
        case LockInspectionStage::NativeResolveTarget:
            return L"native_resolve_target";
        case LockInspectionStage::NativeScan:
            return L"native_scan";
        }
        return L"none";
    }
} // namespace

Options Parse(std::span<const std::wstring_view> arguments)
{
    Options options;
    if(arguments.empty())
        return options;
    options.mode = Mode::Invalid;
    options.error = L"Invalid arguments. Use --lock <absolute-path> [--deep] [--json] or --help.";
    if(arguments.size() == 1 && arguments[0] == L"--help") {
        options.mode = Mode::Help;
        return options;
    }
    if(arguments.size() == 1 && arguments[0] == L"--version") {
        options.mode = Mode::Version;
        return options;
    }
    bool lock = false;
    for(size_t i = 0; i < arguments.size(); ++i) {
        if(arguments[i] == L"--lock" && !lock) {
            if(++i == arguments.size() || arguments[i].empty() || arguments[i].starts_with(L"--")) {
                options.error = L"--lock requires a nonempty absolute path.";
                return options;
            }
            lock = true;
            options.path = arguments[i];
        } else if(arguments[i] == L"--json" && !options.json) {
            options.json = true;
        } else if(arguments[i] == L"--deep" && !options.deep) {
            options.deep = true;
        } else {
            return options;
        }
    }
    if(lock)
        options.mode = Mode::Inspect;
    return options;
}

LockInspectionResult InspectDeep(const std::filesystem::path& path)
{
    return InspectLocks(path, LockInspectionOptions{true});
}

Output Run(const Options& options, Inspector inspect, Inspector deepInspect)
{
    if(options.mode == Mode::Desktop)
        return {};
    if(options.mode == Mode::Help)
        return {0, std::wstring(Help), {}};
    if(options.mode == Mode::Version)
        return {0, std::wstring(L"wperf ") + WPERF_VERSION_STRING + L"\n", {}};
    if(options.mode == Mode::Invalid)
        return {2, {}, options.error + L"\n"};
    const auto result = (options.deep ? deepInspect : inspect)(std::filesystem::path(options.path));
    const bool partial = result.status == LockInspectionStatus::PartialSuccess;
    const bool success = result.status == LockInspectionStatus::Success || partial;
    Output output;
    output.exitCode = partial ? 3 : (success ? 0 : 1);
    if(options.json) {
        output.out = L"{\"path\":" + Quote(options.path) + L",\"status\":" + Quote(success ? L"success" : L"error");
        if(success) {
            output.out += L",\"processes\":[";
            bool first = true;
            for(const auto& process: result.processes) {
                if(!first)
                    output.out += L',';
                first = false;
                output.out += L"{\"pid\":" + std::to_wstring(process.pid) + L",\"name\":" + Quote(process.name);
                if(options.deep) {
                    const auto source = process.source == DiscoverySource::Both ? L"both"
                                                                                : (process.source == DiscoverySource::NativeHandleScan ? L"native_handle_scan" : L"restart_manager");
                    output.out += L",\"source\":" + Quote(source) + L",\"resources\":[";
                    bool firstResource = true;
                    for(const auto& resource: process.resources) {
                        if(!firstResource)
                            output.out += L',';
                        firstResource = false;
                        output.out += Quote(resource);
                    }
                    output.out += L"]";
                }
                output.out += L"}";
            }
            output.out += L"]";
        } else {
            output.out += L",\"error\":{\"category\":" + Quote(Category(result.status))
                          + L",\"native_code\":" + std::to_wstring(result.nativeError)
                          + L",\"stage\":" + Quote(Stage(result.stage))
                          + L",\"cleanup_code\":" + std::to_wstring(result.cleanupError) + L"}";
        }
        if(options.deep) {
            const auto& scan = result.nativeScan;
            output.out += L",\"complete\":" + std::wstring(result.status == LockInspectionStatus::Success ? L"true" : L"false")
                          + L",\"native_scan\":{\"skipped_process_count\":" + std::to_wstring(scan.skippedProcesses)
                          + L",\"skipped_handle_count\":" + std::to_wstring(scan.skippedHandles)
                          + L",\"limit_reached\":" + (scan.limitReached ? L"true" : L"false")
                          + L",\"native_code\":" + std::to_wstring(result.nativeError)
                          + L",\"nt_status\":" + std::to_wstring(scan.ntStatus)
                          + L",\"stage\":" + Quote(Stage(result.stage)) + L"}"
                          + L",\"restart_manager\":{\"status\":" + Quote(Category(result.restartManagerStatus))
                          + L",\"native_code\":" + std::to_wstring(result.restartManagerError)
                          + L",\"stage\":" + Quote(Stage(result.restartManagerStage))
                          + L",\"cleanup_code\":" + std::to_wstring(result.cleanupError) + L"}";
        }
        output.out += L"}\n";
    } else if(success) {
        output.out = L"Target:\n  " + options.path + L"\n\n";
        if(result.processes.empty())
            output.out += partial
                              ? L"No matching processes found in the inspected portion.\n"
                              : L"No locking processes found.\n";
        else {
            output.out += L"Locking processes:\n  PID\tProcess\n";
            for(const auto& process: result.processes) {
                output.out += L"  " + std::to_wstring(process.pid) + L"\t"
                              + (process.name.empty() ? L"(name unavailable)" : process.name) + L"\n";
                if(options.deep)
                    for(const auto& resource: process.resources) output.out += L"    " + resource + L"\n";
            }
        }
        if(partial)
            output.err = L"Partial inspection: some processes/handles could not be inspected or a backend failed. "
                         L"Skipped processes: "
                         + std::to_wstring(result.nativeScan.skippedProcesses)
                         + L", skipped handles: " + std::to_wstring(result.nativeScan.skippedHandles)
                         + L", limit reached: " + (result.nativeScan.limitReached ? L"yes" : L"no")
                         + L", native Windows error: " + std::to_wstring(result.nativeError)
                         + L", Restart Manager Windows error: " + std::to_wstring(result.restartManagerError) + L".\n";
    } else {
        std::wstring category = Category(result.status);
        for(auto& c: category)
            if(c == L'_')
                c = L' ';
        output.err = L"Lock inspection failed: " + category + L" (Windows error "
                     + std::to_wstring(result.nativeError) + L", stage " + Stage(result.stage)
                     + L", cleanup error " + std::to_wstring(result.cleanupError) + L").\n";
    }
    return output;
}

namespace
{
    bool Valid(HANDLE handle)
    {
        return handle != nullptr && handle != INVALID_HANDLE_VALUE;
    }

    bool Write(HANDLE handle, std::wstring_view value)
    {
        if(value.empty())
            return true;
        if(!Valid(handle))
            return false;
        DWORD mode = 0;
        if(GetConsoleMode(handle, &mode)) {
            while(!value.empty()) {
                DWORD written = 0;
                const DWORD count = static_cast<DWORD>((std::min)(value.size(), size_t{16384}));
                if(!WriteConsoleW(handle, value.data(), count, &written, nullptr) || written == 0)
                    return false;
                value.remove_prefix(written);
            }
            return true;
        }
        const int32_t count = static_cast<int32_t>(value.size());
        const int32_t size = WideCharToMultiByte(CP_UTF8, 0, value.data(), count, nullptr, 0, nullptr, nullptr);
        if(size == 0)
            return false;
        std::string bytes(static_cast<size_t>(size), '\0');
        if(!WideCharToMultiByte(CP_UTF8, 0, value.data(), count, bytes.data(), size, nullptr, nullptr))
            return false;
        size_t offset = 0;
        while(offset < bytes.size()) {
            DWORD written = 0;
            if(!WriteFile(handle, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &written, nullptr)
               || written == 0)
                return false;
            offset += written;
        }
        return true;
    }
} // namespace

int32_t DispatchCommandLine(Options* guiOptions)
{
    int32_t argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if(!argv){
        return 2;
    }
    struct FreeArguments
    {
        void operator()(wchar_t** p) const
        {
            LocalFree(p);
        }
    };
    std::unique_ptr<wchar_t*, FreeArguments> owner(argv);
    std::vector<std::wstring_view> arguments;
    for(int32_t i = 1; i < argc; ++i) arguments.emplace_back(argv[i]);
    const auto options = Parse(arguments);
    if(options.mode == Mode::Desktop) {
        if(guiOptions)
            *guiOptions = options;
        return -1;
    }

    // Capture inherited redirects before AttachConsole replaces standard handles.
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    const bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
    if(!Valid(out))
        out = GetStdHandle(STD_OUTPUT_HANDLE);
    if(!Valid(err))
        err = GetStdHandle(STD_ERROR_HANDLE);
    const auto output = Run(options);
    const bool outWritten = Write(out, output.out);
    const bool errWritten = Write(err, output.err);
    if(attached)
        FreeConsole();
    return outWritten && errWritten ? output.exitCode : (output.exitCode == 0 ? 1 : output.exitCode);
}
} // namespace wperf::cli
