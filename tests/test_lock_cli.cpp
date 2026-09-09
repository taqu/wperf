#include <doctest/doctest.h>
#include "lock_cli.h"
#include <array>
using namespace wperf;
using namespace wperf::cli;
namespace {
int calls;
LockInspectionResult response;
std::filesystem::path received;
LockInspectionResult Inspect(const std::filesystem::path& path) { ++calls; received = path; return response; }
Options Args(std::initializer_list<std::wstring_view> args) { return Parse({args.begin(), args.size()}); }
}
TEST_CASE("CLI desktop and help do not inspect") {
    calls = 0;
    CHECK(Args({}).mode == Mode::Desktop);
    CHECK(Run(Args({}), Inspect).exitCode == 0);
    CHECK(Run(Args({L"--help"}), Inspect).out.find(L"--lock") != std::wstring::npos);
    CHECK(calls == 0);
}
TEST_CASE("CLI accepts wide paths and JSON in either order") {
    const auto options = Args({L"--lock", L"C:\\作業 folder\\file.txt", L"--json"});
    CHECK(options.mode == Mode::Inspect);
    CHECK(options.json);
    CHECK(options.path == L"C:\\作業 folder\\file.txt");
    CHECK(Args({L"--json", L"--lock", L"C:\\file"}).mode == Mode::Inspect);
}
TEST_CASE("CLI rejects missing empty unknown duplicate and conflicting arguments") {
    const std::vector<std::vector<std::wstring_view>> cases = {
        {L"--lock"}, {L"--lock", L""}, {L"--json"}, {L"--unknown"},
        {L"--lock", L"--json"}, {L"--help", L"--lock", L"C:\\file"},
        {L"--lock", L"C:\\file", L"extra"}, {L"--lock", L"C:\\file", L"--lock", L"C:\\other"},
        {L"--lock", L"C:\\file", L"--json", L"--json"}};
    calls = 0;
    for(const auto& args : cases) {
        const auto output = Run(Parse(args), Inspect);
        CHECK(output.exitCode == 2);
        CHECK(output.out.empty());
        CHECK_FALSE(output.err.empty());
    }
    CHECK(calls == 0);
}
TEST_CASE("CLI invokes core once and empty success is clear") {
    calls = 0; response = {};
    auto output = Run(Args({L"--lock", L"C:\\作業 file"}), Inspect);
    CHECK(calls == 1);
    CHECK(received.native() == L"C:\\作業 file");
    CHECK(output.exitCode == 0);
    CHECK(output.err.empty());
    CHECK(output.out.find(L"No locking processes found.") != std::wstring::npos);
    output = Run(Args({L"--lock", L"C:\\file", L"--json"}), Inspect);
    CHECK(output.out == L"{\"path\":\"C:\\\\file\",\"status\":\"success\",\"processes\":[]}\n");
}
TEST_CASE("CLI preserves core process ordering and names in both formats") {
    response = {}; response.processes = {{7, L"first", 1}, {12, L"", 2}};
    const auto options = Args({L"--lock", L"C:\\file"});
    auto output = Run(options, Inspect);
    CHECK(output.out.find(L"7\tfirst") < output.out.find(L"12\t(name unavailable)"));
    auto json = options; json.json = true;
    output = Run(json, Inspect);
    CHECK(output.out.find(L"{\"pid\":7,\"name\":\"first\"}") < output.out.find(L"{\"pid\":12,\"name\":\"\"}"));
}
TEST_CASE("CLI JSON escapes quotes slashes controls and Unicode") {
    response = {}; response.processes = {{1, L"\"\\\n\t\r\b\f\x0001日本\U0001f600", 0}};
    const auto output = Run(Args({L"--lock", L"C:\\file", L"--json"}), Inspect);
    CHECK(output.out.find(L"\\\"\\\\\\u000a\\u0009\\u000d\\u0008\\u000c\\u0001日本\\ud83d\\ude00") != std::wstring::npos);
}
TEST_CASE("CLI maps all core errors and retains native and cleanup context") {
    const std::array statuses = {LockInspectionStatus::InvalidPath, LockInspectionStatus::AccessDenied,
        LockInspectionStatus::DirectoryUnsupported, LockInspectionStatus::RestartManagerFailure};
    for(auto status : statuses) {
        response = {}; response.status = status; response.nativeError = 5;
        response.cleanupError = 6; response.stage = LockInspectionStage::GetList;
        const auto human = Run(Args({L"--lock", L"C:\\file"}), Inspect);
        CHECK(human.exitCode == 1); CHECK(human.out.empty());
        CHECK(human.err.find(L"Windows error 5") != std::wstring::npos);
        const auto json = Run(Args({L"--lock", L"C:\\file", L"--json"}), Inspect);
        CHECK(json.exitCode == 1); CHECK(json.err.empty());
        CHECK(json.out.find(L"\"native_code\":5,\"stage\":\"get_list\",\"cleanup_code\":6") != std::wstring::npos);
    }
}
