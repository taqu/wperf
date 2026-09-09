#include <doctest/doctest.h>
#include "lock_inspector_internal.h"
#include <algorithm>
#include <iterator>
#include <new>
#include <stdexcept>

namespace
{
using namespace wperf;

struct Reply
{
    DWORD error = ERROR_SUCCESS;
    UINT needed = 0;
    std::vector<RM_PROCESS_INFO> entries;
};

struct Backend
{
    // Test-local callback state; production always receives its own API table.
    static inline Backend* current = nullptr;
    DWORD attributeError = ERROR_SUCCESS;
    DWORD attributes = FILE_ATTRIBUTE_NORMAL;
    DWORD startError = ERROR_SUCCESS;
    DWORD registerError = ERROR_SUCCESS;
    DWORD endError = ERROR_SUCCESS;
    unsigned attributeCalls = 0;
    unsigned starts = 0;
    unsigned registrations = 0;
    unsigned ends = 0;
    size_t listCalls = 0;
    std::vector<UINT> capacities;
    std::vector<Reply> replies{{}};
    std::wstring registeredPath;
    bool allocationFailure = false;
    bool registrationException = false;

    Backend() { current = this; }
    ~Backend() { current = nullptr; }

    static DWORD Attributes(LPCWSTR, DWORD* value)
    {
        ++current->attributeCalls;
        *value = current->attributes;
        return current->attributeError;
    }
    static DWORD WINAPI Start(DWORD* session, DWORD flags, WCHAR* key)
    {
        ++current->starts;
        CHECK(flags == 0);
        CHECK(key != nullptr);
        *session = 0; // A successful zero-valued handle must still be ended.
        return current->startError;
    }
    static DWORD WINAPI Register(DWORD session, UINT files, LPCWSTR* paths,
                                 UINT applications, RM_UNIQUE_PROCESS*, UINT services, LPCWSTR*)
    {
        ++current->registrations;
        CHECK(session == 0);
        CHECK(files == 1);
        CHECK(applications == 0);
        CHECK(services == 0);
        current->registeredPath = paths[0];
        if(current->registrationException)
            throw std::runtime_error("test exception");
        return current->registerError;
    }
    static DWORD WINAPI List(DWORD, UINT* needed, UINT* count, RM_PROCESS_INFO* entries, DWORD* reasons)
    {
        if(current->allocationFailure)
            throw std::bad_alloc();
        current->capacities.push_back(*count);
        CHECK((entries == nullptr) == (*count == 0));
        if(current->listCalls >= current->replies.size()) {
            FAIL_CHECK("Unexpected extra RmGetList call");
            return ERROR_INVALID_DATA;
        }
        const auto& reply = current->replies[current->listCalls++];
        if(reply.entries.size() > *count) {
            FAIL_CHECK("Insufficient process buffer");
            return ERROR_INVALID_DATA;
        }
        if(!reply.entries.empty())
            std::copy(reply.entries.begin(), reply.entries.end(), entries);
        *needed = reply.needed;
        *count = static_cast<UINT>(reply.entries.size());
        *reasons = 0;
        return reply.error;
    }
    static DWORD WINAPI End(DWORD session)
    {
        CHECK(session == 0);
        ++current->ends;
        return current->endError;
    }
    LockInspectionResult Inspect(const std::filesystem::path& path = L"C:\\test file.txt")
    {
        const detail::LockInspectorApi api{Attributes, Start, Register, List, End};
        return detail::InspectLocksWithApi(path, api);
    }
};

RM_PROCESS_INFO Process(DWORD pid, const wchar_t* name = L"Example", DWORD startLow = 1, DWORD startHigh = 0)
{
    RM_PROCESS_INFO result{};
    result.Process.dwProcessId = pid;
    result.Process.ProcessStartTime = {startLow, startHigh};
    wcscpy_s(result.strAppName, name);
    return result;
}
}

TEST_CASE("Lock inspector rejects empty relative and embedded null paths before OS calls")
{
    Backend backend;
    for(const auto& path : {std::filesystem::path{}, std::filesystem::path(L"relative.txt"),
                           std::filesystem::path(L"C:relative.txt"),
                           std::filesystem::path(std::wstring(L"C:\\valid.txt\0extra", 18))}) {
        const auto result = backend.Inspect(path);
        CHECK(result.status == LockInspectionStatus::InvalidPath);
        CHECK(result.stage == LockInspectionStage::ValidatePath);
        CHECK(result.nativeError == ERROR_INVALID_NAME);
        CHECK(result.processes.empty());
    }
    CHECK(backend.attributeCalls == 0);
    CHECK(backend.starts == 0);
}

