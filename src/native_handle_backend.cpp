#include "native_handle_backend.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winternl.h>
#include <algorithm>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>

namespace wperf::detail
{
namespace
{
struct CloseHandleDeleter
{
    void operator()(void* handle) const { if(handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
using OwnedHandle = std::unique_ptr<void, CloseHandleDeleter>;

// One temporary watchdog per deep scan, never one worker per handle. It only
// cancels this scanner thread's synchronous I/O; it never touches remote threads.
// Drivers must cooperate with cancellation, so this is not a hard time guarantee.
class IoWatchdog
{
public:
    IoWatchdog() : thread_(OpenThread(THREAD_TERMINATE, FALSE, GetCurrentThreadId()))
    {
        if(!thread_) throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
        worker_ = std::thread([this] {
            std::unique_lock lock(mutex_);
            while(!stop_) {
                changed_.wait(lock, [this] { return stop_ || armed_; });
                if(stop_) break;
                const auto generation = generation_;
                if(!changed_.wait_for(lock, std::chrono::milliseconds(100), [this, generation] {
                    return stop_ || !armed_ || generation_ != generation;
                })) {
                    CancelSynchronousIo(thread_.get());
                    cancelled_ = true;
                }
            }
        });
    }
    ~IoWatchdog()
    {
        { std::lock_guard lock(mutex_); stop_ = true; }
        changed_.notify_one();
        if(worker_.joinable()) worker_.join();
    }
    void Begin()
    {
        std::lock_guard lock(mutex_);
        ++generation_;
        armed_ = true;
        cancelled_ = false;
        changed_.notify_one();
    }
    bool End()
    {
        std::lock_guard lock(mutex_);
        armed_ = false;
        changed_.notify_one();
        return cancelled_;
    }
private:
    OwnedHandle thread_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool stop_ = false;
    bool armed_ = false;
    bool cancelled_ = false;
    uint64_t generation_ = 0;
    std::thread worker_;
};
struct IoGuard
{
    IoWatchdog& watchdog;
    explicit IoGuard(IoWatchdog& value) : watchdog(value) { watchdog.Begin(); }
    ~IoGuard() { watchdog.End(); }
};

// SystemExtendedHandleInformation (64) is not exposed by the public SDK.
// Pointer-sized layout follows phnt's SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX.
// https://github.com/winsiderss/systeminformer/blob/master/phnt/include/ntexapi.h
struct SystemHandleEntry
{
    void* object;
    ULONG_PTR pid;
    ULONG_PTR handle;
    ULONG access;
    USHORT backTrace;
    USHORT type;
    ULONG attributes;
    ULONG reserved;
};
struct SystemHandleHeader { ULONG_PTR count; ULONG_PTR reserved; };
static_assert(sizeof(void*) == 8, "Native scanning currently supports Windows x64 only");
static_assert(sizeof(SystemHandleEntry) == 40 && offsetof(SystemHandleEntry, type) == 30);
static_assert(sizeof(SystemHandleHeader) == 16);
using QuerySystemInformation = LONG (NTAPI*)(ULONG, PVOID, ULONG, PULONG);
using StatusToError = ULONG (WINAPI*)(LONG);

LockInspectionResult Failure(DWORD error, LockInspectionStage stage, uint32_t ntStatus = 0)
{
    LockInspectionResult result;
    result.status = LockInspectionStatus::NativeScanFailure;
    result.stage = stage;
    result.nativeError = error;
    result.deepScan = true;
    result.nativeScan.ntStatus = ntStatus;
    return result;
}

std::wstring FinalPath(HANDLE handle)
{
    // No NtQueryObject: callers have already filtered File objects and disk handles.
    // FILE_NAME_OPENED avoids SMB's normalized-name component access checks.
    // These synchronous driver calls have no hard timeout; see docs/lock-inspector.md.
    for(DWORD volume : {DWORD(VOLUME_NAME_NT), DWORD(VOLUME_NAME_DOS)}) {
        std::vector<wchar_t> buffer(512);
        for(unsigned attempt = 0; attempt < 3; ++attempt) {
            const DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(),
                static_cast<DWORD>(buffer.size()), FILE_NAME_OPENED | volume);
            if(length == 0) break;
            if(length < buffer.size()) return {buffer.data(), length};
            if(length >= 32768) break;
            buffer.resize(static_cast<size_t>(length) + 1);
        }
    }
    return {};
}

std::vector<DeviceMapping> DeviceMappings()
{
    std::vector<DeviceMapping> mappings;
    std::vector<wchar_t> buffer(32768);
    for(wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        const wchar_t drive[] = {letter, L':', L'\0'};
        // First string is the current mapping; later strings are old mappings.
        if(QueryDosDeviceW(drive, buffer.data(), static_cast<DWORD>(buffer.size())) != 0)
            mappings.push_back({buffer.data(), drive});
    }
    return mappings;
}

class WindowsScanOperations final : public NativeScanOperations
{
public:
    explicit WindowsScanOperations(const std::vector<DeviceMapping>& mappings, IoWatchdog& watchdog,
                                   decltype(&NtCreateFile) reopen)
        : mappings_(mappings), watchdog_(watchdog), reopen_(reopen) {}
    bool Open(uint32_t pid) override
    {
        process_.reset(OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid));
        return process_ != nullptr;
    }
    void CloseProcess() override { process_.reset(); }
    std::wstring Resolve(uintptr_t handle) override
    {
        IoGuard watch(watchdog_);
        HANDLE duplicate = nullptr;
        if(!DuplicateHandle(process_.get(), reinterpret_cast<HANDLE>(handle), GetCurrentProcess(),
                            &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS)) return {};
        OwnedHandle owner(duplicate);
        // A duplicate shares the source FILE_OBJECT's synchronous-I/O lock.
        // Reopen for metadata with an independent asynchronous FILE_OBJECT so
        // another process's pending synchronous read cannot serialize this query.
        // Empty name relative to the duplicated file object reopens the object
        // itself, including directories (ReOpenFile rejects these on tested Windows).
        UNICODE_STRING empty{};
        wchar_t terminator = L'\0';
        empty.Buffer = &terminator;
        empty.MaximumLength = sizeof(wchar_t);
        OBJECT_ATTRIBUTES attributes{};
        attributes.Length = sizeof(attributes);
        attributes.RootDirectory = duplicate;
        attributes.ObjectName = &empty;
        IO_STATUS_BLOCK io{};
        HANDLE reopened = nullptr;
        const LONG status = reopen_(&reopened, FILE_READ_ATTRIBUTES, &attributes, &io, nullptr, 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
            FILE_OPEN_FOR_BACKUP_INTENT, nullptr, 0);
        if(status < 0) return {};
        OwnedHandle query(reopened);
        if(GetFileType(query.get()) != FILE_TYPE_DISK) return {};
        auto path = NormalizeLockPath(FinalPath(query.get()), mappings_);
        query.reset();
        owner.reset();
        if(watchdog_.End()) return {};
        return path;
    }
    void Metadata(LockingProcess& process) override
    {
        // Optional metadata only for matching PIDs, independently of duplication rights.
        OwnedHandle query(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.pid));
        if(!query) return;
        FILETIME created{}, exited{}, kernel{}, user{};
        if(GetProcessTimes(query.get(), &created, &exited, &kernel, &user))
            process.startTime = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        std::vector<wchar_t> name(32768);
        DWORD size = static_cast<DWORD>(name.size());
        if(QueryFullProcessImageNameW(query.get(), 0, name.data(), &size))
            process.name = std::filesystem::path(std::wstring(name.data(), size)).filename().native();
    }
    uint64_t Milliseconds() override { return GetTickCount64(); }
private:
    OwnedHandle process_;
    const std::vector<DeviceMapping>& mappings_;
    IoWatchdog& watchdog_;
    decltype(&NtCreateFile) reopen_;
};

LockInspectionResult InspectNative(const std::filesystem::path& path)
{
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if(!ntdll) return Failure(ERROR_NOT_SUPPORTED, LockInspectionStage::NativeSnapshot);
    // memcpy avoids non-portable function-pointer conversion warnings (/W4).
    const auto queryAddress = GetProcAddress(ntdll, "NtQuerySystemInformation");
    const auto errorAddress = GetProcAddress(ntdll, "RtlNtStatusToDosError");
    const auto reopenAddress = GetProcAddress(ntdll, "NtCreateFile");
    QuerySystemInformation query = nullptr;
    StatusToError toError = nullptr;
    decltype(&NtCreateFile) reopen = nullptr;
    static_assert(sizeof(query) == sizeof(queryAddress));
    std::memcpy(&query, &queryAddress, sizeof(query));
    std::memcpy(&toError, &errorAddress, sizeof(toError));
    std::memcpy(&reopen, &reopenAddress, sizeof(reopen));
    if(!query || !toError || !reopen) return Failure(ERROR_NOT_SUPPORTED, LockInspectionStage::NativeSnapshot);

    OwnedHandle target(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if(target.get() == INVALID_HANDLE_VALUE)
        return Failure(GetLastError(), LockInspectionStage::NativeResolveTarget);
    FILE_BASIC_INFO basic{};
    if(!GetFileInformationByHandleEx(target.get(), FileBasicInfo, &basic, sizeof(basic)))
        return Failure(GetLastError(), LockInspectionStage::NativeResolveTarget);
    const auto mappings = DeviceMappings();
    const auto targetPath = NormalizeLockPath(FinalPath(target.get()), mappings);
    if(targetPath.empty()) return Failure(ERROR_PATH_NOT_FOUND, LockInspectionStage::NativeResolveTarget);

    std::vector<std::byte> buffer(1024 * 1024);
    const DWORD ownPid = GetCurrentProcessId();
    const auto anchor = reinterpret_cast<uintptr_t>(target.get());
    for(unsigned attempt = 0; attempt < 8; ++attempt) {
        ULONG needed = 0;
        const LONG status = query(64, buffer.data(), static_cast<ULONG>(buffer.size()), &needed);
        if(status < 0) {
            // STATUS_INFO_LENGTH_MISMATCH / STATUS_BUFFER_TOO_SMALL only.
            if(static_cast<uint32_t>(status) != 0xc0000004u && static_cast<uint32_t>(status) != 0xc0000023u)
                return Failure(toError(status), LockInspectionStage::NativeSnapshot, static_cast<uint32_t>(status));
            const size_t next = NextSnapshotSize(buffer.size(), needed);
            if(next == 0 || attempt == 7)
                return Failure(ERROR_INSUFFICIENT_BUFFER, LockInspectionStage::NativeSnapshot, static_cast<uint32_t>(status));
            buffer.resize(next);
            continue;
        }
        if(needed < sizeof(SystemHandleHeader) || needed > buffer.size())
            return Failure(ERROR_INVALID_DATA, LockInspectionStage::NativeSnapshot);
        SystemHandleHeader header{};
        std::memcpy(&header, buffer.data(), sizeof(header));
        if(header.count > (needed - sizeof(header)) / sizeof(SystemHandleEntry))
            return Failure(ERROR_INVALID_DATA, LockInspectionStage::NativeSnapshot);
        std::vector<NativeHandleEntry> entries;
        entries.reserve(header.count);
        uint16_t fileType = 0;
        for(size_t index = 0; index < header.count; ++index) {
            SystemHandleEntry entry{};
            std::memcpy(&entry, buffer.data() + sizeof(header) + index * sizeof(entry), sizeof(entry));
            if(entry.pid > MAXDWORD) continue;
            entries.push_back({static_cast<uint32_t>(entry.pid), entry.handle, entry.type});
            // Derive File's type index from our own real file/directory handle,
            // never hard-code an OS-dependent object type number.
            if(entry.pid == ownPid && entry.handle == anchor) fileType = entry.type;
        }
        if(fileType == 0) return Failure(ERROR_INVALID_DATA, LockInspectionStage::NativeSnapshot);
        std::vector<std::byte>().swap(buffer);
        IoWatchdog watchdog;
        WindowsScanOperations operations(mappings, watchdog, reopen);
        auto result = ScanNativeEntries(std::move(entries), fileType, ownPid, anchor, targetPath,
            (basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, operations);
        result.nativeScan.systemHandles = header.count;
        return result;
    }
    return Failure(ERROR_INSUFFICIENT_BUFFER, LockInspectionStage::NativeSnapshot);
}
}

LockInspectionResult ScanNativeEntries(std::vector<NativeHandleEntry> entries, uint16_t fileType,
    uint32_t ownPid, uintptr_t anchor, std::wstring_view target, bool directory, NativeScanOperations& operations)
{
    LockInspectionResult result;
    result.deepScan = true;
    auto& diagnostics = result.nativeScan;
    diagnostics.systemHandles = entries.size();
    const uint64_t started = operations.Milliseconds();
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const auto& entry) {
        return entry.type != fileType || (entry.pid == ownPid && entry.handle == anchor);
    }), entries.end());
    diagnostics.candidateHandles = entries.size();
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return std::tie(a.pid, a.handle) < std::tie(b.pid, b.handle);
    });
    struct ProcessGuard { NativeScanOperations& api; ~ProcessGuard() { api.CloseProcess(); } } guard{operations};
    uint32_t currentPid = 0;
    bool havePid = false;
    bool opened = false;
    size_t resourceCharacters = 0;
    for(size_t index = 0; index < entries.size(); ++index) {
        if(index >= 100000 || operations.Milliseconds() - started >= 5000 || resourceCharacters >= 1024 * 1024) {
            diagnostics.limitReached = true;
            break;
        }
        const auto& entry = entries[index];
        if(!havePid || entry.pid != currentPid) {
            operations.CloseProcess();
            havePid = true;
            currentPid = entry.pid;
            opened = operations.Open(currentPid);
            if(!opened) ++diagnostics.skippedProcesses;
        }
        if(!opened) continue;
        auto resource = operations.Resolve(entry.handle);
        if(resource.empty()) { ++diagnostics.skippedHandles; continue; }
        ++diagnostics.resolvedHandles;
        if(!MatchesLockPath(target, resource, directory)) continue;
        if(result.processes.empty() || result.processes.back().pid != currentPid) {
            LockingProcess process;
            process.pid = currentPid;
            process.source = DiscoverySource::NativeHandleScan;
            operations.Metadata(process);
            result.processes.push_back(std::move(process));
        }
        resourceCharacters += resource.size();
        result.processes.back().resources.push_back(std::move(resource));
    }
    diagnostics.elapsedMilliseconds = operations.Milliseconds() - started;
    if(diagnostics.skippedProcesses || diagnostics.skippedHandles || diagnostics.limitReached)
        result.status = LockInspectionStatus::PartialSuccess;
    return result;
}

LockInspectionResult InspectNativeHandles(const std::filesystem::path& path)
{
    const auto started = GetTickCount64();
    try {
        auto result = InspectNative(path);
        result.nativeScan.elapsedMilliseconds = GetTickCount64() - started;
        return result;
    } catch(const std::bad_alloc&) {
        return Failure(ERROR_OUTOFMEMORY, LockInspectionStage::NativeScan);
    } catch(const std::length_error&) {
        return Failure(ERROR_OUTOFMEMORY, LockInspectionStage::NativeScan);
    } catch(const std::system_error& error) {
        return Failure(static_cast<DWORD>(error.code().value()), LockInspectionStage::NativeScan);
    }
}
}
