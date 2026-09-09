#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "app_logic.h"
#include <limits>
#include <string>

namespace
{
std::wstring Bytes(uint64_t bytes)
{
    wchar_t buffer[64]{};
    wperf::FormatBytes(64, buffer, bytes);
    return buffer;
}

std::wstring Speed(double bps)
{
    wchar_t buffer[64]{};
    wperf::FormatNetworkSpeed(64, buffer, bps);
    return buffer;
}

// Display ASCII-only metric strings in doctest's failure diagnostics.
std::string Text(const std::wstring& text)
{
    std::string result;
    for(wchar_t c : text)
        result.push_back(static_cast<char>(c));
    return result;
}
}

TEST_CASE("Memory formatting keeps GB units for zero and small values")
{
    CHECK(Text(Bytes(0)) == "0.0 GB");
    CHECK(Text(Bytes(1024 * 1024)) == "0.0 GB");
}

TEST_CASE("Memory formatting uses binary GB and one decimal")
{
    CHECK(Text(Bytes(1ULL << 30)) == "1.0 GB");
    CHECK(Text(Bytes(7ULL << 28)) == "1.8 GB");
}

TEST_CASE("Memory formatting accepts large unsigned values")
{
    CHECK(Text(Bytes(1ULL << 40)) == "1024.0 GB");
    CHECK(Text(Bytes((std::numeric_limits<uint64_t>::max)())) == "17179869184.0 GB");
}

TEST_CASE("Speed formatting handles zero and fractional bytes")
{
    CHECK(Text(Speed(0)) == "0 B/s");
    CHECK(Text(Speed(12.75)) == "13 B/s");
}

TEST_CASE("Speed formatting changes to KB at 1024")
{
    CHECK(Text(Speed(1023)) == "1023 B/s");
    CHECK(Text(Speed(1024)) == "1.0 KB/s");
    CHECK(Text(Speed(1536)) == "1.5 KB/s");
}

TEST_CASE("Speed formatting changes to MB at the binary boundary")
{
    CHECK(Text(Speed(1048575)) == "1024.0 KB/s");
    CHECK(Text(Speed(1048576)) == "1.00 MB/s");
    CHECK(Text(Speed(1310720)) == "1.25 MB/s");
}

TEST_CASE("Speed formatting changes to GB and retains GB for large rates")
{
    CHECK(Text(Speed(1073741823)) == "1024.00 MB/s");
    CHECK(Text(Speed(1073741824)) == "1.00 GB/s");
    CHECK(Text(Speed(1099511627776.0)) == "1024.00 GB/s");
}

TEST_CASE("Speed formatting preserves negative input")
{
    CHECK(Text(Speed(-1536)) == "-1536 B/s");
}

TEST_CASE("Settings defaults are one second and behind windows")
{
    const wperf::AppSettings settings;
    CHECK(settings.updateIntervalMs == 1000);
    CHECK_FALSE(settings.alwaysOnTop);
}

TEST_CASE("Stored settings preserve valid values and endpoints")
{
    for(int32_t interval : {250, 1250, 60000}) {
        const auto settings = wperf::SettingsFromStoredValues(interval, true);
        CHECK(settings.updateIntervalMs == interval);
        CHECK(settings.alwaysOnTop);
    }
}

TEST_CASE("Stored interval values are clamped")
{
    CHECK(wperf::SettingsFromStoredValues(-1, false).updateIntervalMs == 250);
    CHECK(wperf::SettingsFromStoredValues(0, false).updateIntervalMs == 250);
    CHECK(wperf::SettingsFromStoredValues(60001, false).updateIntervalMs == 60000);
    CHECK(wperf::SettingsFromStoredValues((std::numeric_limits<int32_t>::max)(), false).updateIntervalMs == 60000);
}

TEST_CASE("Dialog accepts valid interval text including endpoints")
{
    wperf::AppSettings settings;
    struct Example { const wchar_t* text; int32_t expected; };
    for(const auto& example : {Example{L"250", 250}, Example{L"1500", 1500}, Example{L"60000", 60000}}) {
        wperf::ApplyIntervalText(settings, example.text);
        CHECK(settings.updateIntervalMs == example.expected);
    }
}

TEST_CASE("Dialog retains prior settings for malformed and out of range text")
{
    wperf::AppSettings settings{1750, true};
    for(const wchar_t* text : {L"", L"abc", L"-1", L"249", L"60001"}) {
        wperf::ApplyIntervalText(settings, text);
        CHECK(settings.updateIntervalMs == 1750);
        CHECK(settings.alwaysOnTop);
    }
}

TEST_CASE("Dialog preserves existing integer prefix parsing")
{
    wperf::AppSettings settings;
    wperf::ApplyIntervalText(settings, L" 1500ms");
    CHECK(settings.updateIntervalMs == 1500);
}

TEST_CASE("CPU percentage includes user and non-idle kernel time")
{
    CHECK(wperf::CpuUsageFromDeltas(30, 60, 40) == doctest::Approx(70.0));
    CHECK(wperf::CpuUsageFromDeltas(2, 3, 0) == doctest::Approx(100.0 / 3.0));
}

TEST_CASE("CPU percentage handles fully idle and fully busy samples")
{
    CHECK(wperf::CpuUsageFromDeltas(100, 100, 0) == 0.0);
    CHECK(wperf::CpuUsageFromDeltas(0, 50, 50) == 100.0);
}

TEST_CASE("CPU percentage handles absent and inconsistent deltas")
{
    CHECK(wperf::CpuUsageFromDeltas(0, 0, 0) == 0.0);
    CHECK(wperf::CpuUsageFromDeltas(101, 50, 50) == 0.0);
}

TEST_CASE("CPU percentage retains 64-bit counter precision")
{
    CHECK(wperf::CpuUsageFromDeltas(1ULL << 40, 1ULL << 41, 1ULL << 41) == 75.0);
}
