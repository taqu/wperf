#include "doctest/doctest.h"
#include "process_controller.h"
#include <windows.h>

using namespace wperf;

TEST_CASE("Process identity requires PID and creation time")
{
    const ProcessIdentity expected{42, 100, true};
    CHECK(SameProcessIdentity(expected, ProcessIdentity{42, 100, true}));
    CHECK_FALSE(SameProcessIdentity(expected, ProcessIdentity{42, 101, true}));
    CHECK_FALSE(SameProcessIdentity(expected, ProcessIdentity{43, 100, true}));
    CHECK_FALSE(SameProcessIdentity(expected, ProcessIdentity{42, 0, false}));
}

TEST_CASE("Process controller refuses invalid and self identities")
{
    CHECK(RequestGracefulClose(ProcessIdentity{}).status == ProcessControlStatus::InvalidIdentity);
    const ProcessIdentity self{GetCurrentProcessId(), 1, true};
    CHECK(ForceTerminate(self).status == ProcessControlStatus::SelfTarget);
}
