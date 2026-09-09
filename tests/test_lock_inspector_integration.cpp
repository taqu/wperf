#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "lock_inspector.h"
#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <algorithm>
#include <iostream>
#include <thread>

namespace
{
// Own only resources created by this test; never recursively delete a directory.
struct TemporaryResource
{
    std::filesystem::path seed;
    std::filesystem::path directory;
    std::filesystem::path file;
    HANDLE handle = INVALID_HANDLE_VALUE;
    bool directoryCreated = false;
    bool fileCreated = false;

    void Create()
    {
        wchar_t temp[MAX_PATH + 1]{};
        DWORD length = GetTempPathW(MAX_PATH + 1, temp);
        REQUIRE(length > 0);
        REQUIRE(length <= MAX_PATH);
        wchar_t name[MAX_PATH + 1]{};
        REQUIRE(GetTempFileNameW(temp, L"wpf", 0, name) != 0);
        seed = name;
        directory = seed.native() + L" lock 資料";
        directoryCreated = CreateDirectoryW(directory.c_str(), nullptr) != FALSE;
        REQUIRE(directoryCreated);
        file = directory / L"open 日本語 file.txt";
        handle = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                             nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        fileCreated = handle != INVALID_HANDLE_VALUE;
        REQUIRE(fileCreated);
    }

    void Close()
    {
        if(handle != INVALID_HANDLE_VALUE) {
            CHECK(CloseHandle(handle) != FALSE);
            handle = INVALID_HANDLE_VALUE;
        }
    }

    ~TemporaryResource()
    {
        Close();
        if(fileCreated)
            CHECK(DeleteFileW(file.c_str()) != FALSE);
        if(directoryCreated)
            CHECK(RemoveDirectoryW(directory.c_str()) != FALSE);
        if(!seed.empty())
            CHECK(DeleteFileW(seed.c_str()) != FALSE);
    }
};

void RequireSuccess(const wperf::LockInspectionResult& result)
{
    INFO("status=" << static_cast<int>(result.status)
         << " stage=" << static_cast<int>(result.stage)
         << " nativeError=" << result.nativeError << " cleanupError=" << result.cleanupError);
    REQUIRE(result.status == wperf::LockInspectionStatus::Success);
    REQUIRE(result.cleanupError == 0);
}
}

TEST_CASE("Restart Manager detects this process holding a Unicode file and releases the association")
{
    TemporaryResource resource;
    resource.Create();
    const auto held = wperf::InspectLocks(resource.file);
    RequireSuccess(held);
    const DWORD pid = GetCurrentProcessId();
    const auto self = std::find_if(held.processes.begin(), held.processes.end(), [pid](const auto& process) {
        return process.pid == pid;
    });
    REQUIRE(self != held.processes.end());
    CHECK_FALSE(self->name.empty());
    CHECK(self->startTime != 0);
    resource.Close();
    const auto released = wperf::InspectLocks(resource.file);
    RequireSuccess(released);
    CHECK(released.processes.empty());
}

TEST_CASE("Restart Manager core rejects empty and nonexistent resources")
{
    CHECK(wperf::InspectLocks({}).status == wperf::LockInspectionStatus::InvalidPath);
    TemporaryResource resource;
    resource.Create();
    const auto missing = wperf::InspectLocks(resource.directory / L"does not exist.txt");
    CHECK(missing.status == wperf::LockInspectionStatus::InvalidPath);
    CHECK(missing.nativeError == ERROR_FILE_NOT_FOUND);
    CHECK(missing.stage == wperf::LockInspectionStage::ValidatePath);
}

TEST_CASE("Restart Manager core reports the directory backend limitation")
{
    TemporaryResource resource;
    resource.Create();
    const auto result = wperf::InspectLocks(resource.directory);
    INFO("nativeError=" << result.nativeError << " stage=" << static_cast<int>(result.stage));
    CHECK(result.status == wperf::LockInspectionStatus::DirectoryUnsupported);
    CHECK(result.nativeError == ERROR_ACCESS_DENIED);
    CHECK(result.stage == wperf::LockInspectionStage::GetList);
    CHECK(result.cleanupError == 0);
    CHECK(result.processes.empty());
}

