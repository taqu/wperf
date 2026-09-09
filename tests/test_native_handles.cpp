#include <doctest/doctest.h>
#include "native_handle_backend.h"
#include "lock_cli.h"
#include <limits>
#include <map>
using namespace wperf;
using namespace wperf::detail;
namespace {
struct FakeOperations : NativeScanOperations {
    std::vector<uint32_t> opened;
    std::map<uintptr_t, std::wstring> paths;
    uint32_t inaccessible = 9;
    uint32_t active = 0;
    int resolved = 0;
    int metadata = 0;
    bool throwResolve = false;
    uint64_t clock = 0;
    uint64_t increment = 0;
    bool Open(uint32_t pid) override { opened.push_back(pid); active = pid == inaccessible ? 0 : pid; return active != 0; }
    void CloseProcess() override { active = 0; }
    std::wstring Resolve(uintptr_t handle) override {
        ++resolved;
        if(throwResolve) throw std::bad_alloc();
        return paths[handle];
    }
    void Metadata(LockingProcess& p) override { ++metadata; p.name = L"test"; p.startTime = 42; }
    uint64_t Milliseconds() override { const auto now = clock; clock += increment; return now; }
};
LockInspectionResult Primary() { LockInspectionResult r; r.processes = {{7, L"display", 42}}; return r; }
LockInspectionResult Native() {
    LockInspectionResult r; r.processes = {{7, L"native.exe", 42, {L"C:\\build\\a"}, DiscoverySource::NativeHandleScan}};
    return r;
}
}
TEST_CASE("native path normalization handles Win32 extended and UNC prefixes") {
    CHECK(NormalizeLockPath(L"\\\\?\\C:\\作業 folder\\") == L"C:\\作業 folder");
    CHECK(NormalizeLockPath(L"\\??\\C:\\x") == L"C:\\x");
    CHECK(NormalizeLockPath(L"\\\\?\\UNC\\server\\share\\x") == L"\\\\server\\share\\x");
    CHECK(NormalizeLockPath(L"\\Device\\Mup\\server\\share\\x") == L"\\\\server\\share\\x");
    CHECK(NormalizeLockPath(L"C:/build/") == L"C:\\build");
    CHECK(NormalizeLockPath(L"C:\\") == L"C:\\");
}
TEST_CASE("native device mapping uses longest Unicode case insensitive component prefix") {
    const std::vector<DeviceMapping> mappings = {{L"\\Device\\HarddiskVolume3", L"C:"}, {L"\\Device\\HarddiskVolume30", L"D:"}};
    CHECK(NormalizeLockPath(L"\\device\\harddiskvolume3\\日本", mappings) == L"C:\\日本");
    CHECK(NormalizeLockPath(L"\\Device\\HarddiskVolume30\\x", mappings) == L"D:\\x");
    CHECK(NormalizeLockPath(L"\\Device\\HarddiskVolume300\\x", mappings) == L"\\Device\\HarddiskVolume300\\x");
}
TEST_CASE("native file matching is exact with ordinal Unicode case comparison") {
    CHECK(MatchesLockPath(L"C:\\日本\\Ä.txt", L"c:\\日本\\ä.TXT", false));
    CHECK_FALSE(MatchesLockPath(L"C:\\build\\a", L"C:\\build\\a\\x", false));
    CHECK_FALSE(MatchesLockPath(L"C:\\build\\a", L"C:\\build\\b", false));
    CHECK_FALSE(MatchesLockPath(L"", L"C:\\a", true));
}
TEST_CASE("native descendants respect boundaries roots trailing slash and Unicode") {
    const auto target = NormalizeLockPath(L"C:\\project\\build\\");
    for(auto path : {L"C:\\project\\build", L"c:\\PROJECT\\Build\\a.dll", L"C:\\project\\build\\sub\\日本.dll"})
        CHECK(MatchesLockPath(target, path, true));
    for(auto path : {L"C:\\project\\builder\\a.dll", L"C:\\project\\build-old\\a.dll", L"C:\\project\\other\\a.dll"})
        CHECK_FALSE(MatchesLockPath(target, path, true));
    CHECK(MatchesLockPath(L"C:\\", L"C:\\日本 file", true));
    CHECK(MatchesLockPath(L"\\\\server\\share", L"\\\\SERVER\\share\\a", true));
    CHECK_FALSE(MatchesLockPath(L"\\\\server\\share", L"\\\\server\\share2\\a", true));
}
TEST_CASE("native buffer growth bounds unreasonable sizes and overflow") {
    CHECK(NextSnapshotSize(1024, 3000) == 3000);
    CHECK(NextSnapshotSize(1024, 0) == 2048);
    CHECK(NextSnapshotSize(64 * 1024 * 1024, 0) == 0);
    CHECK(NextSnapshotSize(1024, std::numeric_limits<size_t>::max()) == 0);
    CHECK(NextSnapshotSize(40 * 1024 * 1024, 0) == 64 * 1024 * 1024);
}
TEST_CASE("native merge deduplicates process resources and sources deterministically") {
    auto native = Native();
    native.processes[0].resources = {L"C:\\build\\z", L"\\\\?\\C:\\build\\a", L"c:\\BUILD\\A"};
    const auto merged = MergeLockResults(Primary(), native);
    REQUIRE(merged.processes.size() == 1);
    CHECK(merged.processes[0].source == DiscoverySource::Both);
    CHECK(merged.processes[0].name == L"display");
    REQUIRE(merged.processes[0].resources.size() == 2);
    CHECK(CompareLockPaths(merged.processes[0].resources[0], L"C:\\build\\a") == 0);
    CHECK(merged.processes[0].resources[1] == L"C:\\build\\z");
}
TEST_CASE("native merge preserves distinct known process lifetimes") {
    auto native = Native(); native.processes[0].startTime = 99;
    const auto result = MergeLockResults(Primary(), native);
    CHECK(result.processes.size() == 2);
    CHECK(result.status == LockInspectionStatus::PartialSuccess);
    native.processes[0].startTime = 0;
    CHECK(MergeLockResults(Primary(), native).processes.size() == 1);
}
TEST_CASE("native merge handles complete partial and failed backends") {
    auto primary = Primary(); auto native = Native();
    native.status = LockInspectionStatus::PartialSuccess; native.nativeScan.skippedProcesses = 1;
    auto result = MergeLockResults(primary, native);
    CHECK(result.status == LockInspectionStatus::PartialSuccess);
    CHECK(result.nativeScan.skippedProcesses == 1);
    native.status = LockInspectionStatus::NativeScanFailure; native.nativeError = 5;
    result = MergeLockResults(primary, native);
    CHECK(result.status == LockInspectionStatus::PartialSuccess);
    CHECK(result.nativeError == 5);
    REQUIRE(result.processes.size() == 1);
    CHECK(result.processes[0].source == DiscoverySource::RestartManager);
    primary.status = LockInspectionStatus::DirectoryUnsupported; primary.nativeError = 5;
    result = MergeLockResults(primary, Native());
    CHECK(result.status == LockInspectionStatus::Success);
    CHECK(result.restartManagerError == 5);
    result = MergeLockResults(primary, native);
    CHECK(result.status == LockInspectionStatus::NativeScanFailure);
    CHECK(result.processes.empty());
    primary.status = LockInspectionStatus::RestartManagerFailure; primary.cleanupError = 29;
    result = MergeLockResults(primary, Native());
    CHECK(result.status == LockInspectionStatus::PartialSuccess);
    CHECK(result.cleanupError == 29);
}
TEST_CASE("native scan filters types and own anchor and opens each PID once") {
    FakeOperations ops;
    ops.paths = {{11, L"C:\\build\\a"}, {12, L"C:\\build\\b"}, {13, L"C:\\builder\\a"}};
    auto result = ScanNativeEntries({{7, 12, 2}, {7, 11, 2}, {7, 13, 2}, {7, 14, 3}, {1, 20, 2}},
        2, 1, 20, L"C:\\build", true, ops);
    CHECK(result.status == LockInspectionStatus::Success);
    CHECK(ops.opened == std::vector<uint32_t>{7});
    CHECK(ops.resolved == 3); CHECK(ops.metadata == 1); CHECK(ops.active == 0);
    REQUIRE(result.processes.size() == 1); CHECK(result.processes[0].resources.size() == 2);
    CHECK(result.nativeScan.systemHandles == 5); CHECK(result.nativeScan.candidateHandles == 3);
}
TEST_CASE("native scan skips inaccessible processes and raced handles without losing results") {
    FakeOperations ops; ops.paths = {{11, L"C:\\build\\a"}};
    auto result = ScanNativeEntries({{9, 1, 2}, {9, 2, 2}, {7, 10, 2}, {7, 11, 2}}, 2, 1, 20, L"C:\\build", true, ops);
    CHECK(result.status == LockInspectionStatus::PartialSuccess);
    CHECK(result.nativeScan.skippedProcesses == 1); CHECK(result.nativeScan.skippedHandles == 1);
    REQUIRE(result.processes.size() == 1); CHECK(result.processes[0].pid == 7);
    CHECK(ops.opened.size() == 2); CHECK(ops.active == 0);
}
TEST_CASE("native scan budget returns partial and cleanup runs on exceptions") {
    FakeOperations ops; ops.increment = 5000;
    auto result = ScanNativeEntries({{7, 1, 2}}, 2, 1, 20, L"C:\\build", true, ops);
    CHECK(result.status == LockInspectionStatus::PartialSuccess);
    CHECK(result.nativeScan.limitReached); CHECK(ops.opened.empty());
    ops.increment = 0; ops.throwResolve = true;
    CHECK_THROWS_AS(ScanNativeEntries({{7, 1, 2}}, 2, 1, 20, L"C:\\build", true, ops), std::bad_alloc);
    CHECK(ops.active == 0);
}
TEST_CASE("deep CLI parsing dispatch partial exit and additive JSON") {
    const std::vector<std::wstring_view> args = {L"--deep", L"--lock", L"C:\\build", L"--json"};
    const auto options = cli::Parse(args);
    CHECK(options.mode == cli::Mode::Inspect); CHECK(options.deep);
    const auto never = +[](const std::filesystem::path&) -> LockInspectionResult { FAIL("normal inspector called"); return {}; };
    const auto deep = +[](const std::filesystem::path&) { auto r = Native(); r.status = LockInspectionStatus::PartialSuccess; return r; };
    auto result = cli::Run(options, never, deep);
    CHECK(result.exitCode == 3); CHECK(result.err.empty());
    CHECK(result.out.find(L"\"status\":\"success\"") != std::wstring::npos);
    CHECK(result.out.find(L"\"complete\":false") != std::wstring::npos);
    CHECK(result.out.find(L"\"source\":\"native_handle_scan\",\"resources\":[\"C:\\\\build\\\\a\"]") != std::wstring::npos);
    const std::vector<std::wstring_view> duplicate = {L"--lock", L"C:\\a", L"--deep", L"--deep"};
    CHECK(cli::Parse(duplicate).mode == cli::Mode::Invalid);
    const std::vector<std::wstring_view> alone = {L"--deep"};
    CHECK(cli::Parse(alone).mode == cli::Mode::Invalid);
}