TEST_CASE("Lock inspector maps missing and malformed resources without starting a session")
{
    Backend backend;
    for(DWORD error : {ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND, ERROR_INVALID_NAME,
                       ERROR_BAD_PATHNAME, ERROR_DIRECTORY, ERROR_FILENAME_EXCED_RANGE}) {
        backend.attributeError = error;
        const auto result = backend.Inspect();
        CHECK(result.status == LockInspectionStatus::InvalidPath);
        CHECK(result.nativeError == error);
    }
    CHECK(backend.starts == 0);
}

TEST_CASE("Lock inspector preserves permission and unexpected errors with stage context")
{
    Backend backend;
    backend.attributeError = ERROR_ACCESS_DENIED;
    auto result = backend.Inspect();
    CHECK(result.status == LockInspectionStatus::AccessDenied);
    CHECK(result.stage == LockInspectionStage::ValidatePath);
    backend.attributeError = ERROR_SUCCESS;
    backend.startError = ERROR_SEM_TIMEOUT;
    result = backend.Inspect();
    CHECK(result.status == LockInspectionStatus::RestartManagerFailure);
    CHECK(result.stage == LockInspectionStage::StartSession);
    CHECK(result.nativeError == ERROR_SEM_TIMEOUT);
    CHECK(backend.registrations == 0);
    CHECK(backend.ends == 0);
}

TEST_CASE("Lock inspector cleans up on registration failure")
{
    Backend backend;
    backend.registerError = ERROR_PRIVILEGE_NOT_HELD;
    const auto result = backend.Inspect();
    CHECK(result.status == LockInspectionStatus::AccessDenied);
    CHECK(result.stage == LockInspectionStage::RegisterResource);
    CHECK(result.nativeError == ERROR_PRIVILEGE_NOT_HELD);
    CHECK(result.processes.empty());
    CHECK(backend.listCalls == 0);
    CHECK(backend.ends == 1);
}

TEST_CASE("Lock inspector preserves absolute Unicode and spaced paths verbatim")
{
    Backend backend;
    const std::filesystem::path path(L"C:\\日本語 folder\\..\\資料 file.txt");
    const auto result = backend.Inspect(path);
    CHECK(result.status == LockInspectionStatus::Success);
    CHECK(backend.registeredPath == path.native());
    CHECK(backend.registrations == 1);
    CHECK(backend.ends == 1);
}

TEST_CASE("Lock inspector distinguishes an empty successful snapshot from failure")
{
    Backend backend;
    const auto result = backend.Inspect();
    CHECK(result.status == LockInspectionStatus::Success);
    CHECK(result.stage == LockInspectionStage::None);
    CHECK(result.nativeError == 0);
    CHECK(result.cleanupError == 0);
    CHECK(result.processes.empty());
    CHECK(backend.listCalls == 1);
    CHECK(backend.ends == 1);
}

TEST_CASE("Lock inspector resizes again when the process list grows")
{
    Backend backend;
    backend.replies = {{ERROR_MORE_DATA, 1, {}}, {ERROR_MORE_DATA, 3, {Process(999)}},
                       {ERROR_SUCCESS, 0, {Process(42, L"資料"), Process(7), Process(9)}}};
    const auto result = backend.Inspect();
    REQUIRE(result.status == LockInspectionStatus::Success);
    REQUIRE(result.processes.size() == 3);
    CHECK(result.processes[0].pid == 7);
    CHECK(result.processes[1].pid == 9);
    CHECK(result.processes[2].pid == 42);
    CHECK(result.processes[2].name == L"資料");
    CHECK(backend.capacities == std::vector<UINT>{0, 1, 3});
    CHECK(backend.ends == 1);
}

TEST_CASE("Lock inspector uses the returned count when processes disappear")
{
    Backend backend;
    backend.replies = {{ERROR_MORE_DATA, 3, {}}, {ERROR_SUCCESS, 0, {Process(5)}}};
    auto result = backend.Inspect();
    REQUIRE(result.processes.size() == 1);
    CHECK(result.processes[0].pid == 5);
    backend.listCalls = 0;
    backend.replies = {{ERROR_MORE_DATA, 3, {}}, {ERROR_SUCCESS, 0, {}}};
    result = backend.Inspect();
    CHECK(result.status == LockInspectionStatus::Success);
    CHECK(result.processes.empty());
    CHECK(backend.ends == 2);
}

TEST_CASE("Lock inspector bounds retries and discards incomplete records")
{
    Backend backend;
    backend.replies = {{ERROR_MORE_DATA, 1, {}}, {ERROR_MORE_DATA, 2, {Process(1)}},
                       {ERROR_MORE_DATA, 3, {Process(1)}}, {ERROR_MORE_DATA, 4, {Process(1)}}};
    const auto result = backend.Inspect();
    CHECK(result.status == LockInspectionStatus::RestartManagerFailure);
    CHECK(result.stage == LockInspectionStage::GetList);
    CHECK(result.nativeError == ERROR_MORE_DATA);
    CHECK(result.processes.empty());
    CHECK(backend.listCalls == 4);
    CHECK(backend.ends == 1);
}