namespace {
void RequireDeep(const wperf::LockInspectionResult& result)
{
    INFO("status=" << static_cast<int>(result.status) << " native=" << result.nativeError
         << " nt=" << result.nativeScan.ntStatus);
    REQUIRE((result.status == wperf::LockInspectionStatus::Success || result.status == wperf::LockInspectionStatus::PartialSuccess));
    REQUIRE(result.nativeError == 0);
    REQUIRE_FALSE(result.nativeScan.limitReached);
    std::cout << "native scan: " << result.nativeScan.elapsedMilliseconds << " ms, "
              << result.nativeScan.systemHandles << " system handles, "
              << result.nativeScan.candidateHandles << " file candidates, "
              << result.nativeScan.resolvedHandles << " resolved, "
              << result.nativeScan.skippedProcesses << " skipped processes, "
              << result.processes.size() << " matching processes\n";
}
bool HasResource(const wperf::LockInspectionResult& result, const std::filesystem::path& path)
{
    for(const auto& process : result.processes) {
        if(process.pid != GetCurrentProcessId()) continue;
        for(const auto& resource : process.resources)
            if(_wcsicmp(resource.c_str(), path.c_str()) == 0) return true;
    }
    return false;
}
}
TEST_CASE("native scan detects exact Unicode file and excludes released handle")
{
    TemporaryResource resource; resource.Create();
    const auto held = wperf::InspectLocks(resource.file, {true});
    RequireDeep(held);
    CHECK(HasResource(held, resource.file));
    const auto self = std::find_if(held.processes.begin(), held.processes.end(), [](const auto& p) { return p.pid == GetCurrentProcessId(); });
    REQUIRE(self != held.processes.end());
    CHECK(self->source == wperf::DiscoverySource::Both);
    CHECK_FALSE(self->name.empty());
    resource.Close();
    const auto released = wperf::InspectLocks(resource.file, {true});
    RequireDeep(released);
    CHECK_FALSE(HasResource(released, resource.file));
    CHECK(released.processes.empty());
}
TEST_CASE("native scan detects directory descendants and directory handle itself")
{
    TemporaryResource resource; resource.Create();
    HANDLE directory = CreateFileW(resource.directory.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    REQUIRE(directory != INVALID_HANDLE_VALUE);
    struct DirectoryOwner { HANDLE value; ~DirectoryOwner() { CloseHandle(value); } } owner{directory};
    const auto held = wperf::InspectLocks(resource.directory, {true});
    RequireDeep(held);
    CHECK(HasResource(held, resource.directory));
    CHECK(HasResource(held, resource.file));
    resource.Close();
    const auto releasedFile = wperf::InspectLocks(resource.directory, {true});
    RequireDeep(releasedFile);
    CHECK(HasResource(releasedFile, resource.directory));
    CHECK_FALSE(HasResource(releasedFile, resource.file));
}
TEST_CASE("native scan accepts extended target paths")
{
    TemporaryResource resource; resource.Create();
    const auto held = wperf::InspectLocks(std::filesystem::path(L"\\\\?\\" + resource.file.native()), {true});
    RequireDeep(held);
    CHECK(HasResource(held, resource.file));
}

TEST_CASE("native scan does not wait for a held synchronous pipe read")
{
    TemporaryResource resource; resource.Create();
    HANDLE readPipe = nullptr, writePipe = nullptr;
    REQUIRE(CreatePipe(&readPipe, &writePipe, nullptr, 0));
    struct PipeReader {
        HANDLE read;
        HANDLE write;
        std::thread worker;
        ~PipeReader() {
            DWORD written = 0;
            const char release = 'x';
            WriteFile(write, &release, 1, &written, nullptr);
            worker.join();
            CloseHandle(read);
            CloseHandle(write);
        }
    } pipe{readPipe, writePipe, std::thread([readPipe] {
        char value = 0;
        DWORD read = 0;
        ReadFile(readPipe, &value, 1, &read, nullptr);
    })};
    const auto result = wperf::InspectLocks(resource.file, {true});
    RequireDeep(result);
    CHECK(HasResource(result, resource.file));
}
