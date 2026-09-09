#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "lock_inspector.h"
#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <algorithm>

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