TEST_CASE("Lock inspector sorts and deduplicates process identities deterministically")
{
    const std::vector<RM_PROCESS_INFO> records{Process(42, L"", 4, 1), Process(7),
        Process(42, L"Zulu", 4, 1), Process(42, L"Alpha", 4, 1), Process(42, L"New", 5, 1)};
    Backend backend;
    backend.replies = {{ERROR_MORE_DATA, 5, {}}, {ERROR_SUCCESS, 0, records}};
    const auto first = backend.Inspect();
    REQUIRE(first.processes.size() == 3);
    CHECK(first.processes[0].pid == 7);
    CHECK(first.processes[1].name == L"Alpha");
    CHECK(first.processes[1].startTime == (1ULL << 32) + 4);
    CHECK(first.processes[2].name == L"New");
    std::reverse(backend.replies[1].entries.begin(), backend.replies[1].entries.end());
    backend.listCalls = 0;
    const auto second = backend.Inspect();
    REQUIRE(second.processes.size() == first.processes.size());
    for(size_t i = 0; i < first.processes.size(); ++i) {
        CHECK(first.processes[i].pid == second.processes[i].pid);
        CHECK(first.processes[i].startTime == second.processes[i].startTime);
        CHECK(first.processes[i].name == second.processes[i].name);
    }
}

TEST_CASE("Lock inspector safely copies missing and full length display names")
{
    auto full = Process(2);
    std::fill(std::begin(full.strAppName), std::end(full.strAppName), L'X');
    Backend backend;
    backend.replies = {{ERROR_MORE_DATA, 2, {}}, {ERROR_SUCCESS, 0, {Process(1, L""), full}}};
    const auto result = backend.Inspect();
    REQUIRE(result.processes.size() == 2);
    CHECK(result.processes[0].name.empty());
    CHECK(result.processes[1].name == std::wstring(CCH_RM_MAX_APP_NAME + 1, L'X'));
}

TEST_CASE("Lock inspector distinguishes directory limitations from file access denial")
{
    Backend backend;
    backend.replies = {{ERROR_ACCESS_DENIED, 0, {}}};
    const auto file = backend.Inspect();
    CHECK(file.status == LockInspectionStatus::AccessDenied);
    backend.attributes = FILE_ATTRIBUTE_DIRECTORY;
    backend.listCalls = 0;
    const auto directory = backend.Inspect(L"C:\\folder");
    CHECK(directory.status == LockInspectionStatus::DirectoryUnsupported);
    CHECK(directory.stage == LockInspectionStage::GetList);
    CHECK(directory.nativeError == ERROR_ACCESS_DENIED);
    CHECK(directory.processes.empty());
    CHECK(backend.ends == 2);
}

TEST_CASE("Lock inspector reports list failures without retrying unrelated errors")
{
    Backend backend;
    backend.replies = {{ERROR_MORE_DATA, 1, {}}, {ERROR_CANCELLED, 0, {}}};
    const auto result = backend.Inspect();
    CHECK(result.status == LockInspectionStatus::RestartManagerFailure);
    CHECK(result.nativeError == ERROR_CANCELLED);
    CHECK(result.stage == LockInspectionStage::GetList);
    CHECK(result.processes.empty());
    CHECK(backend.listCalls == 2);
    CHECK(backend.ends == 1);
}

TEST_CASE("Lock inspector cleans up when allocation or another exception occurs")
{
    Backend backend;
    backend.allocationFailure = true;
    const auto result = backend.Inspect();
    CHECK(result.nativeError == ERROR_OUTOFMEMORY);
    CHECK(result.status == LockInspectionStatus::RestartManagerFailure);
    CHECK(backend.ends == 1);
    backend.registrationException = true;
    CHECK_THROWS_AS(backend.Inspect(), std::runtime_error);
    CHECK(backend.ends == 2);
}

TEST_CASE("Lock inspector reports cleanup failure without hiding an earlier failure")
{
    Backend backend;
    backend.endError = ERROR_WRITE_FAULT;
    auto result = backend.Inspect();
    CHECK(result.status == LockInspectionStatus::RestartManagerFailure);
    CHECK(result.stage == LockInspectionStage::EndSession);
    CHECK(result.nativeError == ERROR_WRITE_FAULT);
    CHECK(result.cleanupError == ERROR_WRITE_FAULT);
    backend.registerError = ERROR_ACCESS_DENIED;
    result = backend.Inspect();
    CHECK(result.status == LockInspectionStatus::AccessDenied);
    CHECK(result.stage == LockInspectionStage::RegisterResource);
    CHECK(result.nativeError == ERROR_ACCESS_DENIED);
    CHECK(result.cleanupError == ERROR_WRITE_FAULT);
    CHECK(backend.ends == 2);
}
