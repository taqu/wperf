#include "doctest/doctest.h"
#include "lock_gui_presenter.h"

using namespace wperf;

TEST_CASE("Lock GUI presentation retains every matching resource and sorts rows")
{
    LockInspectionResult result;
    result.processes = {
        {20, L"zeta.exe", 0, {L"C:\\work\\b.obj", L"C:\\work\\a.obj"}, DiscoverySource::NativeHandleScan},
        {10, L"alpha.exe", 0, {}, DiscoverySource::RestartManager},
    };
    const auto view = PresentLockInspection(result);
    REQUIRE(view.rows.size() == 3);
    CHECK(view.rows[0].process == L"alpha.exe");
    CHECK(view.rows[0].resource.empty());
    CHECK(view.rows[1].resource == L"C:\\work\\a.obj");
    CHECK(view.rows[2].resource == L"C:\\work\\b.obj");
    CHECK(view.successful);
    CHECK_FALSE(view.partial);
    CHECK(view.status == L"2 locking processes found.");
}

TEST_CASE("Lock GUI presentation distinguishes empty partial results")
{
    LockInspectionResult result;
    result.status = LockInspectionStatus::PartialSuccess;
    const auto view = PresentLockInspection(result);
    CHECK(view.successful);
    CHECK(view.partial);
    CHECK(view.rows.empty());
    CHECK(view.status.find(L"inspected portion") != std::wstring::npos);
}

TEST_CASE("Lock GUI presentation reports useful native errors")
{
    LockInspectionResult result;
    result.status = LockInspectionStatus::AccessDenied;
    result.nativeError = 5;
    const auto view = PresentLockInspection(result);
    CHECK_FALSE(view.successful);
    CHECK(view.status.find(L"access denied") != std::wstring::npos);
    CHECK(view.status.find(L"5") != std::wstring::npos);
}
