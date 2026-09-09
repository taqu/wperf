#include "lock_cli.h"
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <memory>

namespace wperf::cli
{
namespace
{
bool Valid(HANDLE handle) { return handle != nullptr && handle != INVALID_HANDLE_VALUE; }

bool Write(HANDLE handle, std::wstring_view value)
{
    if(value.empty()) return true;
    if(!Valid(handle)) return false;
    DWORD mode = 0;
    if(GetConsoleMode(handle, &mode)) {
        while(!value.empty()) {
            DWORD written = 0;
            const DWORD count = static_cast<DWORD>((std::min)(value.size(), size_t{16384}));
            if(!WriteConsoleW(handle, value.data(), count, &written, nullptr) || written == 0) return false;
            value.remove_prefix(written);
        }
        return true;
    }
    const int count = static_cast<int>(value.size());
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), count, nullptr, 0, nullptr, nullptr);
    if(size == 0) return false;
    std::string bytes(static_cast<size_t>(size), '\0');
    if(!WideCharToMultiByte(CP_UTF8, 0, value.data(), count, bytes.data(), size, nullptr, nullptr)) return false;
    size_t offset = 0;
    while(offset < bytes.size()) {
        DWORD written = 0;
        if(!WriteFile(handle, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &written, nullptr)
            || written == 0) return false;
        offset += written;
    }
    return true;
}
}

int DispatchCommandLine(Options* guiOptions)
{
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if(!argv) return 2;
    struct FreeArguments { void operator()(wchar_t** p) const { LocalFree(p); } };
    std::unique_ptr<wchar_t*, FreeArguments> owner(argv);
    std::vector<std::wstring_view> arguments;
    for(int i = 1; i < argc; ++i) arguments.emplace_back(argv[i]);
    const auto options = Parse(arguments);
    if(options.mode == Mode::Desktop || options.mode == Mode::LockUi) {
        if(guiOptions) *guiOptions = options;
        return -1;
    }

    // Capture inherited redirects before AttachConsole replaces standard handles.
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    const bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
    if(!Valid(out)) out = GetStdHandle(STD_OUTPUT_HANDLE);
    if(!Valid(err)) err = GetStdHandle(STD_ERROR_HANDLE);
    const auto output = Run(options);
    const bool outWritten = Write(out, output.out);
    const bool errWritten = Write(err, output.err);
    if(attached) FreeConsole();
    return outWritten && errWritten ? output.exitCode : (output.exitCode == 0 ? 1 : output.exitCode);
}
}
