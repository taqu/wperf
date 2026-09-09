#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "process_controller.h"
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>
#include <windows.h>

using namespace wperf;

namespace
{
struct Child
{
    PROCESS_INFORMATION information{};
    Child() = default;
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    Child(Child&& other) noexcept
        : information(other.information)
    {
        other.information = {};
    }
    ~Child()
    {
        if(information.hProcess) {
            if(WaitForSingleObject(information.hProcess, 0) == WAIT_TIMEOUT)
                TerminateProcess(information.hProcess, 0x54455354);
            CloseHandle(information.hProcess);
            CloseHandle(information.hThread);
        }
    }
};

Child StartInspectorChild()
{
    wchar_t module[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
    REQUIRE(length > 0);
    const std::filesystem::path testPath(module);
    const auto configuration = testPath.parent_path().filename();
    const auto executable = testPath.parent_path().parent_path().parent_path() / configuration / L"wperf.exe";
    std::wstring command = L"\"" + executable.native() + L"\" --lock-ui";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    Child child;
    STARTUPINFOW startup{sizeof(startup)};
    REQUIRE(CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                           0, nullptr, nullptr, &startup, &child.information)
            != FALSE);
    REQUIRE(WaitForInputIdle(child.information.hProcess, 10000) != WAIT_FAILED);
    return child;
}

ProcessIdentity WaitForIdentity(DWORD pid)
{
    for(int attempt = 0; attempt < 100; ++attempt) {
        const auto identity = CaptureProcessIdentity(pid);
        if(identity.valid)
            return identity;
        Sleep(50);
    }
    return {};
}
} // namespace

TEST_CASE("controlled Lock Inspector child accepts graceful close")
{
    auto child = StartInspectorChild();
    const auto identity = WaitForIdentity(child.information.dwProcessId);
    REQUIRE(identity.valid);
    const auto result = RequestGracefulClose(identity, 3000);
    CHECK(result.status == ProcessControlStatus::ProcessExited);
    CHECK(WaitForSingleObject(child.information.hProcess, 0) == WAIT_OBJECT_0);
}

TEST_CASE("controlled Lock Inspector child accepts force termination")
{
    auto child = StartInspectorChild();
    const auto identity = WaitForIdentity(child.information.dwProcessId);
    REQUIRE(identity.valid);
    const auto result = ForceTerminate(identity, 3000);
    CHECK(result.status == ProcessControlStatus::ProcessExited);
    CHECK(WaitForSingleObject(child.information.hProcess, 0) == WAIT_OBJECT_0);
}

TEST_CASE("stale process identity refuses control")
{
    auto child = StartInspectorChild();
    const auto identity = WaitForIdentity(child.information.dwProcessId);
    REQUIRE(identity.valid);
    auto stale = identity;
    ++stale.creationTime;
    const auto result = ForceTerminate(stale, 3000);
    CHECK(result.status == ProcessControlStatus::IdentityMismatch);
    CHECK(WaitForSingleObject(child.information.hProcess, 0) == WAIT_TIMEOUT);
}

TEST_CASE("already exited process is reported without another action")
{
    auto child = StartInspectorChild();
    const auto identity = WaitForIdentity(child.information.dwProcessId);
    REQUIRE(identity.valid);
    REQUIRE(ForceTerminate(identity, 3000).status == ProcessControlStatus::ProcessExited);
    CHECK(RequestGracefulClose(identity, 500).status == ProcessControlStatus::AlreadyExited);
}
